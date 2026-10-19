// Frame pacing, the fixed logic tick, input edges, canvas fitting and settings.

#include "test_support.hpp"

#include <chrono>
#include <cmath>

#include "phantomcrash/canvas.hpp"
#include "phantomcrash/fixed_step.hpp"
#include "phantomcrash/frame_rate.hpp"
#include "phantomcrash/input.hpp"
#include "phantomcrash/settings.hpp"

using namespace phantomcrash;

namespace {
bool near(double left, double right, double tolerance = 1e-4) {
  return std::abs(left - right) <= tolerance;
}
} // namespace

TEST_CASE("frame cap snaps to common rates and clamps typed values") {
  CHECK(snap_frame_cap(59) == 60);
  CHECK(snap_frame_cap(143) == 144);
  CHECK(snap_frame_cap(75) == 76);
  CHECK(snap_frame_cap(10) == 30);
  CHECK(snap_frame_cap(500) == 144);
  CHECK(parse_frame_cap(L" 90 ") == 90);
  CHECK(parse_frame_cap(L"200") == 144);
  CHECK(!parse_frame_cap(L"9x").has_value());
  CHECK(!parse_frame_cap(L"").has_value());
}

TEST_CASE("frame pacer holds the cap and is free when uncapped") {
  using Clock = std::chrono::steady_clock;
  FramePacer pacer;
  const FrameRateSettings capped{false, true, 100};
  pacer.wait(capped); // starts the schedule
  const auto start = Clock::now();
  for (int frame = 0; frame < 20; ++frame) {
    pacer.wait(capped);
  }
  const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
  CHECK(seconds >= 0.18);
  CHECK(seconds < 0.5);

  const auto free_start = Clock::now();
  for (int frame = 0; frame < 1000; ++frame) {
    pacer.wait(FrameRateSettings{false, false, 60});
  }
  CHECK(std::chrono::duration<double>(Clock::now() - free_start).count() < 0.05);
}

TEST_CASE("fixed step runs whole ticks and interpolates the rest") {
  FixedStep step(1.0 / 60.0);
  CHECK(step.add(1.0 / 144.0) == 0);
  CHECK(step.alpha() > 0.4 && step.alpha() < 0.43);
  CHECK(step.add(1.0 / 60.0) == 1);
  int total = 0;
  for (int frame = 0; frame < 144; ++frame) {
    total += step.add(1.0 / 144.0);
  }
  CHECK(total >= 59 && total <= 61);
  // A long stall runs at most the burst limit, then starts fresh.
  CHECK(step.add(5.0) == 8);
  CHECK(step.alpha() == 0.0);
  CHECK(step.add(-1.0) == 0);
}

TEST_CASE("fixed step carries a stalled frame's time in online play") {
  FixedStep step(1.0 / 60.0);
  step.set_carry_backlog(true);
  // 300 ms owed: caught up four ticks a frame, none of it dropped.
  int total = step.add(0.3 + 0.001);
  CHECK(total == 4);
  while (const int ticks = step.add(0.0)) {
    CHECK(ticks <= 4);
    total += ticks;
  }
  CHECK(total == 18);
  // A very long freeze owes at most one second.
  total = step.add(10.0);
  while (const int ticks = step.add(0.0)) {
    total += ticks;
  }
  CHECK(total >= 59);
  CHECK(total <= 60);
}

TEST_CASE("input edges report each press once, including short taps") {
  InputEdges edges;
  auto frame = edges.update(button::a, 0, 1);
  CHECK(frame.pressed == button::a);
  frame = edges.update(button::a, 0, 1);
  CHECK(frame.pressed == 0);
  CHECK(frame.held == button::a);
  // A tap that went down and up between two ticks arrives latched.
  frame = edges.update(0, button::start, 1);
  CHECK(frame.any_pressed(button::confirm));
  CHECK(!frame.any_pressed(button::cancel));
  frame = edges.update(button::b, 0, 2);
  CHECK(frame.any_pressed(button::cancel));
  CHECK(frame.controllers == 2);
}

TEST_CASE("button names cover single buttons and groups") {
  CHECK(button_from_name("start") == button::start);
  CHECK(button_from_name("confirm") == (button::a | button::start));
  CHECK(button_from_name("any") == button::any);
  CHECK(!button_from_name("select").has_value());
  CHECK(buttons_from_json(json::parse(R"(["a", "b"])")) == (button::a | button::b));
  CHECK_THROWS(buttons_from_json(json::parse(R"("jump")")));
}

TEST_CASE("canvas fills the height of wide windows and letterboxes narrow ones") {
  const auto wide = fit_canvas(1920, 1080);
  CHECK(near(wide.scale, 2.25));
  CHECK(near(wide.offset_x, 240.0));
  CHECK(near(wide.offset_y, 0.0));
  CHECK(near(wide.visible_left(), -240.0 / 2.25));
  CHECK(near(wide.visible_right(), 640.0 + 240.0 / 2.25));
  CHECK(near(wide.content().width, 1440.0));

  const auto square = fit_canvas(1024, 1024);
  CHECK(near(square.scale, 1.6));
  CHECK(near(square.offset_x, 0.0));
  CHECK(near(square.offset_y, 128.0));
  CHECK(near(square.to_pixel_y(480.0F), 896.0));

  const auto degenerate = fit_canvas(0, 0);
  CHECK(degenerate.scale > 0.0F);
}

TEST_CASE("settings round-trip and tolerate old or bad files") {
  Settings changed;
  changed.frame_rate = {false, true, 144};
  changed.fullscreen = true;
  changed.frame_counter = true;
  changed.quest.player_name = "Tester";
  changed.quest.day = 12;
  QuestSave slot;
  slot.player_name = "Saved";
  slot.money = 5000;
  slot.seen_events = {50, 51};
  changed.save_slots[2] = slot;
  CHECK(settings_from_json(json::parse(json::write(settings_to_json(changed)))) == changed);

  CHECK(settings_from_json(json::parse("{}")) == Settings{});
  const auto clamped = settings_from_json(json::parse(R"({"frame_rate": {"cap": 500}})"));
  CHECK(clamped.frame_rate.cap == 144);
  CHECK(load_settings("definitely/missing/settings.json") == Settings{});
}

TEST_CASE("save slots that cannot be read are marked for LOAD and SAVE's notice") {
  CHECK(!settings_from_json(json::parse(R"({"save_slots": [null, {}]})")).save_slots_unreadable);
  const auto bad_entry = settings_from_json(json::parse(R"({"save_slots": [null, 7]})"));
  CHECK(bad_entry.save_slots_unreadable);
  CHECK(settings_from_json(json::parse(R"({"save_slots": "x"})")).save_slots_unreadable);
}
