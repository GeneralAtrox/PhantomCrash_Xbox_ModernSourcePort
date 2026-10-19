#include "desktop_robot_preview.hpp"

#include "desktop_draw_list.hpp"
#include "desktop_robot.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/robot_mover.hpp"

#include <cmath>
#include <exception>
#include <utility>
#include <vector>

namespace phantomcrash {
namespace {

// The camera's up, and the aspect the preview's field of view is over.
constexpr Vec3 preview_up{0.0F, 1.0F, 0.0F};
constexpr float picture_aspect = 640.0F / 480.0F;

} // namespace

// A standing robot's assembly, animator and parts (null for the parts shown
// one by one), and the items as last posed.
struct RobotPreview::Model {
  RobotAssembly assembly;
  std::unique_ptr<RobotAnimator> animator;
  std::unique_ptr<RobotVisual> visual;
  Pose pose{};
  std::vector<std::pair<int, Mat4>> lone; // a part's model and its place from the root
  bool pops_in{};                         // one part alone, growing in (0x083b0b)
  Vec3 spot;                              // where it stands (preview_spot)
  double seconds{};                       // since it was shown
  std::vector<WorldItem> items;
};

RobotPreview::RobotPreview(PreviewWorld world, const RobotPreviewScript &script,
                           const std::string &screen)
    : world_(world), script_(script) {
  if (const auto found = script.screens.find(screen); found != script.screens.end()) {
    screen_ = found->second;
    spin_ = screen_->spin;
  }
  if (world_.world == nullptr || !world_.world->available() || world_.robots == nullptr ||
      !world_.robots->available()) {
    world_ = {};
  }
}

RobotPreview::~RobotPreview() = default;

void RobotPreview::show(const PreviewRobot &robot) {
  if (shown_ == robot) {
    return;
  }
  hide();
  shown_ = robot;
  if (world_.world == nullptr || !screen_) {
    return;
  }
  auto model = std::make_unique<Model>();
  model->spot = preview_spot(world_.robots->catalogue(), script_, *screen_, robot);
  model->pops_in = robot.lone;
  if (!robot.standing()) {
    for (const PreviewPart &part :
         preview_part_display(world_.robots->catalogue(), script_, robot)) {
      if (const int loaded = world_.world->model(part.model); loaded >= 0) {
        model->lone.emplace_back(loaded, part.local);
      }
    }
    model_ = std::move(model);
    tick(0.0);
    return;
  }
  try {
    model->assembly =
        assemble_robot(world_.robots->catalogue(), world_.robots->skeletons(), robot.parts());
    model->animator = std::make_unique<RobotAnimator>(*world_.robots, model->assembly);
    model->animator->play(robot_motion::stand, true, 0.0F);
    model->visual = std::make_unique<RobotVisual>(*world_.world, model->assembly);
    if (model->visual->ready()) {
      model_ = std::move(model);
      tick(0.0);
    }
  } catch (const std::exception &error) {
    SDL_Log("robot preview: %s", error.what());
  }
}

void RobotPreview::hide() {
  shown_.reset();
  model_.reset();
}

void RobotPreview::place(const PreviewPlacement &place, const bool spins) {
  if (!screen_) {
    return;
  }
  screen_->place = place;
  screen_->spin = spins ? spin_ : 0.0F;
  if (model_) {
    if (spins && !spinning_) {
      model_->seconds = 0.0;
    }
    model_->spot = preview_spot(world_.robots->catalogue(), script_, *screen_, *shown_);
  }
  spinning_ = spins;
}

void RobotPreview::tick(const double seconds) {
  if (!model_ || !screen_) {
    return;
  }
  model_->seconds += seconds;
  const Mat4 root =
      preview_root(*screen_, model_->spot, model_->seconds, model_->animator != nullptr);
  model_->items.clear();
  if (!model_->animator) {
    Mat4 grow;
    if (model_->pops_in) {
      const float scale = lone_part_scale(script_.lone_part, model_->seconds);
      if (scale <= 0.0F) {
        return;
      }
      grow = lone_part_pop_in(script_.lone_part, scale);
    }
    for (const auto &[loaded, local] : model_->lone) {
      WorldItem item;
      item.model = loaded;
      item.world = local * grow * root;
      model_->items.push_back(std::move(item));
    }
    return;
  }
  if (script_.animate) {
    model_->animator->tick(seconds);
  }
  model_->animator->pose(root, 0.0F, 0.0F, true, model_->pose);
  model_->visual->animate(model_->animator->motion(), model_->animator->frame(),
                          model_->animator->looping(),
                          script_.animate ? static_cast<float>(seconds) : 0.0F);
  model_->visual->add_items(model_->pose, root, model_->items);
}

void RobotPreview::draw(DrawList &list, const Viewport &viewport, SDL_Renderer *renderer) const {
  if (!model_ || model_->items.empty()) {
    return;
  }
  list.custom(script_.layer, [this, viewport, renderer] { render(viewport, renderer); });
}

// The world renders the 4:3 picture's rectangle, SDL's viewport moved there.
void RobotPreview::render(const Viewport &viewport, SDL_Renderer *renderer) const {
  const PixelRect content = viewport.content();
  const SDL_Rect rect{
      static_cast<int>(std::lround(content.x)), static_cast<int>(std::lround(content.y)),
      static_cast<int>(std::lround(content.width)), static_cast<int>(std::lround(content.height))};
  if (rect.w <= 0 || rect.h <= 0) {
    return;
  }
  SDL_Rect before{};
  const bool had_viewport = SDL_RenderViewportSet(renderer);
  SDL_GetRenderViewport(renderer, &before);
  SDL_SetRenderViewport(renderer, &rect);
  // The shops' boxes cut a part shown alone to their frame, relative to the
  // viewport; a standing robot on the same screen (USED-SV) is not cut.
  SDL_Rect clip_before{};
  const bool had_clip = SDL_RenderClipEnabled(renderer);
  SDL_GetRenderClipRect(renderer, &clip_before);
  if (screen_->part_clip && model_->pops_in) {
    const auto &box = *screen_->part_clip;
    const float scale = content.width / 640.0F;
    const SDL_Rect cut{static_cast<int>(std::lround(box[0] * scale)),
                       static_cast<int>(std::lround((box[1] - lift_) * scale)),
                       static_cast<int>(std::lround((box[2] - box[0]) * scale)),
                       static_cast<int>(std::lround((box[3] - box[1]) * scale))};
    SDL_SetRenderClipRect(renderer, &cut);
  }
  WorldFrame frame;
  frame.width = rect.w;
  frame.height = rect.h;
  frame.eye = script_.eye;
  frame.view = look_at(script_.eye, script_.target, preview_up);
  frame.projection = perspective(radians(script_.fov_degrees), picture_aspect, script_.near_plane,
                                 script_.far_plane);
  if (lift_ != 0.0F) {
    // Moved up on the screen after projection: the picture's 480 canvas
    // pixels span 2 in clip space.
    Mat4 lifted;
    lifted.at(3, 1) = 2.0F * lift_ / canvas_height;
    frame.projection = frame.projection * lifted;
  }
  frame.lighting.ambient = {script_.ambient, script_.ambient, script_.ambient};
  frame.lighting.sun_colour = {script_.diffuse, script_.diffuse, script_.diffuse};
  frame.lighting.sun_direction = screen_->light_direction;
  frame.lighting.fog = false;
  frame.items = model_->items;
  frame.transparent = true;
  world_.world->draw(frame);
  SDL_SetRenderClipRect(renderer, had_clip ? &clip_before : nullptr);
  SDL_SetRenderViewport(renderer, had_viewport ? &before : nullptr);
}

} // namespace phantomcrash
