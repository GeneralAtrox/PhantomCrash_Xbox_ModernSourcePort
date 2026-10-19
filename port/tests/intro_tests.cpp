// The intro step machine: order, first-boot steps, fades, and one tap per skip.

#include "test_support.hpp"

#include <string>

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/intro_sequence.hpp"
#include "phantomcrash/movie_clock.hpp"

using namespace phantomcrash;

namespace {

constexpr double tick = 1.0 / 60.0;

IntroScript script() {
  return parse_intro(json::parse(R"({
    "steps": [
      {"id": "logo", "type": "movie", "file": "movie/logo.sfd", "skip": {"buttons": "any"}},
      {"id": "genki", "type": "image", "fade_in": 0.5, "hold": 2.0, "fade_out": 0.5,
       "layers": [{"image": "a.png", "rect": [0, 0, 192, 192], "position": [320, 240],
                   "centered": true}],
       "skip": {"buttons": "any", "fade_out": 0.0}},
      {"id": "licence", "type": "image", "first_boot_only": true, "hold": 1.0,
       "layers": [{"image": "b.png", "rect": [0, 0, 640, 480], "position": [0, 0]}]},
      {"id": "attract", "type": "movie", "files": ["one.sfd", "two.sfd"], "rotate": true,
       "end_hold": 0.25, "fade_out": 0.5, "skip": {"buttons": "confirm"}}
    ],
    "then": "title"
  })"));
}

// Runs ticks until the step changes or `limit` ticks pass; returns the ticks run.
int run_until_change(IntroSequence &sequence, int limit, bool movie_finished = false) {
  const int start = sequence.generation();
  for (int count = 1; count <= limit; ++count) {
    sequence.update(tick, InputFrame{}, movie_finished);
    if (sequence.generation() != start) {
      return count;
    }
  }
  return limit;
}

InputFrame press(Buttons buttons) {
  return InputFrame{buttons, buttons, 1};
}

std::string id(const IntroSequence &sequence) {
  return sequence.step() == nullptr ? "done" : sequence.step()->id;
}

} // namespace

TEST_CASE("intro plays every step in order on first boot") {
  IntroSequence sequence(script(), true, 0);
  CHECK(id(sequence) == "logo");
  CHECK(sequence.movie_file() == "movie/logo.sfd");
  CHECK(sequence.next_movie_file() == "one.sfd");
  // A movie step waits for its movie, then cuts straight on (no fade-out).
  CHECK(run_until_change(sequence, 600) == 600);
  CHECK(run_until_change(sequence, 5, true) == 1);
  CHECK(id(sequence) == "genki");
  // 0.5 s in, 2 s hold, 0.5 s out = 180 ticks.
  const int genki = run_until_change(sequence, 400);
  CHECK(genki >= 180 && genki <= 183);
  CHECK(id(sequence) == "licence");
  CHECK(run_until_change(sequence, 400) >= 59);
  CHECK(id(sequence) == "attract");
  CHECK(sequence.movie_file() == "one.sfd");
  // A natural end holds the last frame, then fades to black.
  CHECK(run_until_change(sequence, 1, true) == 1);
  CHECK(!sequence.movie_playing());
  const int ending = run_until_change(sequence, 200, true);
  CHECK(ending >= 45 && ending <= 47);
  CHECK(sequence.done());
  CHECK(sequence.then() == "title");
}

TEST_CASE("later passes drop first-boot steps and rotate movies") {
  IntroSequence sequence(script(), false, 1);
  run_until_change(sequence, 5, true);
  CHECK(id(sequence) == "genki");
  run_until_change(sequence, 400);
  CHECK(id(sequence) == "attract");
  CHECK(sequence.movie_file() == "two.sfd");
}

TEST_CASE("image fades follow the step timing") {
  IntroSequence sequence(script(), true, 0);
  run_until_change(sequence, 5, true);
  CHECK(sequence.image_alpha() == 0.0F);
  for (int count = 0; count < 15; ++count) {
    sequence.update(tick, InputFrame{}, false);
  }
  CHECK(sequence.image_alpha() > 0.45F && sequence.image_alpha() < 0.55F);
  for (int count = 0; count < 60; ++count) {
    sequence.update(tick, InputFrame{}, false);
  }
  CHECK(sequence.image_alpha() == 1.0F);
  CHECK(sequence.black_alpha() == 0.0F);
}

TEST_CASE("one tap skips exactly one step") {
  IntroSequence sequence(script(), true, 0);
  sequence.update(tick, press(button::a), false);
  CHECK(id(sequence) == "genki");
  // Holding the button does not skip the next step; only a new press does.
  sequence.update(tick, InputFrame{button::a, 0, 1}, false);
  CHECK(id(sequence) == "genki");
  // Genki skips with an instant cut, even during its fade-in.
  sequence.update(tick, press(button::b), false);
  CHECK(id(sequence) == "licence");
  // The licence screen has no skip rule, so presses are ignored.
  sequence.update(tick, press(button::start), false);
  CHECK(id(sequence) == "licence");
  run_until_change(sequence, 400);
  // The attract movie only takes A or START, and a skip fades without a hold.
  sequence.update(tick, press(button::x), false);
  CHECK(sequence.movie_playing());
  sequence.update(tick, press(button::start), false);
  CHECK(!sequence.movie_playing());
  CHECK(sequence.black_alpha() >= 0.0F);
  sequence.update(tick, press(button::start), false); // ignored while fading
  const int fade = run_until_change(sequence, 200);
  CHECK(fade >= 29 && fade <= 31);
  CHECK(sequence.done());
}

TEST_CASE("intro parsing reports the step at fault") {
  CHECK_THROWS(parse_intro(json::parse(R"({"steps": [{"id": "x", "type": "slide"}]})")));
  CHECK_THROWS(parse_intro(json::parse(R"({"steps": [{"id": "x", "type": "movie"}]})")));
  CHECK_THROWS(parse_intro(json::parse(R"({"steps": [{"id": "x", "type": "image"}]})")));
  CHECK_THROWS(parse_intro(json::parse(
      R"({"steps": [{"id": "x", "type": "movie", "file": "a", "skip": {"buttons": "jump"}}]})")));
}

TEST_CASE("the shipped intro parses") {
  const auto shipped =
      parse_intro(DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("boot/intro.json"));
  CHECK(shipped.steps.size() == 5);
  CHECK(shipped.steps.front().id == "phantagram");
  CHECK(shipped.steps.back().rotate);
  // The original boots into phantomcrash.sfd: its counter (0x1d6f6c) starts at 1.
  CHECK(shipped.steps.back().files.front() == "movie/phantomcrash.sfd");
}

TEST_CASE("the audio clock trails the queue by what is still buffered") {
  using namespace phantomcrash::movie;
  // 2 s queued in total, 0.5 s still in the stream and 0.1 s in the device.
  CHECK(audio_media_clock(2.0, 192000, 384000.0, 0.1) > 1.39);
  CHECK(audio_media_clock(2.0, 192000, 384000.0, 0.1) < 1.41);
  CHECK(audio_media_clock(0.0, 0, 384000.0, 0.1) == 0.0);
  // Once the queue runs dry the clock keeps moving with wall time.
  AudioMediaClockTracker tracker;
  const double first = tracker.update(5.0, 0, 384000.0, 10.0, 0.0, 4096);
  const double later = tracker.update(5.0, 0, 384000.0, 10.5, 0.0, 4096);
  CHECK(later - first > 0.49 && later - first < 0.51);
}

TEST_CASE("audio drains one device buffer after the queue empties") {
  phantomcrash::movie::AudioDrainTracker drain;
  CHECK(!drain.update(false, 0, 1.0, 0.1));
  CHECK(!drain.update(true, 100, 1.0, 0.1));
  CHECK(!drain.update(true, 0, 1.0, 0.1));
  CHECK(drain.update(true, 0, 1.2, 0.1));
}

TEST_CASE("a movie completes only when everything has played") {
  using namespace phantomcrash::movie;
  CompletionState state{true, true, true, false, 0, true, 10.0, 10.0};
  CHECK(playback_complete(state));
  state.staged_video_frame = true;
  CHECK(!playback_complete(state));
  state.staged_video_frame = false;
  state.playback_seconds = 9.0;
  CHECK(!playback_complete(state));
  state.authored_duration_seconds = 0.0;
  CHECK(playback_complete(state));
  CHECK(decoded_video_frame_seconds(false, 0.0, 1.0, 0.04, 0.0) > 1.039);
  CHECK(decoded_video_frame_seconds(true, 2.5, 1.0, 0.04, 0.0) == 2.5);
}

namespace {

IntroScript staff_roll() {
  return parse_intro(
      DataRoots::shipped(PHANTOMCRASH_SOURCE_ROOT).document("story/staff_roll.json"));
}

// Plays the staff and epilogue movies to their ends and the fade after them.
void reach_the_end(IntroSequence &sequence) {
  run_until_change(sequence, 5, true); // staff
  run_until_change(sequence, 5, true); // epilogue, then its fade
  run_until_change(sequence, 120);
}

} // namespace

TEST_CASE("the staff roll cannot be skipped until THE END, which loops for 30 seconds") {
  IntroSequence sequence(staff_roll(), true, 0);
  CHECK(id(sequence) == "staff");
  sequence.update(tick, press(button::any), false);
  CHECK(id(sequence) == "staff");
  reach_the_end(sequence);
  REQUIRE(id(sequence) == "the_end");
  CHECK(sequence.black_alpha() > 0.9F); // fading in
  CHECK(sequence.movie_playing());
  // Its movie ending only starts it again.
  CHECK(run_until_change(sequence, 60 * 29, true) == 60 * 29);
  CHECK(id(sequence) == "the_end");
  CHECK(run_until_change(sequence, 120) < 120);
  CHECK(id(sequence) == "kudryavka");
  sequence.update(tick, press(button::a), false);
  CHECK(id(sequence) == "kudryavka");
  run_until_change(sequence, 5, true);
  CHECK(id(sequence) == "cloud");
  run_until_change(sequence, 200);
  sequence.update(tick, press(button::a), false);
  CHECK(sequence.done());
  CHECK(sequence.then() == "sleep");
  CHECK(sequence.take_sounds().empty());
}

TEST_CASE("confirming THE END after two seconds goes straight to sleep, with the sound") {
  IntroSequence sequence(staff_roll(), true, 0);
  reach_the_end(sequence);
  REQUIRE(id(sequence) == "the_end");
  sequence.update(tick, press(button::a), false);
  CHECK(id(sequence) == "the_end"); // too soon
  run_until_change(sequence, 120);
  sequence.update(tick, press(button::a), false);
  CHECK(sequence.done());
  CHECK(sequence.then() == "sleep");
  const auto sounds = sequence.take_sounds();
  REQUIRE(sounds.size() == 1);
  CHECK(sounds[0] == "confirm");
}
