#include "extract.hpp"

#include "sha256.hpp"
#include "xbe.hpp"
#include "xdvdfs.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stop_token>
#include <thread>

namespace phantomcrash::extract {
namespace {

constexpr std::size_t hash_chunk = 8 * 1024 * 1024;

// The file's SHA-256; stops early (returning "") once `stop` is requested.
std::string sha256_file(const std::filesystem::path &path, const std::stop_token &stop = {}) {
  std::ifstream in(path, std::ios::binary);
  Sha256 hash;
  Bytes chunk(hash_chunk);
  while (in) {
    if (stop.stop_requested()) {
      return {};
    }
    in.read(reinterpret_cast<char *>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    hash.update({chunk.data(), static_cast<std::size_t>(in.gcount())});
  }
  return hash.hex();
}

std::filesystem::path safe_destination(const std::filesystem::path &root,
                                       const std::string &disc_path) {
  const auto relative = std::filesystem::path(
      std::u8string(reinterpret_cast<const char8_t *>(disc_path.c_str())).substr(1));
  for (const auto &part : relative) {
    if (part == ".." || part == "." || part.has_root_name()) {
      throw FormatError("Disc path escapes the output directory: " + disc_path);
    }
  }
  return root / relative;
}

bool non_empty_directory(const std::filesystem::path &path) {
  return std::filesystem::exists(path) &&
         std::filesystem::directory_iterator(path) != std::filesystem::directory_iterator();
}

std::string grouped(std::uint64_t value) {
  std::string digits = std::to_string(value);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
    digits.insert(static_cast<std::size_t>(i), ",");
  }
  return digits;
}

} // namespace

std::string python_path(const std::filesystem::path &path) {
  std::string text = path.string();
  for (char &c : text) {
    if (c == '/') {
      c = '\\';
    }
  }
  std::string out;
  std::size_t i = 0;
  // Keep a leading "\\" (UNC) as it is.
  if (text.starts_with("\\\\")) {
    out = "\\\\";
    i = 2;
  }
  std::string part;
  const auto flush = [&] {
    if (part == "." && !out.empty()) {
      part.clear();
      return;
    }
    if (!part.empty()) {
      if (!out.empty() && out.back() != '\\') {
        out += '\\';
      }
      out += part;
      part.clear();
    }
  };
  for (; i < text.size(); ++i) {
    if (text[i] == '\\') {
      const bool drive_root = part.size() == 2 && part[1] == ':' && out.empty();
      flush();
      if (drive_root || (out.empty() && i == 0)) {
        out += '\\';
      }
    } else {
      part += text[i];
    }
  }
  flush();
  return out.empty() ? "." : out;
}

Json extract_disc(const std::filesystem::path &disc, const std::filesystem::path &output,
                  const Progress &progress) {
  if (non_empty_directory(output)) {
    throw std::runtime_error("Output directory is not empty: " + output.string());
  }
  const auto report = [&](const std::string &message) {
    if (progress) {
      progress(message);
    }
  };
  const auto files_root = output / "files";
  Json records = array();
  std::uint64_t total = 0;
  std::string xbe_hash;
  XdvdfsImage image(disc);
  const auto entries = image.files();
  // The whole image hashes on its own thread while the files are copied.
  std::string image_hash;
  std::jthread image_hasher(
      [&image_hash, &disc](const std::stop_token &stop) { image_hash = sha256_file(disc, stop); });
  for (const auto &entry : entries) {
    const auto destination = safe_destination(files_root, entry.path);
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream target(destination, std::ios::binary | std::ios::trunc);
    Sha256 hash;
    image.copy_to(entry, [&](ByteView chunk) {
      hash.update(chunk);
      target.write(reinterpret_cast<const char *>(chunk.data()),
                   static_cast<std::streamsize>(chunk.size()));
    });
    if (!target) {
      throw std::runtime_error("cannot write " + destination.string());
    }
    const std::string digest = hash.hex();
    std::string lowered = entry.path;
    for (char &c : lowered) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lowered == "/default.xbe") {
      xbe_hash = digest;
    }
    push(records, object({{"path", text(entry.path)},
                          {"sector", integer(entry.sector)},
                          {"size", integer(entry.size)},
                          {"sha256", text(digest)}}));
    total += entry.size;
    char size[32];
    std::snprintf(size, sizeof(size), "%12s", grouped(entry.size).c_str());
    report(std::string(size) + "  " + entry.path);
  }

  report("Hashing disc image...");
  image_hasher.join();
  Json disc_record = object({{"image", text(python_path(disc))},
                             {"image_size", integer(std::filesystem::file_size(disc))},
                             {"image_sha256", text(image_hash)},
                             {"partition_offset", integer(image.partition_offset())},
                             {"file_count", integer(entries.size())},
                             {"total_bytes", integer(total)},
                             {"files", records}});
  write_json(output / "metadata" / "disc.json", disc_record, 2);

  const Bytes executable = read_file(files_root / "default.xbe");
  Json header = parse_xbe(executable).to_json();
  put(header, "sha256", text(xbe_hash));
  write_json(output / "metadata" / "xbe.json", header, 2);
  return object({{"disc", disc_record}, {"xbe", header}});
}

} // namespace phantomcrash::extract
