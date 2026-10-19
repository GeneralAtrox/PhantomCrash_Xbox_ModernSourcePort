// Desktop entry point: finds the game data, opens the window and runs the
// fixed-tick loop with paced presentation.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

#include "desktop_host.hpp"
#include "phangrade_desktop.hpp"
#include "desktop_coop_storage.hpp"
#include "desktop_frame_counter.hpp"
#include "desktop_game_data.hpp"
#include "desktop_slai.hpp"
#include "phantomcrash/slai_content.hpp"
#include "desktop_input.hpp"
#include "desktop_debug_log.hpp"
#include "desktop_live_resize.hpp"
#include "desktop_online_diagnostics.hpp"
#include "desktop_options.hpp"
#include "desktop_tools.hpp"
#include "desktop_scene_dump.hpp"
#include "desktop_screens.hpp"
#include "desktop_settings_menu.hpp"
#include "desktop_window_title.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/fixed_step.hpp"
#include "phantomcrash/frame_rate.hpp"
#include "phantomcrash/frame_stats.hpp"
#include "phantomcrash/saved_time.hpp"
#include "phantomcrash/settings.hpp"

namespace phantomcrash {
namespace {

// The original's logic runs at 60 Hz (0xac73b8 frames per second).
constexpr double logic_tick_seconds = 1.0 / 60.0;
// A benchmark leaves out its first seconds (loading, the first textures)
// and measures against 144 fps, the frame cap's top.
constexpr double benchmark_warmup_seconds = 2.0;
constexpr double benchmark_target_rate = 144.0;

// The data documents the boot flow needs; loading them checks the data and
// mod overlay before the window opens.
constexpr const char *documents[] = {
    "boot/intro.json",    "ui/title.json",         "ui/main_menu.json",
    "ui/option.json",     "ui/message_box.json",   "ui/placeholders.json",
    "audio/music.json",   "audio/sfx.json",        "story/new_game.json",
    "ui/name_entry.json", "story/talk.json",       "story/scenes.json",
    "ui/load_game.json",  "ui/garage.json",        "ui/town_map.json",
    "ui/battle_hud.json", "ui/battle_panels.json", "audio/battle_sounds.json",
    "world/rumble.json",  "story/staff_roll.json"};

std::filesystem::path find_root(const Options &options) {
  if (options.root) {
    return *options.root;
  }
  std::vector<std::filesystem::path> starts{std::filesystem::current_path()};
  if (const char *base = SDL_GetBasePath()) {
    starts.emplace_back(std::filesystem::path(reinterpret_cast<const char8_t *>(base)));
  }
  if (auto root = DataRoots::find_root(starts)) {
    return *root;
  }
  throw std::runtime_error("Unable to find the data folder (data/boot/intro.json). "
                           "Run from the checkout or pass --root.");
}

std::filesystem::path settings_path() {
  char *folder = SDL_GetPrefPath("PhantomCrash", "PhantomCrash Port");
  if (folder == nullptr) {
    return "settings.json";
  }
  std::filesystem::path path = std::filesystem::path(reinterpret_cast<const char8_t *>(folder));
  SDL_free(folder);
  return path / "settings.json";
}

void apply_fullscreen(DesktopHost &host, SettingsMenu &menu) {
  const bool wanted = menu.settings().fullscreen;
  if (wanted == host.fullscreen()) {
    return;
  }
  if (wanted) {
    menu.set_visible(false);
    host.set_fullscreen(true);
  } else {
    host.set_fullscreen(false);
    menu.set_visible(true);
  }
}

int run(const Options &options) {
  const auto root = find_root(options);
  const bool smoke =
      options.smoke_frames > 0 || options.robot_shot || options.online_resource_check ||
      options.online_stress_seconds > 0.0;
  const bool benchmark = options.benchmark_seconds > 0.0;
  if (!prepare_game_data(root, !smoke && !benchmark)) {
    return 0;
  }
  const auto configuration = options.settings.value_or(settings_path());
  start_debug_log(configuration.parent_path() / "phantomcrash.log");
  const bool slai = load_settings(configuration).slai_enabled;
  if (slai && !prepare_slai_data(root, !smoke && !benchmark)) {
    return 1;
  }
  const auto roots = DataRoots::configured(root, slai);
  if (const auto result = run_desktop_tool(options, roots)) {
    return *result;
  }
  for (const char *document : documents) {
    static_cast<void>(roots.document(document));
  }
  const auto preferences = content_profile_path(configuration, slai, legacy_slai(root));
  const Settings saved = load_content_settings(configuration, preferences, slai);
  if (!smoke && !std::filesystem::exists(preferences) && !save_settings(preferences, saved)) {
    throw std::runtime_error("Cannot create the selected content mode's settings and saves");
  }
  Settings initial = saved;
  if (options.fullscreen) {
    initial.fullscreen = *options.fullscreen;
  }

  DesktopHost host(phangrade::initial_fullscreen(initial.fullscreen), smoke, options.size);
  SettingsMenu menu(smoke ? nullptr : host.native_window(), initial);
  menu.set_visible(!initial.fullscreen);
  phangrade::DesktopSession session(host, menu, smoke);
  DesktopInput input;
  FixedStep step(logic_tick_seconds);
  DesktopOnlineDiagnostics diagnostics;
  FramePacer pacer;
  FrameCounter counter;
  SDL_Log("Data root: %s", root.string().c_str());
  auto coop = make_coop_state(options, saved, preferences, root, smoke);
  ScreenRunner screens(host.renderer(), roots, options.online ? "coop" : options.start, saved.game,
                       saved.quest, saved.vs_scoobees, saved.audio, coop);
  screens.set_save_slots(saved.save_slots, saved.save_slots_unreadable);
  screens.set_rumble_robots(options.rumble_robots.value_or(saved.rumble_robots));
  const std::vector<ScreenRunner::Level> levels = screens.levels();
  {
    std::vector<std::string> labels;
    for (const ScreenRunner::Level &level : levels) {
      labels.push_back(level.label);
    }
    menu.set_levels(labels);
  }
  menu.set_cheat_days(screens.cheat_days());
  PostProcessOptions persisted_post = saved.post_process;
  AudioOptions persisted_audio = saved.audio;
  FrameRateSettings persisted_rate = initial.frame_rate;
  bool persisted_fullscreen = initial.fullscreen;
  bool persisted_counter = initial.frame_counter;
  bool persisted_skip = initial.skip_conversations;
  bool persisted_coop = initial.experimental_coop;
  CoopOptions persisted_coop_options = initial.coop;
  CheatOptions persisted_cheats = initial.cheats;
  bool paused = false;

  using Clock = std::chrono::steady_clock;
  auto previous = Clock::now();
  int frames = 0;
  long long ticks_run = 0; // logic ticks so far: when --press presses land
  FrameStats stats;
  FrameStats work; // each frame's time before it is presented
  double benchmark_clock = 0.0;
  bool running = true;
  // The copy icon (or F9, for fullscreen without the menu bar): the game
  // context, dumped once this frame is drawn.
  bool dump_requested = false;
  WindowTitle title(host.window());
  LiveResizeTicks live_resize([&] { return coop->online && !smoke; },
                              [&] { run_neutral_ticks(step, screens, previous); });
  while (running) {
    input.configure(menu.settings().host_controls);
    input.capture_mouse(host.window(), screens.mouse_aim() && host.focused() && !smoke,
                        menu.frame_active());
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if (is_fullscreen_toggle(event)) {
        menu.set_fullscreen(!menu.settings().fullscreen);
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                 event.key.scancode == SDL_SCANCODE_F9) {
        dump_requested = !smoke;
      }
      input.handle(event);
    }
    live_resize.rethrow();
    // Native menu interaction can change focus/capture while SDL pumps events.
    input.capture_mouse(host.window(), screens.mouse_aim() && host.focused() && !smoke,
                        menu.frame_active());
    input.set_text_mode(host.window(), screens.wants_text() && host.focused() && !smoke);
    if (!smoke) {
      update_slai_menu(menu, root, configuration);
    }
    const Settings &settings = menu.settings();
    screens.set_coop_enabled(settings.experimental_coop || bool(coop->open_online));
    coop->options = settings.coop;
    screens.set_post_process(settings.post_process);
    screens.set_audio_options(settings.audio);
    screens.set_cheats(settings.cheats);
    screens.set_skip_conversations(settings.skip_conversations);
    if (!smoke &&
        (settings.post_process != persisted_post || settings.audio != persisted_audio ||
         settings.frame_rate != persisted_rate || settings.fullscreen != persisted_fullscreen ||
         settings.frame_counter != persisted_counter ||
         settings.experimental_coop != persisted_coop || settings.coop != persisted_coop_options ||
         settings.cheats != persisted_cheats || settings.skip_conversations != persisted_skip)) {
      // Save the preference at selection time without checkpointing the live match.
      Settings disk = load_settings(preferences);
      disk.post_process = settings.post_process;
      disk.audio = settings.audio;
      disk.frame_rate = settings.frame_rate;
      disk.frame_counter = settings.frame_counter;
      disk.experimental_coop = settings.experimental_coop;
      disk.coop = settings.coop;
      disk.cheats = settings.cheats;
      disk.skip_conversations = settings.skip_conversations;
      if (settings.fullscreen != persisted_fullscreen) {
        disk.fullscreen = settings.fullscreen;
      }
      if (disk.save_slots_unreadable || !save_settings(preferences, disk)) {
        SDL_Log("Unable to save audio/rendering preferences");
      }
      persisted_post = settings.post_process;
      persisted_audio = settings.audio;
      persisted_rate = settings.frame_rate;
      persisted_fullscreen = settings.fullscreen;
      persisted_counter = settings.frame_counter;
      persisted_coop = settings.experimental_coop;
      persisted_coop_options = settings.coop;
      persisted_cheats = settings.cheats;
      persisted_skip = settings.skip_conversations;
    }
    apply_fullscreen(host, menu);
    FrameRateSettings rate = settings.frame_rate;
    if (benchmark) {
      rate.vsync = false; // as fast as it goes
      rate.cap_enabled = false;
    }
    // The menu bar and its tooltip repaint cleanly only without tearing.
    host.set_vsync(rate.vsync || menu.frame_active());
    if (menu.take_interaction()) {
      // Time spent in the menu or the dialog is not game time.
      step.reset();
      pacer.reset();
      previous = Clock::now();
    }
    if (const auto level = menu.take_level();
        level && *level >= 0 && *level < static_cast<int>(levels.size())) {
      screens.jump(levels[static_cast<std::size_t>(*level)].screen);
    }
    if (const auto cheat = menu.take_cheat()) {
      screens.cheat(*cheat);
    }
    dump_requested = (menu.take_copy() && !smoke) || dump_requested;
    const bool pause = !smoke && !benchmark && !coop->online && settings.pause_unfocused &&
                       !host.focused() && !menu.visuals_open();
    if (pause != paused) {
      paused = pause;
      screens.set_paused(paused);
    }
    if (paused) {
      SDL_WaitEventTimeout(nullptr, 100);
      step.reset();
      pacer.reset();
      previous = Clock::now();
      continue;
    }

    const auto now = Clock::now();
    double elapsed = std::chrono::duration<double>(now - previous).count();
    previous = now;
    // Smoke runs step one logic tick a frame, so --press frames are game
    // ticks (60 a second) however fast the machine presents.
    if (smoke) {
      elapsed = step.tick_seconds();
    }
    step.set_carry_backlog(coop->online != nullptr);
    const int ticks = smoke ? 1 : step.add(elapsed);
    diagnostics.frame(elapsed, ticks, coop->online != nullptr);
    const long long first = ticks_run;
    const long long last = ticks_run + std::max(ticks, 1) - 1;
    for (const auto &press : options.presses) {
      if (press.frame == press.last && first <= press.frame && press.frame <= last) {
        input.press(press.buttons, press.port);
      } else if (press.frame <= last && first <= press.last) {
        input.hold(press.buttons, press.port);
      }
    }
    ticks_run += ticks;
    for (const auto &point : options.points) {
      if (first <= point.frame && point.frame <= last) {
        input.point(point.canvas, point.click);
      }
    }
    Viewport viewport = screens.placed(host.viewport());
    for (int tick = 0; tick < ticks; ++tick) {
      screens.tick(input.sample(host.focused(), viewport), step.tick_seconds());
    }
    viewport.tick_lag = smoke ? 0.0 : step.alpha() * step.tick_seconds();
    input.rumble(screens.rumble(), screens.screen() != "coop");
    screens.advance(elapsed);
    screens.draw(viewport);
    if (dump_requested) {
      dump_requested = false;
      static_cast<void>(dump_scene(host.renderer(), screens, settings, preferences));
    }
    counter.record(elapsed);
    title.update(elapsed, counter.rate(), screens.scene_progress());
    if (benchmark) {
      benchmark_clock += elapsed;
      if (benchmark_clock > benchmark_warmup_seconds) {
        stats.add(elapsed);
      }
      running = benchmark_clock < benchmark_warmup_seconds + options.benchmark_seconds;
    }
    if (settings.frame_counter) {
      counter.draw(host.renderer());
    }
    if (smoke && ++frames >= options.smoke_frames) {
      running = false;
      if (options.capture) {
        save_frame(host.renderer(), *options.capture);
      }
    }
    if (benchmark && benchmark_clock > benchmark_warmup_seconds) {
      work.add(std::chrono::duration<double>(Clock::now() - now).count());
    }
    SDL_RenderPresent(host.renderer());
    pacer.wait(rate);
  }

  if (benchmark) {
    int width = 0;
    int height = 0;
    SDL_GetCurrentRenderOutputSize(host.renderer(), &width, &height);
    const double budget = 1.0 / benchmark_target_rate;
    const std::string report = std::to_string(width) + "x" + std::to_string(height) +
                               "\n  frames: " + stats.report(budget) +
                               "\n  work:   " + work.report(budget);
    SDL_Log("Benchmark %s", report.c_str());
    std::printf("Benchmark %s\n", report.c_str());
    return 0;
  }
  Settings to_save = menu.settings();
  copy_coop_profiles(*coop, to_save);
  to_save.game = screens.game_options();
  to_save.quest = screens.quest_save();
  to_save.vs_scoobees = screens.vs_scoobees();
  to_save.save_slots = screens.save_slots();
  if (options.fullscreen) {
    to_save.fullscreen = saved.fullscreen;
  }
  if (!smoke && !(to_save == saved)) {
    // A changed game is stamped with the time it is written, for LOAD GAME.
    if (!to_save.quest.player_name.empty() && !(to_save.quest == saved.quest)) {
      to_save.quest.saved = local_time_text();
    }
    save_settings(preferences, to_save);
  }
  return 0;
}

} // namespace
} // namespace phantomcrash

int main(int argc, char *argv[]) {
  // Automated runs must never wait on a modal error box.
  bool interactive = true;
  for (int index = 1; index < argc; ++index) {
    interactive = interactive && std::string(argv[index]) != "--smoke-test" &&
                  std::string(argv[index]) != "--benchmark" &&
                  std::string(argv[index]) != "--online-resource-check" &&
                  std::string(argv[index]) != "--render-robot";
  }
  try {
    const auto options = phantomcrash::parse_options(argc, argv);
    if (options.help) {
      std::fputs(phantomcrash::usage(), stdout);
      return 0;
    }
    return phantomcrash::run(options);
  } catch (const std::exception &error) {
    SDL_Log("%s", error.what());
    if (interactive) {
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PhantomCrash port", error.what(), nullptr);
    }
    return 1;
  }
}
