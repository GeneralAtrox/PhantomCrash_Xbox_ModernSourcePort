#include "desktop_world_renderer.hpp"

#include <algorithm>
#include <limits>

namespace phantomcrash {

std::array<float, 4> WorldRenderer::monitor_uv(const Draw &draw) const {
  std::array<float, 2> low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
  std::array<float, 2> high{-low[0], -low[1]};
  const auto &model = draw.model->data;
  for (std::uint32_t i = 0; i < draw.draw->index_count; ++i) {
    const auto &uv = model.vertices[model.indices[draw.draw->first_index + i]].uv;
    for (int axis = 0; axis < 2; ++axis) {
      low[axis] = std::min(low[axis], uv[axis]);
      high[axis] = std::max(high[axis], uv[axis]);
    }
  }
  const float x = high[0] > low[0] ? 1.0F / (high[0] - low[0]) : 1.0F;
  const float y = high[1] > low[1] ? 1.0F / (high[1] - low[1]) : 1.0F;
  return {x, y, -low[0] * x, -low[1] * y};
}

// Reuse the main scene's targets, then save the feed before the main view
// clears them. This shares all models and lighting passes, with one extra
// persistent texture at native viewport resolution. No SDL composition or
// HUD enters the feed. Screens in the feed itself are black, avoiding
// recursive TV feedback and read/write hazards.
void WorldRenderer::render_monitor(const WorldFrame &frame) {
  if (frame.width != width_ || frame.height != height_) {
    return;
  }
  D3D11_TEXTURE2D_DESC desc{};
  if (monitor_texture_) {
    monitor_texture_->GetDesc(&desc);
  }
  if (desc.Width != static_cast<UINT>(width_) || desc.Height != static_cast<UINT>(height_) ||
      desc.Format != format_) {
    monitor_view_.Reset();
    monitor_texture_.Reset();
    desc = {};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = format_;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &monitor_texture_)) ||
        FAILED(
            device_->CreateShaderResourceView(monitor_texture_.Get(), nullptr, &monitor_view_))) {
      SDL_Log("Unable to create the battle monitor texture");
      return;
    }
  }
  // The prior presentation may have left a TV texture bound.
  ID3D11ShaderResourceView *none = nullptr;
  context_->PSSetShaderResources(0, 1, &none);
  capturing_monitor_ = true;
  render(frame);
  capturing_monitor_ = false;
  context_->PSSetShaderResources(0, 1, &none);
  if (samples_ > 1) {
    context_->ResolveSubresource(monitor_texture_.Get(), 0, colour_.Get(), 0, format_);
  } else {
    context_->CopyResource(monitor_texture_.Get(), colour_.Get());
  }
}

} // namespace phantomcrash
