#pragma once

#include "desktop_sv_purchase.hpp"
#include "desktop_town_screen.hpp"
#include "phantomcrash/contests.hpp"
#include "phantomcrash/rumbling.hpp"

#include <memory>
#include <string>
#include <vector>

namespace phantomcrash {

class DrawList;
class CoopMatches;
struct CoopMatch;

// RUMBLING (Rumbling): the area select with the venue's picture and its
// contest, the area's sub-menu over its map, the gates to come in by, the
// contest's details and the notices. `remembered_area` keeps the area the
// sub-menu was on for coming back from SELECT SV.
class RumblingScreen final : public TownScreen {
public:
  void set_coop_matches(std::shared_ptr<CoopMatches> matches) {
    coop_matches_ = std::move(matches);
  }
  [[nodiscard]] std::optional<MenuPresence> menu_presence() const override;
  [[nodiscard]] std::optional<MenuPresence> peer_presence(const MenuPresence &peer) const override;
  RumblingScreen(TextureCache &textures, const GlyphFont &font, Rumbling logic,
                 const ContestTables &contests, const MessageBoxStyle &box, const std::string &from,
                 const FrameStyle &frame, const std::vector<MusicCredit> &credits, QuestSave quest,
                 int &remembered_area);

  void tick(const InputFrame &input, double tick_seconds) override;
  void draw(const Viewport &viewport) override;
  [[nodiscard]] std::vector<std::string> take_sounds() override;
  [[nodiscard]] bool done() const override {
    return logic_.done();
  }
  [[nodiscard]] std::string next() const override {
    return logic_.next();
  }
  [[nodiscard]] const QuestSave *quest_save() const override {
    return logic_.done() ? &logic_.quest() : nullptr;
  }
  [[nodiscard]] const QuestSave *pending_quest() const override {
    return &logic_.quest();
  }
  [[nodiscard]] const RumblingEntry *rumbling_entry() const override {
    return logic_.done() && logic_.entry() ? &*logic_.entry() : nullptr;
  }
  void set_live_rosters(const std::array<AreaRoster, roster_areas> &rosters) override {
    logic_.set_rosters(rosters);
  }

private:
  void draw_items(DrawList &list) const;
  void draw_area(DrawList &list, int area, float alpha) const;
  void draw_info(DrawList &list) const;
  void draw_menu(DrawList &list) const;
  void draw_map(DrawList &list) const;
  void draw_gates(DrawList &list) const;
  // The live RUMBLING (desktop_rumbling_robots.cpp): the opponents out on
  // the map, and the shown one's armour on its card.
  void draw_robots(DrawList &list) const;
  [[nodiscard]] std::shared_ptr<CoopMatch> live_match() const;
  void draw_coop_robots(DrawList &list, const CoopMatch &match) const;
  void draw_armour(DrawList &list, int entrant) const;
  [[nodiscard]] const RosterMember *shown_member() const;
  void draw_character_info(DrawList &list) const;
  void draw_contest_details(DrawList &list) const;

  Rumbling logic_;
  const ContestTables &contests_;
  const HudStyle &hud_;
  ShopNotice notice_;
  int &remembered_area_;
  std::shared_ptr<CoopMatches> coop_matches_;
  mutable std::weak_ptr<CoopMatch> logged_match_;
};

} // namespace phantomcrash
