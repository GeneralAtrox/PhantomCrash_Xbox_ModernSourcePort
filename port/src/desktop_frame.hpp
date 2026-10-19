#pragma once

#include "desktop_textures.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/location_frame.hpp"
#include "phantomcrash/option_menu.hpp"
#include "phantomcrash/settings.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// What every town screen draws over its own picture: the day and money, the
// RIDING SV plate, the CURRENT MUSIC meter and the credit of the music
// playing, which follows the music as it changes. The font, style and credits outlive the frame.
class LocationFrame final {
public:
  LocationFrame(const GlyphFont &font, const FrameStyle &style,
                const std::vector<MusicCredit> &credits, FramePlacement placement);

  // The music file playing now, or empty for silence; the credit restarts
  // when it changes.
  void set_music(const std::string &file);
  // The meter's levels for the music heard now, before each update.
  void hear_music(const std::vector<float> &levels) {
    heard_ = levels;
  }
  void update(double tick_seconds);
  // Slides the RIDING SV plate in (once), as the screen finishes fading in.
  void start_riding_plate() {
    if (plate_seconds_ < 0.0) {
      plate_seconds_ = 0.0;
    }
  }
  void draw(DrawList &list, const QuestSave &quest) const;
  [[nodiscard]] bool day_clicked(const InputFrame &input, const QuestSave &quest) const;
  // The HUD's SAVE button, once a tick from a screen that offers it: lit
  // while the mouse is over it, and true when it was clicked. The button
  // shows only on screens that ask, so it is never drawn where it does
  // nothing.
  bool save_clicked(const InputFrame &input);
  [[nodiscard]] const FrameStyle &style() const noexcept {
    return style_;
  }
  // Loads the textures now rather than on the first frame that needs them.
  void preload(TextureCache &textures) const;

private:
  // The day and money where this screen puts them, the SAVE button with them.
  [[nodiscard]] HudStyle hud() const;

  const GlyphFont &font_;
  const FrameStyle &style_;
  const std::vector<MusicCredit> &credits_;
  FramePlacement placement_;
  MusicMeter meter_;
  std::vector<float> heard_;
  std::string file_;
  std::vector<int> credit_run_;
  double credit_seconds_{};
  double plate_seconds_{-1.0}; // since the plate started; negative until then
  bool save_shown_{};
  bool save_lit_{};
};

// A frame of only a menu's CURRENT MUSIC band: its meter and credit.
[[nodiscard]] FramePlacement credit_band(const MenuTickers &tickers);
// A placement with or without the day and money.
[[nodiscard]] inline FramePlacement with_hud(FramePlacement placement, const bool hud) {
  placement.hud = hud;
  return placement;
}

} // namespace phantomcrash
