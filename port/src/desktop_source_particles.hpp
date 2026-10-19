#pragma once
#include "desktop_world_renderer.hpp"
#include "phantomcrash/battle.hpp"
#include "phantomcrash/source_particles.hpp"
#include "phantomcrash/source_impact_attachment.hpp"
#include "desktop_source_weapon_actions.hpp"
#include "desktop_source_model_actions.hpp"
#include "desktop_source_projectiles.hpp"
#include <map>
#include <memory>

namespace phantomcrash {
class DesktopSourceParticles final {
public:
  static std::unique_ptr<DesktopSourceParticles> load(WorldRenderer &world, const DataRoots &roots);
  DesktopSourceParticles(float hz, float scale) : system_(hz, scale) {}
  ~DesktopSourceParticles();
  bool impact(const BattleEvent &event, const Battle &battle, const BattleView *view);
  void observe(const Battle &battle, const BattleView *view);
  void observe_actions(const Battle &battle, const BattleView &view, float seconds) {
    if (model_actions_) {
      model_actions_->observe(battle, view, seconds);
    }
    if (actions_) {
      actions_->observe(battle, view, seconds);
    }
  }
  void tick(float seconds) {
    system_.tick(seconds);
  }
  void add_sprites(std::vector<WorldSprite> &sprites) const;
  bool add_projectile(const Projectile &shot, std::vector<WorldItem> &items) const {
    return projectiles_ && projectiles_->add(shot, items);
  }
  bool has_projectile(const WeaponTuning &weapon) const {
    return projectiles_ && projectiles_->contains(weapon);
  }

private:
  struct Rule {
    int model{}, particle{};
    bool shoulder{};
    unsigned target_flags{};
  };
  struct Held {
    int owner{};
    const WeaponTuning *weapon{};
  };
  struct Attachment {
    int target{};
    Vec3 offset;
    unsigned flags{};
  };
  SourceParticles system_;
  std::map<int, SourceParticleDefinition> definitions_;
  std::map<std::string, int> textures_;
  std::vector<Rule> rules_;
  std::map<std::uint64_t, Held> held_;
  std::map<std::uint64_t, Attachment> attachments_;
  std::uint64_t next_key_{0x51A2'0000'0000};
  std::uint64_t model_impact_starts_{}, missing_target_frames_{};
  std::unique_ptr<DesktopSourceWeaponActions> actions_;
  std::unique_ptr<DesktopSourceModelActions> model_actions_;
  std::unique_ptr<DesktopSourceProjectiles> projectiles_;
};
} // namespace phantomcrash
