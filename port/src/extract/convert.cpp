// convert_assets: the extracted disc's files into the open formats the port
// loads (convert.py). Movies and music are already open formats; the port
// reads them from the extracted files, so they are not copied.
#include "audio.hpp"
#include "convert_steps.hpp"
#include "gltf.hpp"
#include "parallel.hpp"
#include "png_write.hpp"
#include "xbox_texture.hpp"
#include "phantomcrash/game_data.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

constexpr const char *manifest_name = "manifest.json";
constexpr int format_version = 1;
constexpr const char *texture_directories[]{"scroll", "texture"};

std::string lower(std::string text) {
  std::ranges::transform(text, text.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

void prepare_output(const fs::path &output, const bool replace) {
  if (fs::exists(output) && fs::directory_iterator(output) != fs::directory_iterator()) {
    if (!replace) {
      throw std::runtime_error("Output directory is not empty: " + python_path(output) +
                               " (use --replace)");
    }
    if (!fs::is_regular_file(output / manifest_name)) {
      throw std::runtime_error("Refusing to replace " + python_path(output) + ": it has no " +
                               manifest_name);
    }
    fs::remove_all(output);
  }
  fs::create_directories(output);
}

struct TexturePack {
  std::string relative; // scroll/logo_tex.bin
  Bytes data;
  std::vector<PackTexture> textures;
};

Json convert_textures(const fs::path &media, const fs::path &output, const Progress &report) {
  std::vector<TexturePack> packs;
  for (const char *directory : texture_directories) {
    for (const auto &source : sorted_files(media / directory, ".bin")) {
      TexturePack pack{std::string(directory) + "/" + utf8_name(source), read_file(source), {}};
      pack.textures = parse_texture_pack(pack.data);
      packs.push_back(std::move(pack));
    }
  }
  std::vector<std::pair<std::size_t, std::size_t>> jobs;
  for (std::size_t p = 0; p < packs.size(); ++p) {
    for (std::size_t t = 0; t < packs[p].textures.size(); ++t) {
      jobs.emplace_back(p, t);
    }
  }
  parallel_for(jobs.size(), [&](const std::size_t job) {
    const TexturePack &pack = packs[jobs[job].first];
    const PackTexture &texture = pack.textures[jobs[job].second];
    const fs::path target = output / "textures" / utf8_path(pack_name(pack.relative)) /
                            (std::to_string(texture.index) + ".png");
    write_png(target, texture.format.width, texture.format.height, texture.rgba(pack.data));
  });
  Json records = array();
  for (const auto &pack : packs) {
    Json textures = array();
    for (const auto &texture : pack.textures) {
      const auto &format = texture.format;
      push(textures, object({{"index", integer(texture.index)},
                             {"format", text(format.name())},
                             {"width", integer(format.width)},
                             {"height", integer(format.height)},
                             {"levels", integer(format.levels)}}));
    }
    push(records, object({{"pack", text(pack.relative)}, {"textures", textures}}));
    report(std::format("{:>3} textures  {}", pack.textures.size(), pack.relative));
  }
  return records;
}

std::size_t count_png(const fs::path &directory) {
  std::size_t count = 0;
  if (fs::is_directory(directory)) {
    for (const auto &entry : fs::directory_iterator(directory)) {
      count += lower(entry.path().extension().string()) == ".png" ? 1 : 0;
    }
  }
  return count;
}

// Standalone models (Media/model) to glTF. Binlink models (robots, city
// stages) wait for their texture banks to be converted.
Json convert_models(const fs::path &media, const fs::path &output, const GameTables &tables,
                    const Progress &report) {
  std::map<std::uint32_t, const Resource *> packs;
  for (const auto &resource : tables.resources) {
    packs.emplace(resource.id, &resource);
  }
  std::vector<const Resource *> models;
  for (const auto &resource : tables.resources) {
    if (resource.type == resource_model && resource.path &&
        fs::is_regular_file(media / utf8_path(*resource.path))) {
      models.push_back(&resource);
    }
  }
  std::vector<GltfSummary> summaries(models.size());
  parallel_for(models.size(), [&](const std::size_t index) {
    const Resource &resource = *models[index];
    const std::string &path = *resource.path;
    std::vector<std::string> uris;
    const auto pack =
        resource.texture_resource ? packs.find(*resource.texture_resource) : packs.end();
    if (pack != packs.end() && pack->second->path) {
      const std::string textures = "textures/" + pack_name(*pack->second->path);
      const std::size_t count = count_png(output / utf8_path(textures));
      // One "../" per part of the model's path, back to the output root.
      std::string up;
      for (const char c : path + "/") {
        up += c == '/' ? "../" : "";
      }
      for (std::size_t texture = 0; texture < count; ++texture) {
        uris.push_back(up + textures + "/" + std::to_string(texture) + ".png");
      }
    }
    const Bytes data = read_file(media / utf8_path(path));
    summaries[index] = write_gltf(parse_model(data),
                                  output / "models" / utf8_path(pack_name(path) + ".gltf"), uris);
  });
  Json records = array();
  for (std::size_t index = 0; index < models.size(); ++index) {
    const auto &summary = summaries[index];
    push(records, object({{"model", text(*models[index]->path)},
                          {"resource", integer(models[index]->id)},
                          {"meshes", integer(summary.meshes)},
                          {"vertices", integer(summary.vertices)},
                          {"triangles", integer(summary.triangles)},
                          {"materials", integer(summary.materials)}}));
    report(std::format("{:>3} meshes    {}", summary.meshes, *models[index]->path));
  }
  return records;
}

Json convert_sound_banks(const fs::path &media, const fs::path &output, const Progress &report) {
  struct Bank {
    std::string name, stem;
    Bytes data;
    std::vector<BankEntry> entries;
  };
  std::vector<Bank> banks;
  for (const auto &source : sorted_files(media / "sound", ".bin")) {
    Bytes data = read_file(source);
    if (!is_sound_bank(data)) {
      continue;
    }
    Bank bank{utf8_name(source), utf8_name(source.stem()), std::move(data), {}};
    bank.entries = parse_bank(bank.data);
    banks.push_back(std::move(bank));
  }
  std::vector<std::pair<std::size_t, std::size_t>> jobs;
  for (std::size_t b = 0; b < banks.size(); ++b) {
    for (std::size_t e = 0; e < banks[b].entries.size(); ++e) {
      jobs.emplace_back(b, e);
    }
  }
  const auto wav_name = [](const Bank &bank, const BankEntry &entry) {
    return std::format("sound/sfx/{}/{:02d}.wav", bank.stem, entry.index);
  };
  parallel_for(jobs.size(), [&](const std::size_t job) {
    const Bank &bank = banks[jobs[job].first];
    const BankEntry &entry = bank.entries[jobs[job].second];
    write_pcm16_wav(output / utf8_path(wav_name(bank, entry)), entry.wave);
  });
  Json records = array();
  Json voices = object();
  for (const auto &bank : banks) {
    Json effects = array();
    for (const auto &entry : bank.entries) {
      put(voices, wav_name(bank, entry), object({{"voices", integer(entry.voices())}}));
      const Wave &wave = entry.wave;
      push(effects, object({{"index", integer(entry.index)},
                            {"flags", integer(entry.flags)},
                            {"slot", integer(entry.slot)},
                            {"parameters", integer(entry.parameters)},
                            {"source_format", integer(wave.format_tag)},
                            {"channels", integer(wave.channels)},
                            {"sample_rate", integer(wave.sample_rate)}}));
    }
    push(records, object({{"bank", text("sound/" + bank.name)}, {"effects", effects}}));
    report(std::format("{:>3} effects   sound/{}", bank.entries.size(), bank.name));
  }
  write_json(output / "tables" / "sound_effects.json", object({{"effects", voices}}), 1);
  return records;
}

} // namespace

std::string utf8_name(const fs::path &path) {
  const std::u8string name = path.filename().u8string();
  return {reinterpret_cast<const char *>(name.data()), name.size()};
}

std::vector<fs::path> sorted_files(const fs::path &directory, const std::string_view suffix) {
  std::vector<std::pair<std::string, fs::path>> found;
  if (fs::is_directory(directory)) {
    for (const auto &entry : fs::directory_iterator(directory)) {
      std::string name = lower(utf8_name(entry.path()));
      if (entry.is_regular_file() && name.ends_with(lower(std::string(suffix)))) {
        found.emplace_back(std::move(name), entry.path());
      }
    }
  }
  std::ranges::sort(found);
  std::vector<fs::path> out;
  for (auto &item : found) {
    out.push_back(std::move(item.second));
  }
  return out;
}

Json convert_assets(const fs::path &files, const fs::path &output, const bool replace,
                    const std::vector<fs::path> &text_sources, const Progress &progress) {
  const Progress report = progress ? progress : [](const std::string &) {};
  const fs::path media = files / "Media";
  const fs::path executable_path = files / "default.xbe";
  if (!fs::is_directory(media) || !fs::is_regular_file(executable_path)) {
    throw std::runtime_error(python_path(files) +
                             " does not look like an extracted disc (Media, default.xbe)");
  }
  prepare_output(output, replace);
  Json manifest = object({{"format_version", integer(format_version)},
                          {"converter_version", integer(converted_assets_version)},
                          {"source", text(python_path(files))},
                          {"complete", boolean(false)}});
  // Written first so an interrupted run can still be replaced.
  write_json(output / manifest_name, manifest, 1);
  const Bytes executable = read_file(executable_path);
  const GameTables tables = load_game_tables(executable);
  const XbeImage image(executable);
  put(manifest, "tables", write_tables(image, tables, output));
  put(member(manifest, "tables"), "text", write_game_text(image, text_sources, output));
  put(manifest, "texture_packs", convert_textures(media, output, report));
  put(manifest, "models", convert_models(media, output, tables, report));
  put(manifest, "robots", convert_robots(media, executable, tables, output, report));
  put(manifest, "sound_banks", convert_sound_banks(media, output, report));
  put(manifest, "complete", boolean(true));
  write_json(output / manifest_name, manifest, 1);
  return manifest;
}

} // namespace phantomcrash::extract
