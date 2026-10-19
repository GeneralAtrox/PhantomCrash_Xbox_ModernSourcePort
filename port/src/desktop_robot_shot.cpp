#include "desktop_robot_shot.hpp"

#include "desktop_game_screens.hpp"
#include "desktop_host.hpp"
#include "desktop_robot.hpp"
#include "desktop_world_renderer.hpp"
#include "phantomcrash/garage_tables.hpp"
#include "phantomcrash/robot_library.hpp"
#include "phantomcrash/robot_motion.hpp"
#include "phantomcrash/robot_preview.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace phantomcrash {
namespace {

constexpr Vec3 shot_up{0.0F, 1.0F, 0.0F};
// A long lens, so the robot is not distorted when framed tightly.
constexpr float shot_fov_degrees = 30.0F;
constexpr float shot_near_plane = 0.1F;
constexpr float shot_far_plane = 1000.0F;
// Brighter than the menus' preview (diffuse 1, ambient 0.25) so the dark
// side still reads when small.
constexpr float shot_ambient = 0.4F;
constexpr float shot_diffuse = 1.0F;
// The light travels from above, over the camera's left shoulder: these
// weights are along the camera's forward, down and right.
constexpr float light_forward = 1.0F;
constexpr float light_down = 1.2F;
constexpr float light_right = 0.6F;
// The hidden window the renderer needs; the shot has its own target.
constexpr int host_width = 640;
constexpr int host_height = 480;

struct ShotCamera {
  Vec3 eye;
  Vec3 target;
};

// The camera orbits the robot, which faces -z: yaw 0 looks it in the face.
ShotCamera shot_camera(const Options::RobotShot &shot) {
  const float yaw = radians(shot.yaw_degrees);
  const float pitch = radians(shot.pitch_degrees);
  const Vec3 target{0.0F, shot.height, 0.0F};
  const Vec3 offset{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                    -std::cos(yaw) * std::cos(pitch)};
  return {target + offset * shot.distance, target};
}

// SDL reads back the premultiplied colour the world composited.
void unpremultiply(SDL_Surface &surface) {
  for (int y = 0; y < surface.h; ++y) {
    auto *row = static_cast<std::uint8_t *>(surface.pixels) + y * surface.pitch;
    for (int x = 0; x < surface.w; ++x) {
      std::uint8_t *pixel = row + x * 4;
      const int alpha = pixel[3];
      for (int c = 0; c < 3; ++c) {
        pixel[c] =
            alpha == 0
                ? 0
                : static_cast<std::uint8_t>(std::min(255, (pixel[c] * 255 + alpha / 2) / alpha));
      }
    }
  }
}

} // namespace

void render_robot_shot(const Options::RobotShot &shot, const DataRoots &roots) {
  const auto garage = converted_table(roots, "tables/garage.json");
  if (!garage) {
    throw std::runtime_error("tables/garage.json has not been converted");
  }
  const GarageTables tables = parse_garage_tables(*garage);
  if (shot.record >= static_cast<int>(tables.robots.size())) {
    throw std::runtime_error("No robot record " + std::to_string(shot.record));
  }
  const PreviewRobot robot = preview_record(tables.robots[static_cast<std::size_t>(shot.record)]);
  if (!robot.standing()) {
    throw std::runtime_error("Robot record " + std::to_string(shot.record) +
                             " has no body and legs");
  }

  DesktopHost host(false, true, std::pair(host_width, host_height));
  SDL_Renderer *renderer = host.renderer();
  WorldRenderer world(renderer, roots);
  RobotLibrary library(roots);
  if (!world.available() || !library.available()) {
    throw std::runtime_error("The 3D renderer or the robots are unavailable");
  }
  const RobotAssembly assembly =
      assemble_robot(library.catalogue(), library.skeletons(), robot.parts());
  RobotAnimator animator(library, assembly);
  animator.play(robot_motion::stand, true, 0.0F);
  RobotVisual visual(world, assembly);
  if (!visual.ready()) {
    throw std::runtime_error("The robot's models did not load");
  }

  RobotPreviewScreen still;
  const Mat4 root = preview_root(still, 0.0, false);
  Pose pose{};
  animator.pose(root, 0.0F, 0.0F, true, pose);

  const ShotCamera camera = shot_camera(shot);
  const Vec3 forward = normalize(camera.target - camera.eye);
  const Vec3 right = normalize(cross(shot_up, forward));
  WorldFrame frame;
  frame.width = shot.size;
  frame.height = shot.size;
  frame.eye = camera.eye;
  frame.view = look_at(camera.eye, camera.target, shot_up);
  frame.projection = perspective(radians(shot_fov_degrees), 1.0F, shot_near_plane, shot_far_plane);
  frame.lighting.ambient = {shot_ambient, shot_ambient, shot_ambient};
  frame.lighting.sun_colour = {shot_diffuse, shot_diffuse, shot_diffuse};
  frame.lighting.sun_direction =
      normalize(forward * light_forward - shot_up * light_down - right * light_right);
  frame.lighting.fog = false;
  frame.transparent = true;
  visual.add_items(pose, root, frame.items);

  SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_TARGET, shot.size, shot.size);
  if (target == nullptr || !SDL_SetRenderTarget(renderer, target)) {
    SDL_DestroyTexture(target);
    throw std::runtime_error(std::string("Unable to make the shot's target: ") + SDL_GetError());
  }
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);
  world.draw(frame);
  SDL_Surface *read = SDL_RenderReadPixels(renderer, nullptr);
  SDL_SetRenderTarget(renderer, nullptr);
  SDL_DestroyTexture(target);
  SDL_Surface *rgba = read == nullptr ? nullptr : SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(read);
  if (rgba == nullptr) {
    throw std::runtime_error(std::string("Unable to read the shot back: ") + SDL_GetError());
  }
  unpremultiply(*rgba);
  const bool saved = IMG_SavePNG(rgba, shot.file.string().c_str());
  SDL_DestroySurface(rgba);
  if (!saved) {
    throw std::runtime_error("Unable to save " + shot.file.string() + ": " + SDL_GetError());
  }
}

} // namespace phantomcrash
