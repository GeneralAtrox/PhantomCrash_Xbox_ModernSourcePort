#pragma once

#include "desktop_world_gpu.hpp"
#include <array>
#include <span>

namespace phantomcrash {
struct WorldLight {
  Vec3 position;
  float range{200};
  Vec3 direction{0, -1, 0};
  float outer_degrees{30};
  Vec3 colour{1, 1, 1};
  float intensity{2};
  float inner_degrees{20};
  // The shadow map's near plane: a light thrown far keeps its depth
  // precision (and its shadows' contact) only with a farther one.
  float near_plane{0.5F};
  // Omnidirectional and unshadowed: only position, range, colour and
  // intensity apply (fires, explosions, muzzle flashes, energy shots).
  bool point{};
  // How strongly it lights the air, against its surfaces (volumetric light).
  float air{1.0F};
  // How far its beam carries before dimming in the air (0: the air's
  // beam_fade); a searchlight's narrow beam carries far.
  float air_fade{};
  // Half the length of a strip lamp, from its position to one end (zero: a
  // point lamp). Light leaves the strip's nearest point, so its beam is as
  // wide as the strip; the gates' rows of panel lights.
  Vec3 bar{};
};

// How far behind a spotlight's position a cone of its angle would have to
// start to hold its whole strip's beam (0 for a point lamp).
[[nodiscard]] float bar_setback(const WorldLight &light);

// Whether a sphere can be lit by the light: inside its range and cone.
[[nodiscard]] bool light_reaches(const WorldLight &light, Vec3 centre, float radius);

// A bounded set of shadowed spotlights and unshadowed point lights, shared
// by the main camera and monitor feed.
class DynamicLightPass final {
public:
  static constexpr unsigned capacity = 8;
  static constexpr unsigned point_capacity = 32;
  explicit DynamicLightPass(ID3D11Device *device);
  // Returns the spotlights kept, whose shadow maps are drawn next.
  unsigned prepare(ID3D11DeviceContext *context, std::span<const WorldLight> spots,
                   std::span<const WorldLight> points);
  void begin_shadow(ID3D11DeviceContext *context, unsigned index);
  void caster(ID3D11DeviceContext *context, const GpuModel &model, const ModelDraw &draw,
              const Mat4 &world);
  void bind(ID3D11DeviceContext *context);

private:
  static constexpr unsigned resolution = 1024;
  std::array<Mat4, capacity> matrices_;
  Mat4 current_;
  Com<ID3D11Texture2D> depth_;
  std::array<Com<ID3D11DepthStencilView>, capacity> targets_;
  Com<ID3D11ShaderResourceView> view_;
  Com<ID3D11Buffer> lights_, draw_;
  Com<ID3D11VertexShader> vertex_;
  Com<ID3D11PixelShader> pixel_;
  Com<ID3D11InputLayout> layout_;
  Com<ID3D11RasterizerState> raster_;
  Com<ID3D11DepthStencilState> depth_state_;
  Com<ID3D11SamplerState> comparison_;
};
} // namespace phantomcrash
