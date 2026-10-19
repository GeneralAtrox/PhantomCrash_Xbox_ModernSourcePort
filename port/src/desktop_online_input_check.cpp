#include "desktop_online_input_check.hpp"
#include "desktop_host.hpp"
#include "desktop_online_assets.hpp"
#include "desktop_online_session.hpp"
#include "desktop_training.hpp"
#include <iostream>
#include <stdexcept>

namespace phantomcrash {
namespace {
void check(const bool value, const char *message) {
  if (!value) {
    throw std::runtime_error(std::string("Desktop online input: ") + message);
  }
}
auto encoded_view(const std::optional<TargetView> &view) {
  online::PlayerInput input;
  input.view = view;
  return online::encode_input(input);
}
} // namespace

void check_desktop_online_input(DesktopHost &host, const DataRoots &roots, WorldRenderer &world,
                                RobotLibrary &robots, CoopMatch &match,
                                const TrainingScript &training) {
  // This exercises the production screen's capture path. Multi-peer delivery
  // and delayed-input convergence are separate tests, not claimed by this check.
  for (int slot = 1; slot < 3; ++slot) {
    const auto actor = match.players[slot].actor;
    match.battle->robot(actor).piloted = false;
    match.battle->take_out(actor);
    match.players[slot].actor = -1;
  }
  TrainingScript script = training;
  script.battle.courses.clear();
  script.battle.scenes.entrance = false; // allow control immediately in this test
  script.battle.scenes.exit = true;      // still construct the real scene presenter
  script.battle.scenes.computer_entrance = false;
  script.battle.scenes.computer_exit = false;
  script.battle.scenes.rider_entrance = false;
  const auto font_file = roots.find_asset("tables/font.json");
  check(font_file.has_value(), "Missing converted font");
  const auto font = parse_glyph_font(json::read_file(*font_file));
  TextureCache textures(host.renderer(), roots);
  TalkTables talk;
  TrainingContext context{roots, &world, &robots, script, nullptr, talk, {}, "Input check"};
  context.live_battle = match.battle;
  context.live_world = match.world.get();
  context.live_area = match.area;
  context.coop_match = std::shared_ptr<CoopMatch>(&match, [](CoopMatch *) {});
  context.coop_slot = 0;
  context.scene_seed = 314159;
  TrainingScreen screen(textures, font, context, TrainingLesson{"Input check", 0, 1});
  screen.prepare_online();
  const auto scene = match.rider_scenes[0];
  check(scene != nullptr, "Presenter did not register its simulation-owned scene");
  online::RoomOptions options;
  options.host = true;
  options.bind = online::Endpoint::parse("127.0.0.1", 0);
  online::Room room(options);
  DesktopMatchAssets assets(robots, world);
  // No input delay: each captured tick is checked against the tick it plays.
  match.online = std::make_shared<online::MatchSession>(room, match, 73, 1, &assets, 0, 0);
  const auto before = match.checkpoint();
  try {
    for (std::uint32_t tick = 0; tick < 6; ++tick) {
      match.battle->select_player(match.players[0].actor);
      const auto previous_view = encoded_view(match.battle->player_target_view());
      screen.draw(fit_canvas(tick % 2 ? 960 : 640, 480));
      check(encoded_view(match.battle->player_target_view()) == previous_view,
            "Drawing the local HUD changed simulation targeting");
      InputFrame input;
      input.controllers = 1;
      input.left_stick = {0.12345679F, 0.7F};
      input.right_stick = {-0.33333334F, 0.25F};
      input.mouse_aim = {0.012345679F, -0.0012345679F};
      input.held = tick == 2 ? script.battle.buttons.jump : 0;
      input.mouse_fire_left = tick == 3;
      screen.tick(input, online::tick_seconds);
      check(match.online->local_input().has_value(), "Screen did not capture controls");
      const auto captured = *match.online->local_input();
      online::PlayerInput expected;
      expected.tick = tick;
      expected.control = battle_control(input, script.battle.buttons, nullptr, true, false);
      expected.fire = battle_fire(input, script.battle.buttons);
      expected.view = captured.view;
      check(online::same_input(captured, expected), "Desktop input changed values or precision");
      check(captured.view.has_value(), "Local camera was not captured");
      check(encoded_view(match.battle->player_target_view()) == previous_view,
            "Screen tick wrote targeting outside simulation");
      check(match.online->advance(), "Single-player agreed timeline did not advance");
      check(encoded_view(match.battle->player_target_view()) == encoded_view(captured.view),
            "Simulation did not consume the captured camera/lock boxes");
    }
    match.restore_checkpoint(*before);
    check(match.rider_scenes[0] == scene, "Rollback replaced the presenter's shared scene");
  } catch (...) {
    match.online.reset();
    throw;
  }
  match.online.reset();
  std::cout << "PASS: production desktop input capture, exact controls, local targeting and shared "
               "scene restore\n";
}
} // namespace phantomcrash
