#pragma once

#include "desktop_town_screen.hpp"
#include "phantomcrash/battle_music.hpp"

#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;

// BATTLE MUSIC (BattleMusic): the program discs, the menu with its
// television, the program list, the stock of owned tracks and the play mode,
// sliding between one another, the help line and the town frame. The music
// edited goes back into the quest once the screen is done.
class BattleMusicScreen final : public TownScreen {
public:
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  [[nodiscard]] std::optional<MenuPresence> peer_presence(const MenuPresence &peer) const override;
  BattleMusicScreen(TextureCache &textures, const GlyphFont &font, BattleMusicScript script,
                    const std::string &from, const FrameStyle &frame,
                    const std::vector<MusicCredit> &credits, QuestSave quest);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::string music() const override {
    return logic_.music_track();
  }
  [[nodiscard]] std::vector<std::string> take_sounds() override {
    return logic_.take_sounds();
  }
  [[nodiscard]] bool done() const override {
    return logic_.done();
  }
  [[nodiscard]] std::string next() const override {
    return logic_.next();
  }
  [[nodiscard]] const QuestSave *quest_save() const override {
    return logic_.done() ? &quest() : nullptr;
  }

private:
  void draw_discs(DrawList &list) const;
  void draw_program_label(DrawList &list, float sx, float sy) const;
  void draw_menu(DrawList &list, float offset) const;
  void draw_television(DrawList &list) const;
  void draw_programs(DrawList &list, float offset, bool outgoing) const;
  void draw_stock(DrawList &list, float offset) const;
  void draw_mode(DrawList &list) const;
  void draw_panel(DrawList &list, BattleMusicView view, float offset, bool outgoing) const;
  void draw_track_number(DrawList &list, int track, float left, float top, int layer) const;

  const NumberFont &number_font_;
  BattleMusic logic_;
};

} // namespace phantomcrash
