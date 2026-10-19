// The extractor's Python-compatible primitives (expected values from
// CPython 3.12), its SHA-256 and the game data status check.
#include "test_support.hpp"

#include "phantomcrash/game_data.hpp"
#include "py_json.hpp"
#include "sha256.hpp"
#include "text.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace phantomcrash;
using namespace phantomcrash::extract;

namespace {

std::string sha256_hex(std::string_view data, bool hardware) {
  Sha256 hash(hardware);
  hash.update({reinterpret_cast<const std::uint8_t *>(data.data()), data.size()});
  return hash.hex();
}

void write_text(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

} // namespace

TEST_CASE("python_float matches repr") {
  CHECK(python_float(0.1) == "0.1");
  CHECK(python_float(1.0) == "1.0");
  CHECK(python_float(1e16) == "1e+16");
  CHECK(python_float(1e15) == "1000000000000000.0");
  CHECK(python_float(1e-5) == "1e-05");
  CHECK(python_float(0.0001) == "0.0001");
  CHECK(python_float(-0.0) == "-0.0");
  CHECK(python_float(1.0 / 3.0) == "0.3333333333333333");
  CHECK(python_float(123456789.125) == "123456789.125");
  CHECK(python_float(5e-324) == "5e-324");
}

TEST_CASE("python_round rounds the exact value half to even") {
  CHECK(python_round(2.675, 2) == 2.67); // 2.675 is just below 2.675 as a double
  CHECK(python_round(0.5, 0) == 0.0);
  CHECK(python_round(1.5, 0) == 2.0);
  CHECK(python_round(2.5, 0) == 2.0);
  CHECK(python_round(0.125, 2) == 0.12);
  CHECK(python_round(-0.125, 2) == -0.12);
  CHECK(python_round(1.0000049999999999, 5) == 1.0);
  CHECK(python_round(3.14159, 3) == 3.142);
}

TEST_CASE("python_sum is compensated") {
  CHECK(python_sum({0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1, 0.1}) == 1.0);
  CHECK(python_sum({1e100, 1.0, -1e100}) == 1.0);
}

TEST_CASE("cp932 decoding follows Python's codec") {
  CHECK(decode_cp932("\x82\xa0") == "\u3042");
  CHECK(decode_cp932("\x88\x9f") == "\u4e9c");
  CHECK(decode_cp932("\x80") == "\u0080");
  CHECK(decode_cp932("\xa0") == "\uf8f0");
  CHECK(decode_cp932("\xfd") == "\uf8f1");
  CHECK(decode_cp932("A\xff"
                     "B") == "A\uf8f3B");
  CHECK(decode_cp932("\x81\x20", DecodeErrors::replace) == "\ufffd ");
  CHECK(decode_cp932("\x82", DecodeErrors::replace) == "\ufffd");
  CHECK(decode_cp932("A\x85\x40"
                     "B",
                     DecodeErrors::replace) == "A\ufffd@B");
  CHECK_THROWS(decode_cp932("\x81\x20"));
}

TEST_CASE("NFKC and plain text") {
  CHECK(nfkc("\uff21\uff22\uff23\uff11\uff76\uff9e") == "ABC1\u30ac");
  CHECK(plain("\x82\x60\x81\x40") == "A");
  CHECK(strip(" \u3000x\t ") == "x");
}

TEST_CASE("dumps matches json.dumps") {
  Json value = object();
  put(value, "a", array({integer(1), real(2.5), text("\u00e9")}));
  put(value, "b", null());
  put(value, "c", object());
  CHECK(dumps(value, {}) == R"({"a": [1, 2.5, "\u00e9"], "b": null, "c": {}})");
  CHECK(dumps(value, {-1, true, true}) == R"({"a":[1,2.5,"\u00e9"],"b":null,"c":{}})");
  CHECK(dumps(value, {-1, false, false}) ==
        "{\"a\": [1, 2.5, \"\u00e9\"], \"b\": null, \"c\": {}}");
  CHECK(dumps(value, {1, true, false}) ==
        "{\n \"a\": [\n  1,\n  2.5,\n  \"\\u00e9\"\n ],\n \"b\": null,\n \"c\": {}\n}");
  // A dict keeps a replaced key in its place.
  put(value, "a", integer(7));
  CHECK(dumps(value, {-1, true, true}) == R"({"a":7,"b":null,"c":{}})");
}

TEST_CASE("SHA-256 known vectors") {
  for (const bool hardware : {false, true}) {
    CHECK(sha256_hex("", hardware) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc", hardware) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex(std::string(1000000, 'a'), hardware) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  }
}

TEST_CASE("SHA-256 hardware and portable paths agree") {
  if (!sha256_hardware_available()) {
    return;
  }
  std::mt19937 random(12345);
  std::vector<std::uint8_t> data(4096);
  for (auto &byte : data) {
    byte = static_cast<std::uint8_t>(random());
  }
  for (std::size_t length = 0; length <= data.size(); length += 1 + length / 3) {
    // The hardware hash is fed in uneven pieces to cross block boundaries.
    Sha256 portable(false), hardware(true);
    portable.update({data.data(), length});
    for (std::size_t at = 0; at < length;) {
      const std::size_t piece = std::min<std::size_t>(length - at, 1 + random() % 150);
      hardware.update({data.data() + at, piece});
      at += piece;
    }
    CHECK(portable.hex() == hardware.hex());
  }
}

TEST_CASE("game data status") {
  const auto root = std::filesystem::temp_directory_path() / "phantomcrash_game_data_test";
  std::filesystem::remove_all(root);
  const GameDataPaths paths = game_data_paths(root);
  CHECK(!game_data_status(root).files);

  write_text(paths.files / "Media" / "a.bin", "1234");
  write_text(paths.metadata / "disc.json", R"({"files": [{"path": "/Media/a.bin", "size": 4}]})");
  write_text(paths.assets / "manifest.json", R"({"format_version": 1, "complete": true})");
  GameDataStatus status = game_data_status(root);
  CHECK(status.files);
  CHECK(!status.assets); // written by the Python tools: version 1
  CHECK(status.assets_version == 1);

  write_text(paths.assets / "manifest.json",
             "{\"converter_version\": " + std::to_string(converted_assets_version) +
                 ", \"complete\": false}");
  CHECK(!game_data_status(root).assets);
  write_text(paths.assets / "manifest.json",
             "{\"converter_version\": " + std::to_string(converted_assets_version) +
                 ", \"complete\": true}");
  status = game_data_status(root);
  CHECK(status.files && status.assets && status.problem.empty());

  write_text(paths.files / "Media" / "a.bin", "12345");
  status = game_data_status(root);
  CHECK(!status.files && status.assets);
  CHECK(status.problem.find("/Media/a.bin") != std::string::npos);
  std::filesystem::remove_all(root);
}
