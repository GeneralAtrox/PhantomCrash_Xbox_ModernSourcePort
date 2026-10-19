#include "test_support.hpp"
#include "phantomcrash/glyph_font.hpp"
#include "phantomcrash/data_roots.hpp"
#include "phantomcrash/json_fields.hpp"
#include "phantomcrash/shop_list_style.hpp"

using namespace phantomcrash;

TEST_CASE("shop labels keep short names and bound long names to the available column") {
  GlyphFont font;
  font.glyphs = {{U'Ａ', 0}, {U'．', 1}};
  font.metrics = {{0, 10}, {0, 4}};
  font.characters = {U'Ａ', U'．'};
  const auto short_name = layout_label(font, "AAA", 81, 156, 50);
  REQUIRE(short_name.size() == 3);
  CHECK(short_name.back().x + short_name.back().width == 111);
  const auto long_name = layout_label(font, "AAAAAAAAAA", 81, 156, 50);
  REQUIRE(long_name.size() == 6);
  CHECK(long_name.back().x + long_name.back().width <= 131);
  CHECK(long_name[3].width == 4 && long_name[4].width == 4 && long_name[5].width == 4);
  CHECK(layout_label(font, "AAA", 0, 0, 0).empty());
  CHECK(layout_label(font, "AAA", 0, 0, 5).empty());
}

TEST_CASE("long part names stop before fitted-part and side badges") {
  const auto roots = DataRoots::standard(PHANTOMCRASH_SOURCE_ROOT);
  const auto shop = roots.document("ui/module_shop.json");
  const auto &modes = json::required(shop, "modes");
  const auto buy = parse_goods_list(json::required(json::required(modes, "buy"), "list"));
  auto sell = parse_goods_list(json::required(json::required(modes, "sell"), "list"));
  const auto assemble =
      parse_goods_list(json::required(roots.document("ui/assemble.json"), "list"));
  CHECK(buy.name_at[0] + goods_name_width(buy, 18) == 340);
  CHECK(sell.name_at[0] + goods_name_width(sell, 18) <=
        sell.hangar_badges[0].front().position[0] - 18);
  CHECK(assemble.name_at[0] + goods_name_width(assemble, 18) <=
        assemble.side_marks[0].position[0] - 18);
  sell.name_width = 1000;
  CHECK(sell.name_at[0] + goods_name_width(sell, 18) <=
        sell.hangar_badges[0].front().position[0] - 18);
  sell.name_width = 80;
  CHECK(goods_name_width(sell, 18) == 80);
  sell.name_width = 0;
  sell.hangar_badges[0].front().centered = true;
  CHECK(sell.name_at[0] + goods_name_width(sell, 18) <=
        sell.hangar_badges[0].front().position[0] - 20 - 18);
}
