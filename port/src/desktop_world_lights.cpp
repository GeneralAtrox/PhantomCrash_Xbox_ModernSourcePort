#include "desktop_world_renderer.hpp"
#include <algorithm>
#include <exception>
#include <utility>
#include <vector>

namespace phantomcrash {
namespace {
// Skinned parts are bounded in their bind pose; a pose can reach past it.
constexpr float posed_margin = 2.0F;
} // namespace

void WorldRenderer::render_lights(const WorldFrame &frame) {
  if (!post_process_.dynamic_lighting || dynamic_failed_) {
    return;
  }
  try {
    if (!dynamic_lights_) {
      dynamic_lights_ = std::make_unique<DynamicLightPass>(device_.Get());
      SDL_Log("Dynamic lighting: eight 1024px shadow maps allocated");
    }
    std::vector<WorldLight> spots, points;
    for (const WorldLight &light : frame.lights) {
      (light.point ? points : spots).push_back(light);
    }
    const unsigned count = dynamic_lights_->prepare(context_.Get(), spots, points);
    if (count == 0) {
      return;
    }
    std::vector<Draw> draws;
    for (const auto &item : frame.items) {
      if (item.ground_shadow || item.rainbow || item.additive || item.alpha < 1 || item.optical) {
        continue;
      }
      item_draws(item, -1, frame.eye, draws);
    }
    // Each draw's bounding sphere in the world; a light draws only those
    // it can reach into its map (a ship's narrow beam reaches few).
    std::vector<std::pair<Vec3, float>> spheres;
    spheres.reserve(draws.size());
    for (const auto &draw : draws) {
      const Mat4 &w = draw.world;
      const float scale = std::max({length(Vec3{w.at(0, 0), w.at(0, 1), w.at(0, 2)}),
                                    length(Vec3{w.at(1, 0), w.at(1, 1), w.at(1, 2)}),
                                    length(Vec3{w.at(2, 0), w.at(2, 1), w.at(2, 2)})});
      spheres.emplace_back(transform_point(draw.draw->centre, w),
                           draw.draw->radius * scale + posed_margin);
    }
    for (unsigned i = 0; i < count; ++i) {
      dynamic_lights_->begin_shadow(context_.Get(), i);
      for (std::size_t d = 0; d < draws.size(); ++d) {
        const Draw &draw = draws[d];
        // Authored transparent glow/surface layers aren't solid occluders.
        if (draw.sky_node || draw.monitor || draw.blend || draw.draw->pass != 0 ||
            !light_reaches(spots[i], spheres[d].first, spheres[d].second)) {
          continue;
        }
        dynamic_lights_->caster(context_.Get(), *draw.model, *draw.draw, draw.world);
      }
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
  } catch (const std::exception &error) {
    dynamic_failed_ = true;
    SDL_Log("Dynamic lighting disabled after failure: %s", error.what());
  }
}

void WorldRenderer::render_air(const WorldFrame &frame) {
  if (frame.air.density <= 0 || !post_process_.dynamic_lighting ||
      !post_process_.volumetric_light || !dynamic_lights_ || dynamic_failed_ ||
      volumetric_failed_ || !depth_read_view_) {
    return;
  }
  try {
    if (!volumetric_) {
      volumetric_ = std::make_unique<VolumetricLightPass>(device_.Get());
    }
    dynamic_lights_->bind(context_.Get());
    volumetric_->apply(context_.Get(),
                       {colour_view_.Get(), depth_read_view_.Get(), samples_ > 1, width_, height_},
                       frame.view, frame.projection, frame.eye, frame.air);
  } catch (const std::exception &error) {
    volumetric_failed_ = true;
    SDL_Log("Volumetric light disabled after failure: %s", error.what());
  }
  // Back to the world's bindings.
  ID3D11RenderTargetView *target = colour_view_.Get();
  context_->OMSetRenderTargets(1, &target, depth_view_.Get());
  const D3D11_VIEWPORT viewport{
      0.0F, 0.0F, static_cast<float>(frame.width), static_cast<float>(frame.height), 0.0F, 1.0F};
  context_->RSSetViewports(1, &viewport);
  ID3D11Buffer *world_constants[] = {frame_constants_.Get(), draw_constants_.Get()};
  context_->PSSetConstantBuffers(0, 2, world_constants);
  context_->IASetInputLayout(shaders_.layout.Get());
  context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
  context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
}
} // namespace phantomcrash
