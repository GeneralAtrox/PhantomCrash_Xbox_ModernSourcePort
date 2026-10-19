// Parsing of data/ui/sonic_amp.json.

#include "phantomcrash/sonic_amp_script.hpp"

#include "phantomcrash/json_fields.hpp"

#include <stdexcept>

namespace phantomcrash {
namespace {

using json::member;
using json::numbers;
using json::required;

float number_of(const json::JsonValue &value, const char *key) {
  return static_cast<float>(required(value, key).number());
}

std::array<float, 2> point(const json::JsonValue &value, const char *key) {
  return numbers<2>(required(value, key), key);
}

SonicAmpChart parse_chart(const json::JsonValue &value) {
  SonicAmpChart chart;
  chart.seconds = required(value, "seconds").number();
  chart.fade_in = required(value, "fade_in").number();
  chart.fade_out = required(value, "fade_out").number();
  chart.ring = member(value, "ring", parse_sprite);
  chart.static_frames = member(value, "static", parse_sprites);
  chart.static_period = required(value, "static_period").number();
  chart.tint = member(value, "tint", parse_sprite);
  chart.tint_alpha = number_of(value, "tint_alpha");
  chart.scan = member(value, "scan", parse_sprite);
  chart.scan_distance = number_of(value, "scan_distance");
  chart.scan_seconds = required(value, "scan_seconds").number();
  chart.scan_alpha = number_of(value, "scan_alpha");
  chart.jacket_centre = point(value, "jacket_centre");
  chart.jacket_scale = number_of(value, "jacket_scale");
  chart.jacket_layer = static_cast<int>(required(value, "jacket_layer").number());
  chart.segments = member(value, "segments", parse_sprites);
  chart.segment_colour = parse_colour(required(value, "segment_colour").string());
  chart.segment_lit = member(value, "segment_lit", parse_sprite);
  chart.rank_style = member(value, "rank", parse_ticker);
  chart.rank_format = required(value, "rank_format").string();
  chart.artist_style = member(value, "artist", parse_ticker);
  if (chart.seconds <= 0.0 || chart.static_frames.empty() || chart.static_period <= 0.0 ||
      chart.scan_seconds <= 0.0) {
    throw std::runtime_error("the chart needs timings above 0 and its static");
  }
  return chart;
}

SonicAmpGrid parse_grid(const json::JsonValue &value) {
  SonicAmpGrid grid;
  grid.step = point(value, "step");
  grid.shadow = member(value, "shadow", parse_sprite);
  grid.jacket_layer = static_cast<int>(required(value, "jacket_layer").number());
  grid.cursor = member(value, "cursor", parse_sprite);
  grid.cursor_offset = point(value, "cursor_offset");
  grid.sold_out = member(value, "sold_out", parse_sprite);
  grid.band = member(value, "band", parse_sprite);
  grid.band_seconds = required(value, "band_seconds").number();
  const auto &spot = required(value, "spot");
  grid.spot_origin = point(spot, "origin");
  grid.spot_columns = static_cast<int>(required(spot, "columns").number());
  grid.spot_badge = member(spot, "badge", parse_sprite);
  const auto &catalogue = required(value, "catalogue");
  grid.catalogue_origin = point(catalogue, "origin");
  grid.catalogue_columns = static_cast<int>(required(catalogue, "columns").number());
  grid.catalogue_rows = static_cast<int>(required(catalogue, "rows").number());
  for (const auto &pages : required(catalogue, "category_pages").array()) {
    grid.category_pages.push_back(static_cast<int>(pages.number()));
  }
  grid.catalogue_tracks = static_cast<int>(required(catalogue, "tracks").number());
  if (const auto *ids = catalogue.find("credit_ids")) {
    for (const auto &id : ids->array()) {
      grid.catalogue_ids.push_back(static_cast<int>(id.number()));
    }
    grid.catalogue_tracks = static_cast<int>(grid.catalogue_ids.size());
  }
  grid.short_page_offset = number_of(catalogue, "short_page_offset");
  grid.page_box = member(catalogue, "page_box", parse_sprite);
  grid.page_slash = member(catalogue, "page_slash", parse_sprite);
  grid.page_number = member(catalogue, "page_number", parse_number_style);
  grid.total_at = point(catalogue, "total_at");
  grid.page_arrows = member(catalogue, "page_arrows", parse_sprites);
  grid.hints = member(value, "hints", parse_sprites);
  if (grid.spot_columns <= 0 || grid.catalogue_columns <= 0 || grid.catalogue_rows <= 0 ||
      grid.category_pages.empty()) {
    throw std::runtime_error("the grids need columns, rows and pages");
  }
  return grid;
}

} // namespace

SonicAmpScript parse_sonic_amp(const json::JsonValue &document) {
  try {
    SonicAmpScript script;
    parse_town_basics(document, script);
    const auto &buttons = required(document, "buttons");
    script.left = buttons_from_json(required(buttons, "left"));
    script.right = buttons_from_json(required(buttons, "right"));
    script.up = buttons_from_json(required(buttons, "up"));
    script.down = buttons_from_json(required(buttons, "down"));
    script.page_back = buttons_from_json(required(buttons, "page_back"));
    script.page_forward = buttons_from_json(required(buttons, "page_forward"));
    const auto &sounds = required(document, "sounds");
    script.jump_sound = sounds.string_or("jump", "");
    script.cancel_sound = sounds.string_or("cancel", "");
    script.buy_sound = sounds.string_or("buy", "");
    script.leave_sound = sounds.string_or("leave", "");
    script.open_sound = sounds.string_or("open", "");
    script.price = static_cast<std::int64_t>(required(document, "price").number());

    script.help_style = member(document, "help", parse_ticker);
    script.help.text = required(document, "help_text").string();
    script.help.trail = number_of(document, "help_trail");
    script.track_format = required(document, "track_help").string();
    script.track_trail = number_of(document, "track_help_trail");

    const auto &menu = required(document, "menu");
    script.items = member(menu, "items", parse_shop_icons);
    script.arrows = member(menu, "arrows", parse_sprites);
    script.arrow_drop = number_of(menu, "arrow_drop");
    script.arrow_seconds = required(menu, "arrow_seconds").number();
    script.watch = member(menu, "watch", parse_sprite);
    script.chart = member(document, "chart", parse_chart);
    script.grid = member(document, "grid", parse_grid);

    const auto &actions = required(document, "actions");
    script.actions = member(actions, "items", parse_shop_icons);
    script.first_action = static_cast<int>(required(actions, "first").number());
    script.dialog = member(document, "dialog", parse_shop_dialog);
    script.message_seconds = required(document, "message_seconds").number();

    const auto &preview = required(document, "preview");
    script.preview_layers = member(preview, "layers", parse_sprites);
    script.preview_jacket = point(preview, "jacket");
    script.preview_layer = static_cast<int>(required(preview, "layer").number());
    script.preview_name = point(preview, "name");
    script.preview_link = point(preview, "link");
    script.preview_hints = member(preview, "hints", parse_sprites);
    script.preview_min_seconds = required(preview, "min_seconds").number();
    if (script.first_action < 0 || script.first_action >= static_cast<int>(script.actions.size())) {
      throw std::runtime_error("actions: first must name one of the actions");
    }
    return script;
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("sonic_amp.json: ") + error.what());
  }
}

} // namespace phantomcrash
