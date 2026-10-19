#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <string_view>

namespace phantomcrash {

// The world renderer's one shader pair and its vertex layout (ModelVertex).
struct WorldShaders {
  Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
};

// The shadow passes' shaders (desktop_world_shadows.cpp): the volumes, with
// their layout (ShadowVertex), and the cover laid where they counted.
struct ShadowShaders {
  Microsoft::WRL::ComPtr<ID3D11VertexShader> volume;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> static_volume;
  Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> cover;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> cover_colour;
};

// Compiles the HLSL at run time (d3dcompiler_47 ships with Windows).
[[nodiscard]] WorldShaders create_world_shaders(ID3D11Device *device);
[[nodiscard]] ShadowShaders create_shadow_shaders(ID3D11Device *device);

// Compiles `entry` of `source`; throws std::runtime_error with the errors.
[[nodiscard]] Microsoft::WRL::ComPtr<ID3DBlob>
compile_world_hlsl(std::string_view source, const char *entry, const char *target);

} // namespace phantomcrash
