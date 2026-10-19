#include "test_support.hpp"
#include "phantomcrash/source_pmt.hpp"
#include "phantomcrash/source_leg_playback.hpp"
#include "phantomcrash/skeleton.hpp"
#include <cmath>

using namespace phantomcrash;

namespace {
void near(const float a, const float b) {
  CHECK(std::abs(a - b) < .0001F);
}
SourcePmtTrack translation_track() {
  SourcePmtTrack track;
  for (const auto [duration, x] : {std::pair{2.0F, 0.0F}, {3.0F, 4.0F}, {99.0F, 10.0F}}) {
    SourcePmtKey key;
    key.duration = duration;
    key.position = Vec3{x, 0, 0};
    track.keys.push_back(key);
  }
  return track;
}
} // namespace

TEST_CASE("source PMT cursors traverse segments and exclude the terminal duration") {
  const auto track = translation_track();
  SourcePmtCursor cursor;
  near(track.period(), 5);
  cursor.advance(track, 2, true);
  CHECK(cursor.key == 1);
  near(cursor.elapsed, 0);
  near(cursor.sample(track).at(3, 0), 4);
  cursor.advance(track, 3, true);
  CHECK(cursor.key == 0);
  near(cursor.elapsed, 0);
  near(cursor.sample(track).at(3, 0), 0);
  cursor.advance(track, 12, true);
  CHECK(cursor.key == 1);
  near(cursor.elapsed, 0);
  cursor.reset(track, 12, true);
  CHECK(cursor.key == 1);
  near(cursor.elapsed, 0);
}

TEST_CASE("source nonloop endpoints restore elapsed rather than clamp to the endpoint") {
  const auto track = translation_track();
  SourcePmtCursor cursor;
  cursor.advance(track, 1, false);
  near(cursor.sample(track).at(3, 0), 2);
  cursor.advance(track, 4, false);
  CHECK(cursor.key == 1);
  CHECK(cursor.finished);
  near(cursor.elapsed, 1);
  near(cursor.sample(track).at(3, 0), 6);
  cursor.advance(track, -2, false);
  near(cursor.elapsed, 1);
  cursor.advance(track, 0, false);
  near(cursor.elapsed, 1);
  cursor.advance(track, 20, false);
  near(cursor.sample(track).at(3, 0), 6);
}

TEST_CASE("source PMT sampling uses normalized short quaternion arcs across sign changes") {
  SourcePmtTrack track;
  SourcePmtKey a, b;
  a.duration = 4;
  a.rotation = std::array<float, 4>{0, 0, 0, 2};
  const float s = std::sqrt(.5F);
  b.rotation = std::array<float, 4>{2 * s, 0, 0, -2 * s};
  track.keys = {a, b};
  const Mat4 mid = sample_source_pmt(track, 2);
  // Native [-x,y,z,w] gives -45 degrees about X halfway to source +90.
  near(mid.at(1, 1), s);
  near(mid.at(1, 2), -s);
  near(mid.at(2, 1), s);
  SourcePmtTrack sign = track;
  for (float &x : *sign.keys[1].rotation) {
    x = -x;
  }
  const Mat4 same = sample_source_pmt(sign, 2);
  for (std::size_t i = 0; i < 16; ++i) {
    near(mid.m[i], same.m[i]);
  }
}

TEST_CASE("source leg controls evaluate before independent key clocks advance") {
  SkeletonNode root;
  root.track = 0;
  root.source_model_root = true;
  SkeletonNode leg;
  leg.track = 1;
  root.children.push_back(leg);
  Motion clip;
  clip.frames = 5;
  clip.tracks.resize(2);
  for (auto &track : clip.tracks) {
    track.source_pmt = translation_track();
  }
  SourceLegPlayback state;
  state.reset(root);
  state.play(1, 0, &clip, true);
  MotionSource source;
  source.motion = &clip;
  source.frame = 5; // the native global clock does not sample this control
  for (const float x : {0.0F, 2.0F, 4.0F, 6.0F, 8.0F, 0.0F}) {
    state.advance(source, 1.0F / 60, true, true, 1);
    near(state.locals()[1]->at(3, 0), x);
    near(state.locals()[0]->at(3, 0), 0);
  }
  state.play(1, 0, &clip, true);
  state.advance(source, 1.0F / 60, true, true, 2);
  near(state.locals()[1]->at(3, 0), 0);
  state.advance(source, 1.0F / 60, true, true, 0);
  near(state.locals()[1]->at(3, 0), 4);
  state.advance(source, 1.0F / 60, true, true, 0);
  near(state.locals()[1]->at(3, 0), 4);
}
