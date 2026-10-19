#include "phantomcrash/online_content.hpp"
#include "phantomcrash/data_roots.hpp"
#include "../extract/sha256.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <cgltf.h>

namespace phantomcrash::online {
namespace {
void number(extract::Sha256 &hash, std::uint64_t value) {
  std::array<std::uint8_t, 8> bytes{};
  for (auto &byte : bytes) {
    byte = static_cast<std::uint8_t>(value);
    value >>= 8;
  }
  hash.update(bytes);
}
void file(extract::Sha256 &hash, const std::string &name, const std::filesystem::path &path) {
  number(hash, name.size());
  hash.update({reinterpret_cast<const std::uint8_t *>(name.data()), name.size()});
  const auto size = std::filesystem::file_size(path);
  number(hash, size);
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Cannot read online compatibility file: " + path.string());
  }
  std::array<std::uint8_t, 64 * 1024> buffer;
  std::uint64_t read = 0;
  while (stream.read(reinterpret_cast<char *>(buffer.data()), buffer.size()) || stream.gcount()) {
    const auto count = static_cast<std::size_t>(stream.gcount());
    hash.update(std::span(buffer).first(count));
    read += count;
  }
  if (!stream.eof() || read != size) {
    throw std::runtime_error("Online compatibility file changed or could not be read: " +
                             path.string());
  }
}

void model_buffers(extract::Sha256 &hash, const std::filesystem::path &path) {
  auto extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (extension != ".gltf" && extension != ".glb") {
    return;
  }
  // Use the renderer's reader and URI decoding. A model may reference buffers
  // outside the scanned directories; hashing its JSON alone misses those bytes.
  cgltf_options options{};
  cgltf_data *raw = nullptr;
  const auto filename = path.string();
  if (cgltf_parse_file(&options, filename.c_str(), &raw) != cgltf_result_success) {
    throw std::runtime_error("Cannot inspect online model dependencies: " + filename);
  }
  std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
  if (cgltf_load_buffers(&options, data.get(), filename.c_str()) != cgltf_result_success) {
    throw std::runtime_error("Cannot read online model buffers: " + filename);
  }
  number(hash, data->buffers_count);
  for (cgltf_size index = 0; index < data->buffers_count; ++index) {
    const auto &buffer = data->buffers[index];
    number(hash, buffer.size);
    hash.update({static_cast<const std::uint8_t *>(buffer.data), buffer.size});
  }
}

void scan(const std::filesystem::path &folder, const std::filesystem::path &root,
          std::map<std::string, std::filesystem::path> &files,
          std::set<std::filesystem::path> &ancestors) {
  const auto target = std::filesystem::canonical(folder);
  if (!ancestors.insert(target).second) {
    throw std::runtime_error("Cycle in online content directories: " + folder.string());
  }
  for (const auto &entry : std::filesystem::directory_iterator(folder)) {
    if (entry.is_directory()) {
      // DataRoots follows directory links/junctions when resolving a mod.
      scan(entry.path(), root, files, ancestors);
    } else if (entry.is_regular_file()) {
      const auto relative = entry.path().lexically_relative(root).generic_u8string();
      files.emplace(std::string(relative.begin(), relative.end()), entry.path());
    }
  }
  ancestors.erase(target);
}
} // namespace

Compatibility content_fingerprint(const std::filesystem::path &executable,
                                  const std::filesystem::path &root, std::optional<bool> slai) {
  extract::Sha256 hash;
  file(hash, "executable", executable);
  number(hash, slai ? (*slai ? 2 : 1) : 0);
  std::map<std::string, std::filesystem::path> files;
  const auto roots = slai ? DataRoots::configured(root, *slai) : DataRoots::standard(root);
  auto folders = roots.asset_roots();
  folders.push_back(root / "data");
  for (const auto &folder : folders) {
    if (!std::filesystem::is_directory(folder)) {
      if (folder == root / "mods" || folder == root / "artifacts/europe/files/Media" ||
          folder == root / "data" / "assets") {
        continue;
      }
      throw std::runtime_error("Online play requires " + folder.string());
    }
    std::set<std::filesystem::path> ancestors;
    scan(folder, root, files, ancestors);
  }
  // The converter's manifest records the absolute folder it converted on this
  // machine. Every file it lists is hashed on its own, so it is left out;
  // otherwise identical installs in different folders could never connect.
  std::erase_if(files, [](const auto &entry) {
    const auto &name = entry.first;
    const std::string suffix = "/assets/manifest.json";
    return name.starts_with("artifacts/") && name.ends_with(suffix) &&
           name.find('/', 10) == name.size() - suffix.size();
  });
  number(hash, files.size());
  for (const auto &[name, path] : files) {
    file(hash, name, path);
    model_buffers(hash, path);
  }
  const auto hex = hash.hex();
  const auto digit = [](char value) { return value <= '9' ? value - '0' : value - 'a' + 10; };
  Compatibility result;
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] =
        static_cast<std::uint8_t>((digit(hex[index * 2]) << 4) | digit(hex[index * 2 + 1]));
  }
  return result;
}
} // namespace phantomcrash::online
