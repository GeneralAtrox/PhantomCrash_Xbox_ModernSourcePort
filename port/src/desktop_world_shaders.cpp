#include "desktop_world_shaders.hpp"
#include "desktop_dynamic_light_shader.hpp"

#include <d3dcompiler.h>

#include <stdexcept>
#include <string>

namespace phantomcrash {
namespace {

// Material flags and ambient RGB/power come from 0x0b3240. Vertex colour
// supplies diffuse; bump batches light the tangent-space normal map and
// specular batches add an eye-dependent highlight after texturing. This
// native per-pixel lighting retains the original batch controls. Matrices
// are row-major for row vectors, as on the CPU side.
//
// Optic Camouflage follows multi_optical.xvu and Blend.xpu: per vertex, how
// much is seen (inside the sphere) and where the frame behind is picked:
// the vertex's clip position moved by its normal, pushed and projected
// without the translation (the original adds P * n to the position before
// the divide); per pixel, the lit robot over that pick.
constexpr char source[] = R"hlsl(
cbuffer Frame : register(b0) {
  row_major float4x4 view_projection;
  float3 eye;           float fog_start;
  float3 ambient;       float fog_end;
  float3 sun_direction; float fog_enabled;
  float3 sun_colour;    float shadow_length;
  float3 fog_colour;    float frame_pad1;
};

cbuffer Draw : register(b1) {
  row_major float4x4 world;
  float4 base_colour;
  float alpha_cutoff;
  float unlit;
  float has_base;
  float has_normal;
  float alpha_test;
  float fogged;
  float item_alpha;
  float flash;
  float2 uv_offset;
  float optical;         // 1: Optic Camouflage
  float local_light;
  float4 optical_sphere; // centre, radius
  float4 optical_shape;  // edge, reach, least, push
  float4 uv_transform;   // UV scale and offset (TV atlas rectangle -> complete live feed)
  float4 rainbow;        // phase, UV scroll, enabled, unused
  float4 material_light; // ambient RGB, specular power
  float4 material_modes; // specular, texture add, texture alpha, original
  float4 source_directions[2];
  float4 source_colours[2];
  float4 reflect_modes;  // reflect stage (normal_texture), sheen, unused x2
  float4 label_colour;   // where the texture's alpha is below 1, when w is set
};

struct VertexIn {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float4 colour : COLOR;
  float2 uv : TEXCOORD0;
};

struct VertexOut {
  float4 position : SV_Position;
  float3 world_position : WORLDPOS;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float4 colour : COLOR;
  float2 uv : TEXCOORD0;
  float4 behind : TEXCOORD1;
  float seen : TEXCOORD2;
  float fog_depth : TEXCOORD3;
};

VertexOut vs_main(VertexIn input) {
  VertexOut output;
  float4 placed = mul(float4(input.position, 1.0), world);
  output.position = mul(placed, view_projection);
  // The original's vertex programs fog by the clip-space z (multi_matrix.xvu
  // writes oFog = r3.z), and the fixed-function draws by the depth: range
  // fog is never enabled. Neither is the distance from the eye.
  output.fog_depth = output.position.z;
  output.world_position = placed.xyz;
  float3x3 axes = (float3x3)world;
  float3x3 cofactor = float3x3(cross(axes[1], axes[2]), cross(axes[2], axes[0]),
                              cross(axes[0], axes[1]));
  float handedness = dot(axes[0], cofactor[0]) < 0.0 ? -1.0 : 1.0;
  // Inverse transpose up to a positive scale, removed by normalization.
  output.normal = mul(input.normal, cofactor) * handedness;
  output.tangent = float4(mul(input.tangent.xyz, axes), input.tangent.w * handedness);
  output.colour = input.colour;
  output.uv = input.uv * uv_transform.xy + uv_transform.zw + uv_offset;
  output.behind = output.position;
  output.seen = 1.0;
  if (rainbow.z > 0.5) {
    float3 phase = rainbow.x + float3(-0.6666, 0.0, 0.6666);
    float3 d = abs(input.position.y + phase);
    float3 bands = abs(step(1.0, d) * 2.0 - d);
    // rainbow.xvu dots the light direction with the model's own normal, not
    // a world one, so the lit side turns with the spinning shell; oD0 clamps.
    float3 light = ambient + sun_colour * saturate(dot(normalize(input.normal), -sun_direction));
    output.colour = float4(saturate(0.75 * light + 0.5 * bands), 1.0);
    output.uv = input.position.xy + float2(0.5, 0.5 + rainbow.y);
  }
  if (optical > 0.5) {
    float d = distance(placed.xyz, optical_sphere.xyz);
    output.seen = saturate((optical_sphere.w - d) / optical_shape.x);
    float share = clamp((optical_sphere.w + optical_shape.y - d) / optical_shape.y,
                        optical_shape.z, 1.0);
    float3 n = normalize(output.normal);
    output.behind.xy += mul(float4(n * share * optical_shape.w, 0.0), view_projection).xy;
  }
  return output;
}

Texture2D base_texture : register(t0);
Texture2D normal_texture : register(t1);
Texture2D scene_texture : register(t2);
SamplerState base_sampler : register(s0);
SamplerState normal_sampler : register(s1);
SamplerState scene_sampler : register(s2);

// Sun shadows (desktop_sun_shadows.cpp): unbound, the buffer reads 0 and
// every point is in the sun.
cbuffer SunShadow : register(b3) {
  row_major float4x4 sun_projection;
  float4 sun_shadow; // enabled, texel size, red debug, shadow strength
  // PCSS: penumbra radius per unit of blocker distance (0 = hard 3x3),
  // depth range in world units, map uv per world unit, blocker search reach in uv
  float4 sun_soft;
};
Texture2D<float> sun_depth : register(t4);
SamplerComparisonState sun_sampler : register(s4);
// Golden-angle spiral: sample i of count, turned by the pixel's noise angle.
float2 sun_spiral(int i, int count, float turn) {
  float r = sqrt((i + 0.5) / count);
  float a = i * 2.39996323 + turn;
  return r * float2(cos(a), sin(a));
}
float sun_visibility(float3 position, float3 normal, float2 pixel) {
  float4 at = mul(float4(position + normal * 0.08, 1.0), sun_projection);
  float2 uv = at.xy * float2(0.5, -0.5) + 0.5;
  // Receiver plane: how the map depth changes across uv on this surface, so
  // wide filters compare against the receiver's own plane, not one flat depth.
  float3 du = ddx(float3(uv, at.z));
  float3 dv = ddy(float3(uv, at.z));
  float det = du.x * dv.y - dv.x * du.y;
  float2 slope = abs(det) > 1e-14
                     ? float2(du.z * dv.y - dv.z * du.y, dv.z * du.x - du.z * dv.x) / det
                     : float2(0, 0);
  slope = clamp(slope, -1.0, 1.0);
  if (sun_shadow.x < 0.5) return 1.0;
  if (any(uv < 0) || any(uv > 1) || at.z > 1) return 1.0;
  if (sun_soft.x <= 0) {
    float seen = 0;
    [unroll] for (int y = -1; y <= 1; ++y) {
      [unroll] for (int x = -1; x <= 1; ++x) {
        seen += sun_depth.SampleCmpLevelZero(sun_sampler, uv + float2(x, y) * sun_shadow.y, at.z);
      }
    }
    return seen / 9.0;
  }
  float turn = 6.2831853 * frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
  float size = 1.0 / sun_shadow.y;
  float margin = 0.05 / sun_soft.y; // 5 cm in depth units
  // 1. Blocker search: the mean depth of what stands between here and the sun.
  float reach = max(sun_soft.w, 3.0 * sun_shadow.y);
  float blockers = 0;
  float found = 0;
  [unroll] for (int b = 0; b < 16; ++b) {
    float2 o = sun_spiral(b, 16, turn) * reach;
    int2 texel = clamp(int2((uv + o) * size), 0, int(size) - 1);
    float d = sun_depth.Load(int3(texel, 0));
    if (d < at.z + dot(slope, o) - margin) {
      blockers += d;
      found += 1;
    }
  }
  if (found < 0.5) return 1.0;
  // 2. Penumbra: wider the further the blocker is above the receiver.
  float distance = (at.z - blockers / found) * sun_soft.y;
  float radius = clamp(distance * sun_soft.x * sun_soft.z, sun_shadow.y, reach);
  // 3. Filter over that radius.
  float seen = 0;
  [unroll] for (int s = 0; s < 32; ++s) {
    float2 o = sun_spiral(s, 32, turn) * radius;
    seen += sun_depth.SampleCmpLevelZero(sun_sampler, uv + o, at.z + dot(slope, o));
  }
  return seen / 32.0;
}

float4 ps_main(VertexOut input, bool front : SV_IsFrontFace) : SV_Target {
  // A supply crate's lettering (the texture's alpha below 1) takes the label
  // colour in place of the body's.
  bool labelled = label_colour.w > 0.5 && has_base > 0.5;
  float3 tint = base_colour.rgb;
  if (labelled) {
    tint = lerp(label_colour.rgb, tint, base_texture.Sample(base_sampler, input.uv).a);
  }
  float4 colour = float4(input.colour.rgb * tint, input.colour.a * base_colour.a);
  float3 highlight = 0.0;
  float3 n = normalize(input.normal) * (front ? 1.0 : -1.0);
  // Where the sun map is blocked, the stage's shadow colour.
  float sun_blocked = sun_shadow.x > 0.5 && rainbow.z < 0.5
                          ? 1.0 - sun_visibility(input.world_position, normalize(input.normal),
                                                 input.position.xy)
                          : 0.0;
  if ((unlit < 0.5 || local_light > 0.5) && rainbow.z < 0.5) {
    if (has_normal > 0.5 && reflect_modes.x < 0.5) {
      float3 t = input.tangent.xyz - n * dot(n, input.tangent.xyz);
      t = normalize(t + 1e-6);
      float3 b = cross(n, t) * input.tangent.w;
      float3 m = normal_texture.Sample(normal_sampler, input.uv).xyz * 2.0 - 1.0;
      n = normalize(t * m.x + b * m.y + n * m.z);
    }
  }
  if (unlit < 0.5 && rainbow.z < 0.5) {
    float diffuse = saturate(dot(n, -sun_direction));
    float3 source_light = 0;
    [unroll] for (int i = 0; i < 2; ++i) {
      source_light += source_colours[i].rgb * saturate(dot(n, source_directions[i].xyz));
    }
    colour.rgb *= saturate(ambient * material_light.rgb + sun_colour * diffuse + source_light);
    if (material_modes.x > 0.5 && diffuse > 0.0) {
      float3 toward_eye = normalize(eye - input.world_position);
      float3 halfway = normalize(toward_eye - sun_direction);
      float specular = pow(saturate(dot(n, halfway)), max(material_light.w, 1e-4));
      highlight = sun_colour * specular;
    }
  }
  if (reflect_modes.y > 0.0) {
    // Port sheen (the supply drops' metal crates): a rim toward the eye and
    // the sun's glint, in the item's own colour.
    float3 toward_eye = normalize(eye - input.world_position);
    float rim = pow(1.0 - saturate(dot(n, toward_eye)), 2.0);
    float glint = pow(saturate(dot(n, normalize(toward_eye - sun_direction))), 24.0);
    highlight += reflect_modes.y * base_colour.rgb * (rim * 0.6 + glint * sun_colour);
  }
  if (local_light > 0.5) {
    colour.rgb += input.colour.rgb * tint * local_lighting(input.world_position, n);
  }
  if (has_base > 0.5) {
    float4 texel = base_texture.Sample(base_sampler, input.uv);
    if (reflect_modes.x > 0.5) {
      // reflect.xvu: the second stage at the world normal's x and z;
      // reflect.xpu adds it to the base before the light modulates both.
      float3 shine = normal_texture.Sample(normal_sampler, n.xz).rgb;
      texel.rgb = saturate(texel.rgb + shine);
    }
    if (rainbow.z > 0.5) {
      colour.a *= texel.a;
    } else {
      colour.rgb = material_modes.y > 0.5 ? saturate(colour.rgb + texel.rgb)
                                          : colour.rgb * texel.rgb;
      if (material_modes.z > 0.5 && !labelled) {
        colour.a = material_modes.w > 0.5 ? texel.a * base_colour.a : colour.a * texel.a;
      }
    }
  }
  if (alpha_test > 0.5) {
    clip(colour.a - alpha_cutoff);
  }
  colour.rgb += highlight * (1.0 - sun_blocked);
  colour.rgb *= 1.0 - sun_shadow.w * sun_blocked;
  if (sun_shadow.z > 0.5) colour.rgb = lerp(colour.rgb, float3(1.0, 0.0, 0.0), 0.6 * sun_blocked);
  if (fog_enabled > 0.5 && fogged > 0.5) {
    float amount = saturate((input.fog_depth - fog_start) / max(fog_end - fog_start, 1e-3));
    colour.rgb = fogged > 1.5 ? colour.rgb * (1.0 - amount) : lerp(colour.rgb, fog_colour, amount);
  }
  colour.rgb = lerp(colour.rgb, float3(1.0, 1.0, 1.0), flash * 0.6);
  if (optical > 0.5) {
    float2 at = input.behind.xy / input.behind.w * float2(0.5, -0.5) + 0.5;
    float3 behind = scene_texture.Sample(scene_sampler, at).rgb;
    colour.rgb = lerp(behind, colour.rgb, input.seen);
  }
  colour.a *= item_alpha;
  return colour;
}
)hlsl";

} // namespace

Microsoft::WRL::ComPtr<ID3DBlob> compile_world_hlsl(const std::string_view hlsl, const char *entry,
                                                    const char *target) {
  Microsoft::WRL::ComPtr<ID3DBlob> code;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  const HRESULT result =
      D3DCompile(hlsl.data(), hlsl.size(), "phantomcrash_world", nullptr, nullptr, entry, target,
                 D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
  if (FAILED(result)) {
    std::string message = "World shader ";
    message += entry;
    message += " failed to compile";
    if (errors) {
      message += ": ";
      message += static_cast<const char *>(errors->GetBufferPointer());
    }
    throw std::runtime_error(message);
  }
  return code;
}

WorldShaders create_world_shaders(ID3D11Device *device) {
  WorldShaders shaders;
  const std::string hlsl = std::string(dynamic_light_hlsl) + source;
  const auto vertex = compile_world_hlsl(hlsl, "vs_main", "vs_5_0");
  const auto pixel = compile_world_hlsl(hlsl, "ps_main", "ps_5_0");
  if (FAILED(device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(),
                                        nullptr, &shaders.vertex)) ||
      FAILED(device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr,
                                       &shaders.pixel))) {
    throw std::runtime_error("Unable to create the world shaders");
  }
  const D3D11_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  if (FAILED(device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)),
                                       vertex->GetBufferPointer(), vertex->GetBufferSize(),
                                       &shaders.layout))) {
    throw std::runtime_error("Unable to create the world vertex layout");
  }
  return shaders;
}

} // namespace phantomcrash
