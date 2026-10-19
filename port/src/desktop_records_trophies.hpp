#pragma once

#include "desktop_world_renderer.hpp"
#include "phantomcrash/canvas.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/records_script.hpp"

#include <vector>

struct SDL_Renderer;

namespace phantomcrash {

class DrawList; // desktop_draw_list.hpp, which includers take before <windows.h>

// One trophy shown this frame: a node of the recordcup model at its cell.
struct TrophyPlace {
  int node{-1};
  Vec3 at;
  float tilt{}; // radians about x
};

// CONVENTION RECORD's turning trophies (RecordTrophies), drawn through the
// 3D world renderer in the 4:3 picture.
class RecordsTrophyView final {
public:
  RecordsTrophyView(WorldRenderer *world, SDL_Renderer *renderer, const RecordTrophies &style);
  void tick(double seconds);
  void draw(DrawList &list, const Viewport &viewport, std::vector<TrophyPlace> places) const;

private:
  void render(const Viewport &viewport, const std::vector<TrophyPlace> &places) const;

  WorldRenderer *world_;
  SDL_Renderer *renderer_;
  const RecordTrophies &style_;
  int model_{-1};
  float spin_{};
};

} // namespace phantomcrash
