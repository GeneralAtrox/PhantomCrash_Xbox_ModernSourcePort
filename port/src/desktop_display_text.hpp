#pragma once

#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/sprite_text.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>

namespace phantomcrash {

// Outline and shadow round display lettering, in window pixels.
struct DisplayEdge {
  std::optional<std::uint32_t> outline;
  float outline_pixels{};
  std::optional<TextShadow> shadow; // offset already in window pixels
  float shadow_blur{};              // window pixels the shadow spreads
};

// Where a string goes in its box: centred or from the left edge, keeping its
// proportions or filling the box (stretch), leaning by slant (x per y, the
// box's bottom edge fixed).
struct DisplayLayout {
  bool centered{};
  bool stretch{};
  float slant{};
  bool right{}; // against the box's right edge
  // Sized and set as the font's capitals are, rather than fitted by its own
  // ink: lowercase stays smaller and sits on the capitals' line ("m", "m/s").
  bool capitals{};
};

// Whole strings drawn from an installed outline font (DisplayFont) at the
// window's own size, for the menus' painted lettering: buttons, headers and
// banners. Each string is rasterized once per size and kept.
class DisplayText final {
public:
  DisplayText(SDL_Renderer *renderer, const DataRoots &roots);
  ~DisplayText();
  DisplayText(const DisplayText &) = delete;
  DisplayText &operator=(const DisplayText &) = delete;

  // False when none of the font's faces is installed.
  [[nodiscard]] bool available(const DisplayFont &font);
  // Draws `text` with its ink fitted into `box` (window pixels) as `layout`
  // places it. False when the font is unavailable or the text cannot be drawn.
  bool draw(const DisplayFont &font, const std::string &text, const SDL_FRect &box,
            const DisplayLayout &layout, std::uint32_t colour, float alpha,
            const DisplayEdge &edge);

private:
  struct Raster {
    SDL_Texture *texture{};
    float width{}, height{}; // the ink, in pixels
    float top{};             // the ink's top below the line's
  };
  struct Impl; // the platform's rasterizer
  [[nodiscard]] const std::string *face(const DisplayFont &font);
  [[nodiscard]] const Raster *raster(const DisplayFont &font, const std::string &text, int em);

  SDL_Renderer *renderer_{};
  const DataRoots &roots_;
  std::unique_ptr<Impl> impl_;
  std::map<std::string, std::string> faces_; // font key -> installed face, "" when none
  // The ink box of a text at the measuring size, by font key and text.
  std::map<std::pair<std::string, std::string>, std::pair<float, float>> measured_;
  std::map<std::tuple<std::string, std::string, int>, Raster> rasters_;
};

} // namespace phantomcrash
