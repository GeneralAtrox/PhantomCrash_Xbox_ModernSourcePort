#pragma once

#include "desktop_dynamic_lights.hpp"
#include "desktop_world_gpu.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/math3d.hpp"

#include <array>

// Light seen in the air: spotlight beams and the glow around fires and
// blasts (a port addition, drawn while Dynamic lighting is on). Tuned per
// stage by "volumetric" in data/world/dynamic_lighting.json.
namespace phantomcrash {

struct VolumetricAir {
  float density{};             // light scattered per unit travelled; 0 is clear air
  float anisotropy{0.4F};      // how strongly the air scatters forward (0 evenly, toward 1)
  float max_distance{300.0F};  // a ray is marched no farther than this
  int steps{24};               // samples through each spotlight's reach
  int point_steps{6};          // ... and each point light's
  int max_point_lights{8};     // the point lights nearest the eye that light the air
  float resolution{0.5F};      // the march's size against the frame
  float height_base{0.0F};     // the air is thickest below this height ...
  float height_falloff{80.0F}; // ... and thins by e every this many units above it
  float beam_fade{60.0F};      // past this distance from its lamp a beam dims as 1/d^2
};

// The stage's air, or clear air when it has none.
[[nodiscard]] VolumetricAir load_volumetric_air(const DataRoots &roots, int stage);

class VolumetricLightPass final {
public:
  explicit VolumetricLightPass(ID3D11Device *device);

  struct Target {
    ID3D11RenderTargetView *colour{};
    ID3D11ShaderResourceView *depth{};
    bool multisampled{};
    int width{}, height{};
  };
  // Adds the air's light to `target`. The dynamic lights must be bound.
  void apply(ID3D11DeviceContext *context, const Target &target, const Mat4 &view,
             const Mat4 &projection, Vec3 eye, const VolumetricAir &air);

private:
  void ensure_target(int width, int height);

  Com<ID3D11Device> device_;
  Com<ID3D11VertexShader> vertex_;
  std::array<Com<ID3D11PixelShader>, 2> march_, composite_; // single-sampled, multisampled
  Com<ID3D11Buffer> constants_;
  Com<ID3D11BlendState> additive_, opaque_;
  Com<ID3D11RasterizerState> raster_;
  Com<ID3D11DepthStencilState> depth_off_;
  Com<ID3D11Texture2D> scattered_;
  Com<ID3D11RenderTargetView> scattered_target_;
  Com<ID3D11ShaderResourceView> scattered_view_;
  int width_{}, height_{};
};

} // namespace phantomcrash
