#pragma once

// Light scattered toward the eye by the air (a port addition). Appended to
// dynamic_light_hlsl, whose spotlights, point lights and shadow maps it reads.
// ps_march steps each view ray through every light's reach at reduced
// resolution; ps_composite adds the result to the frame, taking only the
// taps whose ray ended near this pixel's, so beams keep the edges of what
// stands in front of them.
namespace phantomcrash::volumetric_shader {
inline constexpr char source[] = R"hlsl(
cbuffer Air : register(b0) {
  float3 eye;          float density;
  float3 ray_right;    float anisotropy;   // the camera's right over the projection's x scale
  float3 ray_up;       float max_distance; // its up over the y scale
  float3 ray_forward;  float max_points;
  float depth_a;       float depth_b;      float steps;        float point_steps;
  float2 source_size;  float2 target_size;
  float height_base;   float height_falloff; float beam_fade;  float air_pad;
};
#ifdef MULTISAMPLED
Texture2DMS<float> scene_depth : register(t0);
#else
Texture2D<float> scene_depth : register(t0);
#endif
Texture2D<float4> scattered : register(t1);

struct ScreenOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

ScreenOut vs_air(uint id : SV_VertexID) {
  ScreenOut output;
  float2 uv = float2((id << 1) & 2, id & 2);
  output.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  output.uv = uv;
  return output;
}

// Distance along the view axis to what the pixel shows; far for the sky.
float view_depth(int2 pixel) {
#ifdef MULTISAMPLED
  float d = scene_depth.Load(pixel, 0);
#else
  float d = scene_depth.Load(int3(pixel, 0));
#endif
  return d >= 1.0 ? 1e9 : depth_b / min(d - depth_a, -1e-7);
}

// Where the ray through `uv` goes and how far it may travel.
float3 view_ray(float2 uv, out float ray_scale) {
  float2 ndc = uv * float2(2, -2) + float2(-1, 1);
  float3 ray = ray_right * ndc.x + ray_up * ndc.y + ray_forward;
  ray_scale = length(ray);
  return ray / ray_scale;
}

// Henyey-Greenstein, scaled so that evenly scattering air is 1.
float phase(float cosine) {
  float g = anisotropy;
  return (1 - g * g) / pow(max(1 + g * g - 2 * g * cosine, 1e-4), 1.5);
}

// The air thins above height_base, by e every height_falloff units.
float thickness(float y) {
  return exp(-max(y - height_base, 0) / height_falloff);
}

// Interleaved gradient noise: each pixel starts its steps a little apart,
// which the composite's taps smooth into an even beam.
float jitter(float2 pixel) {
  return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// The part of the ray (0 to `limit`) inside a light's reach.
bool reach_span(float3 dir, float3 centre, float radius, float limit, out float t0, out float t1) {
  float3 offset = eye - centre;
  float b = dot(offset, dir);
  float h = b * b - (dot(offset, offset) - radius * radius);
  t0 = 0;
  t1 = 0;
  if (h <= 0) {
    return false;
  }
  h = sqrt(h);
  t0 = max(-b - h, 0);
  t1 = min(-b + h, limit);
  return t1 > t0;
}

// Narrows [t0, t1] to where the ray is inside light i's cone, so that its
// samples fall within the beam: the span from the first part inside to the
// last, in front of the lamp.
bool cone_span(int i, float3 dir, inout float t0, inout float t1) {
  float3 axis = lights[i].direction_outer.xyz;
  float cos2 = lights[i].direction_outer.w * lights[i].direction_outer.w;
  // A strip's beam: the cone from behind it that holds every point's.
  float3 offset = eye - (lights[i].position_range.xyz - axis * spot_bar[i].w);
  float along = dot(dir, axis);
  float ahead = dot(offset, axis);
  float lo = t0;
  float hi = t1;
  // In front of the lamp: ahead + t * along >= 0.
  if (abs(along) > 1e-6) {
    float t = -ahead / along;
    if (along > 0) lo = max(lo, t); else hi = min(hi, t);
  } else if (ahead < 0) {
    return false;
  }
  // Inside the double cone: a t^2 + b t + c >= 0.
  float a = along * along - cos2;
  float b = 2 * (along * ahead - cos2 * dot(dir, offset));
  float c = ahead * ahead - cos2 * dot(offset, offset);
  if (abs(a) < 1e-6) {
    if (abs(b) > 1e-9) {
      float t = -c / b;
      if (b > 0) lo = max(lo, t); else hi = min(hi, t);
    } else if (c < 0) {
      return false;
    }
  } else {
    float disc = b * b - 4 * a * c;
    if (disc < 0) {
      if (a < 0) return false; // never inside
    } else {
      float s = sqrt(disc);
      float r1 = min((-b - s) / (2 * a), (-b + s) / (2 * a));
      float r2 = max((-b - s) / (2 * a), (-b + s) / (2 * a));
      if (a < 0) {
        lo = max(lo, r1);
        hi = min(hi, r2);
      } else { // inside before r1 and after r2
        bool before = min(hi, r1) > lo;
        bool after = hi > max(lo, r2);
        if (!before && !after) return false;
        float first = before ? lo : max(lo, r2);
        hi = after ? hi : min(hi, r1);
        lo = first;
      }
    }
  }
  t0 = lo;
  t1 = hi;
  return hi > lo;
}

float spot_scatter(int i, float3 dir, float t0, float t1, float start) {
  LocalLight light = lights[i];
  float air = spot_air[i].x;
  float fade = spot_air[i].y > 0 ? spot_air[i].y : beam_fade;
  if (air <= 0) return 0;
  float step = (t1 - t0) / steps;
  float sum = 0;
  [loop] for (int k = 0; k < (int)steps; ++k) {
    float t = t0 + (k + start) * step;
    float3 at = eye + dir * t;
    float3 delta = at - emitter(i, at);
    float d = length(delta);
    float3 away = delta / max(d, 0.0001);
    float cone = smoothstep(light.direction_outer.w, light.colour_inner.w,
                            dot(away, light.direction_outer.xyz));
    float falloff = saturate(1.0 - d / light.position_range.w);
    // A beam spreads as it leaves the lamp: the air it lights dims with the
    // square of the distance once past its fade distance.
    float spread = 1.0 / (1.0 + (d / fade) * (d / fade));
    float weight = cone * falloff * falloff * spread * thickness(at.y);
    if (weight <= 0) continue;
    float4 clip_position = mul(float4(at, 1), light.projection);
    if (clip_position.w <= 0) continue;
    float3 projected = clip_position.xyz / clip_position.w;
    float2 uv = projected.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0) || any(uv > 1) || projected.z < 0 || projected.z > 1) continue;
    float lit = light_depth.SampleCmpLevelZero(light_sampler, float3(uv, i),
                                               projected.z - 0.00005);
    sum += weight * lit * phase(dot(away, -dir));
  }
  return sum * step * air;
}

float point_scatter(int p, float3 dir, float t0, float t1, float start) {
  float4 light = point_position_range[p];
  float step = (t1 - t0) / point_steps;
  float sum = 0;
  [loop] for (int k = 0; k < (int)point_steps; ++k) {
    float t = t0 + (k + start) * step;
    float3 delta = eye + dir * t - light.xyz;
    float d = length(delta);
    float falloff = saturate(1.0 - d / light.w);
    sum += falloff * falloff * thickness(eye.y + dir.y * t) *
           phase(dot(delta / max(d, 0.0001), -dir));
  }
  return sum * step;
}

// rgb: the light scattered toward the eye; a: how far the ray went.
float4 ps_march(ScreenOut input) : SV_Target {
  int2 pixel = min(int2(input.uv * source_size), int2(source_size) - 1);
  float ray_scale;
  float3 dir = view_ray(input.uv, ray_scale);
  float limit = min(view_depth(pixel) * ray_scale, max_distance);
  float start = jitter(input.position.xy);
  float3 result = 0;
  float t0, t1;
  [loop] for (int i = 0; i < (int)light_count.x; ++i) {
    float reach = lights[i].position_range.w + length(spot_bar[i].xyz);
    if (reach_span(dir, lights[i].position_range.xyz, reach, limit, t0, t1) &&
        cone_span(i, dir, t0, t1)) {
      result += lights[i].colour_inner.rgb * spot_scatter(i, dir, t0, t1, start);
    }
  }
  // The point lights come nearest the eye first; only the first few light the air.
  int points = min((int)light_count.z, (int)max_points);
  [loop] for (int p = 0; p < points; ++p) {
    if (point_colour[p].w > 0 &&
        reach_span(dir, point_position_range[p].xyz, point_position_range[p].w, limit, t0, t1)) {
      result += point_colour[p].rgb * (point_colour[p].w * point_scatter(p, dir, t0, t1, start));
    }
  }
  return float4(result * density, limit);
}

float4 ps_composite(ScreenOut input) : SV_Target {
  int2 pixel = int2(input.position.xy);
  float2 uv = input.position.xy / source_size;
  float ray_scale;
  view_ray(uv, ray_scale);
  float limit = min(view_depth(pixel) * ray_scale, max_distance);
  float2 centre = uv * target_size - 0.5;
  int2 base = int2(floor(centre));
  float2 f = centre - base;
  float3 sum = 0;
  float total = 0;
  [unroll] for (int y = -1; y <= 2; ++y) {
    [unroll] for (int x = -1; x <= 2; ++x) {
      int2 at = clamp(base + int2(x, y), int2(0, 0), int2(target_size) - 1);
      float4 tap = scattered.Load(int3(at, 0));
      float2 spread = abs(float2(x, y) - f);
      float tent = saturate(1.5 - spread.x * 0.75) * saturate(1.5 - spread.y * 0.75);
      float w = tent * exp(-abs(tap.a - limit) / (0.05 * limit + 0.5));
      sum += tap.rgb * w;
      total += w;
    }
  }
  return float4(total > 1e-4 ? sum / total : 0, 0);
}
)hlsl";
} // namespace phantomcrash::volumetric_shader
