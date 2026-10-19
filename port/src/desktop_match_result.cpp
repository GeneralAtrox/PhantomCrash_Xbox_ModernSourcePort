#include "desktop_draw_list.hpp"
#include "desktop_match_result.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/money.hpp"

#include <algorithm>
#include <exception>

namespace phantomcrash {
using json::required;
namespace {
std::string money_text(const std::int64_t value) {
  std::string result = std::to_string(shown_ny(value));
  for (int at = static_cast<int>(result.size()) - 3; at > 0; at -= 3) {
    result.insert(static_cast<std::size_t>(at), ",");
  }
  return result;
}
} // namespace

MatchResultScreen::MatchResultScreen(TextureCache &textures, const GlyphFont &font,
                                     const FrameStyle &frame,
                                     const std::vector<MusicCredit> &credits,
                                     const json::JsonValue &data, MatchReceipt receipt,
                                     QuestSave quest, WorldRenderer *world, WorldFrame background,
                                     std::string next, std::shared_ptr<ReplayArchive> replay,
                                     const int first_win, std::shared_ptr<void> resources)
    : resources_(std::move(resources)), textures_(textures), font_(font),
      frame_(font, frame, credits, parse_frame_placement(required(data, "frame"))),
      receipt_(std::move(receipt)), presentation_(receipt_, required(data, "presentation")),
      quest_(std::move(quest)), world_(world), backdrop_(replay, std::move(background)),
      next_(std::move(next)) {
  shootdowns_font_ = parse_number_font(required(data, "shootdowns_font"));
  time_font_ = parse_number_font(required(data, "time_font"));
  shootdowns_number_ = parse_number_style(required(data, "shootdowns_number"));
  time_number_ = parse_number_style(required(data, "time_number"));
  time_colon_ = parse_sprite(required(data, "time_colon"));
  time_colon_advance_ = static_cast<float>(data.number_or("time_colon_advance", 6));
  sprites_ = parse_sprites(required(data, "sprites"));
  prize_label_ = parse_sprite(required(data, "prize"));
  rental_label_ = parse_sprite(required(data, "rental_prize"));
  rental_counting_ = parse_sprite(required(data, "rental_counting"));
  for (const auto &sprite : required(data, "sprites").array()) {
    reveal_rows_.push_back(static_cast<int>(sprite.number_or("reveal_row", -1)));
  }
  if (receipt_.match.outcome == MatchOutcome::retire) {
    verdict_ = parse_sprite(required(data, "retire"));
  } else {
    const auto &tiers = required(required(data, "verdicts"),
                                 receipt_.match.outcome == MatchOutcome::win ? "win" : "leave");
    int best = -1;
    for (const auto &tier : tiers.array()) {
      const int minimum = static_cast<int>(tier.number_or("min_shootdowns", 0));
      if (minimum > best && receipt_.match.shootdowns >= minimum) {
        verdict_ = parse_sprite(required(tier, "sprite"));
        best = minimum;
      }
    }
  }
  const auto &sounds = required(data, "sounds");
  cursor_sound_ = sounds.string_or("cursor", "");
  confirm_sound_ = sounds.string_or("confirm", "");
  cancel_sound_ = sounds.string_or("cancel", "");
  exit_ = parse_sprite(required(data, "exit"));
  replay_ = parse_sprite(required(data, "replay"));
  replay_selected_ = parse_sprite(required(data, "replay_selected"));
  exit_selected_ = parse_sprite(required(data, "exit_selected"));
  replay_data_ = required(data, "replay_settings");
  tape_ = std::move(replay);
  hint_ = parse_ticker(required(data, "hint"));
  hint_run_ = glyph_run(font_, required(data, "hint").string_or("text", ""));
  values_right_ = static_cast<float>(data.number_or("values_right", 312));
  const auto &rows = required(data, "value_rows").array();
  for (std::size_t row = 0; row < rows_.size(); ++row) {
    rows_[row] = static_cast<float>(rows.at(row).number());
  }
  counting_hint_run_ = glyph_run(font_, data.string_or("counting_hint", ""));
  const auto &music = required(data, "music");
  music_ =
      music.string_or(receipt_.match.outcome == MatchOutcome::retire ? "retire" : "survive", "");
  frame_.preload(textures_);
  textures_.preload(data);
  if (first_win >= 0 && world_ != nullptr && world_->available() && data.find("trophy")) {
    try {
      TrophyStyle style = parse_trophy_style(*data.find("trophy"));
      if (const int award = trophy_award(style, first_win); award >= 0) {
        trophy_ = std::make_unique<ResultTrophy>(*world_, std::move(style), award);
        SDL_Log("Result trophy: contest %d, award %d", first_win, award);
      }
    } catch (const std::exception &error) {
      SDL_Log("match_result.json: %s", error.what());
    }
  }
  SDL_Log("Result presentation: verdict=%s, sequential tally, replay=%d",
          verdict_ ? verdict_->id.c_str() : "none", replay_available());
}

void MatchResultScreen::tick(const InputFrame &input, const double seconds) {
  if (watching_) {
    watching_->tick(input, seconds);
    auto cues = watching_->take_sound_cues();
    pending_sounds_.insert(pending_sounds_.end(), cues.begin(), cues.end());
    if (watching_->done()) {
      watching_.reset();
      SDL_Log("Replay stopped: returned to RESULT");
    }
    return;
  }
  seconds_ += seconds;
  backdrop_.tick(seconds);
  frame_.update(seconds);
  const int old_row = presentation_.counting_row();
  const bool was_complete = presentation_.complete();
  if (trophy_) {
    trophy_->tick(seconds);
  }
  if (!trophy_ || !trophy_->holding()) {
    presentation_.update(seconds);
  }
  if (presentation_.counting_row() != old_row || presentation_.complete() != was_complete) {
    SDL_Log("Result tally: row=%d complete=%d money=%lld", presentation_.counting_row(),
            presentation_.complete(), static_cast<long long>(presentation_.values()[8]));
  }
  const int before = selected_;
  if (input.any_pressed(button::left) && replay_available()) {
    selected_ = 0;
  }
  if (input.any_pressed(button::right | button::cancel)) {
    selected_ = 1;
  }
  bool clicked = false;
  if (input.pointer) {
    const auto [x, y] = *input.pointer;
    // Use the rendered button geometry, including modded positions/scales.
    const auto hit = [&](const Sprite &sprite) {
      return std::ranges::any_of(place_sprite(sprite, seconds_), [&](const Quad &quad) {
        return x >= quad.x && y >= quad.y && x < quad.x + quad.width && y < quad.y + quad.height;
      });
    };
    if (hit(exit_) || (replay_available() && hit(replay_))) {
      if (input.pointer_moved || input.clicked) {
        selected_ = hit(exit_) ? 1 : 0;
      }
      clicked = input.clicked;
    }
  }
  if (before != selected_) {
    sound(cursor_sound_);
  }
  if (input.any_pressed(button::confirm | button::cancel) || clicked) {
    if (!presentation_.complete() || !was_complete) {
      presentation_.finish();
      sound(confirm_sound_);
      SDL_Log("Result tally skipped: money=%lld",
              static_cast<long long>(presentation_.values()[8]));
      return; // One press only completes the receipt; it cannot also leave it.
    }
    if (selected_ == 0 && replay_available()) {
      sound(confirm_sound_);
      watching_ = std::make_unique<ReplayScreen>(textures_, font_, world_, tape_, replay_data_);
    } else if (selected_ == 1) {
      sound(input.any_pressed(button::cancel) ? cancel_sound_ : confirm_sound_);
      done_ = true;
    }
  }
}

bool MatchResultScreen::replay_available() const {
  return tape_ && tape_->size() > 1;
}

void MatchResultScreen::sound(const std::string &name) {
  if (!name.empty()) {
    pending_sounds_.push_back({SoundCue::Action::play, name});
  }
}

std::vector<SoundCue> MatchResultScreen::take_sound_cues() {
  return std::exchange(pending_sounds_, {});
}

void MatchResultScreen::text(DrawList &list, const std::string &value, const float x, const float y,
                             const std::uint32_t colour, const float scale, const bool right,
                             const float alpha) const {
  const auto run = glyph_run(font_, value);
  auto quads = layout_run(font_, run, 0, 0);
  const float left = right ? x - run_width(font_, run) * scale : x;
  for (auto &quad : quads) {
    quad.x = left + quad.x * scale;
    quad.y = y + quad.y * scale;
    quad.width *= scale;
    quad.height *= scale;
  }
  list.text(264, std::move(quads), colour, std::nullopt, alpha);
}

void MatchResultScreen::draw_counters(DrawList &list) const {
  for (const auto &digit :
       number_sprites(shootdowns_font_, shootdowns_number_, receipt_.match.shootdowns, 260)) {
    list.sprite(digit);
  }
  const auto total =
      static_cast<std::int64_t>(std::clamp(receipt_.match.play_seconds, 0.0, 359999.0));
  const std::array<std::int64_t, 3> fields{total % 60, total / 60 % 60, total / 3600};
  NumberStyle style = time_number_;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    float left = style.right[0];
    for (const auto &digit : number_sprites(time_font_, style, fields[i], 260)) {
      list.sprite(digit);
      left = std::min(left, digit.position[0]);
    }
    if (i + 1 < fields.size()) {
      Sprite colon = time_colon_;
      colon.position = {left - time_colon_advance_, style.right[1]};
      list.sprite(colon);
      left -= time_colon_advance_;
    }
    style.right[0] = left;
  }
}

void MatchResultScreen::draw(const Viewport &viewport) {
  if (watching_) {
    watching_->draw(viewport);
    return;
  }
  clear_black(textures_);
  if (world_ && world_->available()) {
    backdrop_.draw(*world_, viewport);
  }
  DrawList list(textures_, font_, viewport, seconds_);
  if (trophy_) {
    trophy_->draw(list, viewport);
  }
  for (std::size_t i = 0; i < sprites_.size(); ++i) {
    const float alpha = reveal_rows_[i] < 0 ? 1.0F : presentation_.row_alpha(reveal_rows_[i]);
    list.sprite(sprites_[i], alpha);
  }
  list.sprite(prize_label_, presentation_.row_alpha(2));
  if (receipt_.match.rental) {
    list.sprite(presentation_.rental_discount_label() ? rental_label_ : rental_counting_,
                presentation_.row_alpha(2));
  }
  const auto &values = presentation_.values();
  constexpr std::array<int, 9> reveal_rows{1, 2, 4, 5, 6, 7, 8, 9, 10};
  for (std::size_t row = 0; row < values.size(); ++row) {
    const bool repair = row >= 2 && row <= 7;
    const auto amount = values[row];
    const auto colour = amount && repair ? 0xff0000U : row == 1 ? 0x86ff00U : 0xffffffU;
    const std::string sign = amount && repair ? "- " : row == 1 ? "+ " : "";
    const auto value = sign + money_text(amount);
    const float scale = std::min(0.9F, 100.0F / run_width(font_, glyph_run(font_, value)));
    text(list, value, values_right_, rows_[row], colour, scale, true,
         presentation_.row_alpha(reveal_rows[row]));
  }
  if (verdict_) {
    list.sprite(*verdict_, presentation_.row_alpha(10));
  }
  draw_counters(list);
  list.sprite(selected_ == 0 ? replay_selected_ : replay_,
              replay_available() && presentation_.complete() ? 1.0F : 0.45F);
  list.sprite(selected_ == 1 ? exit_selected_ : exit_, presentation_.complete() ? 1.0F : 0.45F);
  list.ticker(hint_, presentation_.complete() ? hint_run_ : counting_hint_run_, seconds_);
  QuestSave displayed = quest_;
  displayed.money = values[8];
  frame_.draw(list, displayed);
  list.flush();
}

} // namespace phantomcrash
