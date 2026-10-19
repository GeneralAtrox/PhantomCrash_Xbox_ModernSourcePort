#pragma once

#include "desktop_world_gpu.hpp"

#include <array>

namespace phantomcrash {

// SMAA 1x over the resolved 3D scene. Owns its resources independently of
// world materials and SDL state; the caller saves/restores the device state.
class SmaaPass {
public:
  explicit SmaaPass(ID3D11Device *device);
  void apply(ID3D11DeviceContext *context, ID3D11ShaderResourceView *scene, ID3D11Texture2D *output,
             DXGI_FORMAT format);

private:
  struct Target {
    Com<ID3D11Texture2D> texture;
    Com<ID3D11RenderTargetView> render;
    Com<ID3D11ShaderResourceView> read;
  };
  struct PixelShader {
    Com<ID3D11PixelShader> shader;
    int linear{-1};
    int point{-1};
  };
  void resize(UINT width, UINT height, DXGI_FORMAT format);
  Com<ID3D11Device> device_;
  Com<ID3D11VertexShader> vertex_;
  std::array<PixelShader, 3> pixels_;
  Com<ID3D11Buffer> constants_;
  Com<ID3D11SamplerState> linear_, point_;
  Com<ID3D11BlendState> opaque_;
  Com<ID3D11DepthStencilState> depth_off_;
  Com<ID3D11RasterizerState> raster_;
  Com<ID3D11ShaderResourceView> area_, search_;
  std::array<Target, 3> targets_;
  UINT width_{}, height_{};
  DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
};

} // namespace phantomcrash
