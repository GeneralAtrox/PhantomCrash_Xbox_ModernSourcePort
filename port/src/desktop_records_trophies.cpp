#include "desktop_draw_list.hpp"
#include "desktop_records_trophies.hpp"

#include "phantomcrash/skeleton.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <utility>

namespace phantomcrash {

RecordsTrophyView::RecordsTrophyView(WorldRenderer *world, SDL_Renderer *renderer,
                                     const RecordTrophies &style)
    : world_(world), renderer_(renderer), style_(style) {
  if (world_ != nullptr && world_->available()) {
    model_ = world_->model(style_.model);
  }
}

// One spin turns them all (+0x64, 0.02 a frame).
void RecordsTrophyView::tick(const double seconds) {
  spin_ += style_.spin_per_frame * style_.frame_rate * static_cast<float>(seconds);
}

void RecordsTrophyView::draw(DrawList &list, const Viewport &viewport,
                             std::vector<TrophyPlace> places) const {
  if (model_ < 0 || places.empty()) {
    return;
  }
  list.custom(style_.layer,
              [this, viewport, places = std::move(places)] { render(viewport, places); });
}

// 0x334fc: moved to its cell, tilted about x, then turned about its own y.
// The world renders the 4:3 picture's rectangle, SDL's viewport moved there.
void RecordsTrophyView::render(const Viewport &viewport,
                               const std::vector<TrophyPlace> &places) const {
  const PixelRect content = viewport.content();
  const SDL_Rect rect{
      static_cast<int>(std::lround(content.x)), static_cast<int>(std::lround(content.y)),
      static_cast<int>(std::lround(content.width)), static_cast<int>(std::lround(content.height))};
  if (rect.w <= 0 || rect.h <= 0) {
    return;
  }
  SDL_Rect before{};
  const bool had_viewport = SDL_RenderViewportSet(renderer_);
  SDL_GetRenderViewport(renderer_, &before);
  SDL_SetRenderViewport(renderer_, &rect);
  WorldFrame frame;
  frame.width = rect.w;
  frame.height = rect.h;
  frame.eye = style_.eye;
  frame.view = look_at(style_.eye, style_.target, {0.0F, 1.0F, 0.0F});
  frame.projection = perspective(radians(style_.fov_degrees),
                                 static_cast<float>(rect.w) / static_cast<float>(rect.h),
                                 style_.near_plane, style_.far_plane);
  frame.lighting.fog = false;
  frame.lighting.sun_direction = normalize(style_.light_direction);
  frame.lighting.sun_colour = {style_.diffuse, style_.diffuse, style_.diffuse};
  frame.lighting.ambient = {style_.ambient, style_.ambient, style_.ambient};
  for (const TrophyPlace &place : places) {
    WorldItem item;
    item.model = model_;
    item.node = place.node;
    item.world = rotation_y(spin_) * rotation_x(place.tilt) * translation(place.at);
    frame.items.push_back(std::move(item));
  }
  frame.transparent = true;
  world_->draw(frame);
  SDL_SetRenderViewport(renderer_, had_viewport ? &before : nullptr);
}

} // namespace phantomcrash
