#include "phantomcrash/slai_content.hpp"
#include <stdexcept>

namespace phantomcrash {
bool legacy_slai(const std::filesystem::path &root) {
  return std::filesystem::is_regular_file(root / "mods/slai/native_parts.json");
}

bool slai_installed(const std::filesystem::path &root) {
  const auto assets = root / "artifacts/slai/assets";
  try {
    const auto receipt = json::read_file(assets / "installed.json");
    if (receipt.number_or("version", 0) != 1 || receipt.string_or("disc", "") != "SLES_529.40") {
      return false;
    }
    for (const char *file : {"tables/parts.json", "tables/garage.json", "robots/catalogue.json",
                             "slai/native_parts.json", "slai/assets/manifest.json"}) {
      if (!std::filesystem::is_regular_file(assets / file)) {
        return false;
      }
    }
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

std::filesystem::path content_profile_path(const std::filesystem::path &base, bool slai,
                                           bool legacy) {
  if (!slai && !legacy) {
    return base;
  }
  auto result = base;
  result.replace_filename(base.stem().wstring() + (slai ? L".slai" : L".base") +
                          base.extension().wstring());
  return result;
}

Settings load_content_settings(const std::filesystem::path &base,
                               const std::filesystem::path &profile, bool slai) {
  const bool existing = std::filesystem::exists(profile);
  auto result = load_settings(existing ? profile : base);
  if (!existing && profile != base) {
    if (result.save_slots_unreadable) {
      throw std::runtime_error("Cannot initialize content profiles from unreadable settings");
    }
    result.quest = {};
    result.save_slots.fill(std::nullopt);
    result.coop_profiles.fill(std::nullopt);
    result.online_profile.reset();
    result.vs_scoobees.clear();
  }
  result.slai_enabled = slai;
  return result;
}
} // namespace phantomcrash
