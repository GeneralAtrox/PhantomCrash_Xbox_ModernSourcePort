#pragma once

#include "desktop_world_gpu.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/math3d.hpp"

#include <d3d11_1.h>

// Sun shadows (a port addition): one orthographic depth map seen from the
// stage's sun covers the ground around the camera, and the world shader takes
// the sun's diffuse and highlight away where the map says the sun is blocked,
// with soft edges (PCSS) that widen with the blocker's height. They replace
// the original's stencil shadow volumes and the robots' ground shadows, which
// stay as the fallback when the pass is off or fails. Tuned by "sun_shadows"
// in data/world/dynamic_lighting.json.
namespace phantomcrash {

struct WorldFrame;

struct SunShadowTuning {
  bool enabled{true};
  unsigned resolution{4096};  // the depth map's width and height in texels
  float half_width{110.0F};   // world units the map covers either side of its centre
  float ahead{70.0F};         // the centre, this far in front of the eye
  float sun_distance{400.0F}; // the map's eye, this far back along the sun
  // The sun's apparent diameter in degrees: a stylised hazy sun, about six
  // times the real one (0.53); 0 keeps a hard 3x3 filter.
  float sun_degrees{3.0F};
  // The widest blur radius in world units, so tall thin casters (lattices,
  // fences) keep their shape under the wide sun.
  float widest_blur{0.25F};
  float farthest_blocker{40.0F}; // the blocker search looks this far above the receiver
  int depth_bias{200};
  float slope_depth_bias{2.0F};
  bool debug{}; // shadowed ground tinted red
};

[[nodiscard]] SunShadowTuning load_sun_shadow_tuning(const DataRoots &roots);

class SunShadows final {
public:
  SunShadows(ID3D11Device *device, const SunShadowTuning &tuning);
  // Clears the map and sets the context up to draw casters into it.
  void begin(ID3D11DeviceContext *context, const WorldFrame &frame);
  void caster(ID3D11DeviceContext *context, const GpuModel &model, const ModelDraw &draw,
              const Mat4 &world);
  // Binds the map for the world shader (t4, s4, b3).
  void bind(ID3D11DeviceContext *context);
  // Leaves every point in the sun.
  static void unbind(ID3D11DeviceContext *context);

private:
  SunShadowTuning tuning_;
  Mat4 matrix_;
  float strength_{};
  Com<ID3D11Texture2D> depth_;
  Com<ID3D11DepthStencilView> target_;
  Com<ID3D11ShaderResourceView> view_;
  Com<ID3D11Buffer> caster_constants_, sun_constants_;
  Com<ID3D11VertexShader> vertex_;
  Com<ID3D11PixelShader> pixel_;
  Com<ID3D11InputLayout> layout_;
  Com<ID3D11RasterizerState> raster_;
  Com<ID3D11DepthStencilState> depth_state_;
  Com<ID3D11SamplerState> comparison_;
};

} // namespace phantomcrash
