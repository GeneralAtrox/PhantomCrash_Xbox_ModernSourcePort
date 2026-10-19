// Writes the effect bank with its atlas and models (effect_bank.py), the
// entry shield (effect_convert.py) and the arena's advertising ship
// (arena_monitor.py).
#include "model.hpp"
#include "parallel.hpp"
#include "png_write.hpp"
#include "robot_assets.hpp"
#include "xbox_texture.hpp"

#include <format>

namespace phantomcrash::extract {
namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t atlas_resource = 1589; // texture slot 400, the particles
constexpr std::uint32_t glow_resource = 1574;  // texture slot 0x13f, the pickups' glow

constexpr std::uint32_t shield_resource = 197;
constexpr std::size_t shield_mesh = 30; // object slot 0x41b, the model's first 0x3fd plus 30
constexpr const char *shield_texture = "systemdata/muteki01.tga";
constexpr double shield_repeat = 2.0; // the texture's copies across the sphere
constexpr std::uint32_t white = 0xFFFFFFFF;
constexpr std::size_t tga_header = 18;
constexpr std::uint8_t tga_true_colour = 2;
constexpr std::uint8_t tga_top_left = 0x20; // rows run top to bottom

constexpr std::uint32_t ship_routes = 0x21C768;    // per stage: Bezier points
constexpr std::uint32_t ship_last_segment = 0x21C788;
constexpr std::uint32_t ship_segment_speed = 0x1B59FC; // per 30 Hz tick
constexpr std::uint32_t ship_hinge_speed = 0x1B59F8;
constexpr std::uint32_t ship_spin_speed = 0x1B59E4;
constexpr std::uint32_t ship_hinge_max = 0x19F8DC;
constexpr double ship_tick_rate = 30;

// A texture pack's PNGs as binlink/<id>.png, <id>_<index>.png.
int write_pack(BinlinkPacks &packs, std::uint32_t resource, const fs::path &output) {
  const auto data = packs.read(resource);
  if (!data) {
    return 0;
  }
  return static_cast<int>(
      write_pack_textures(*data, [&](int index) {
        return output / "textures" / "binlink" / binlink_texture_name(resource, index);
      }).size());
}

// A pack whose textures fill slots: binlink/<id>/<index>.png.
int write_slot_pack(BinlinkPacks &packs, std::uint32_t resource, const fs::path &output) {
  const auto data = packs.read(resource);
  if (!data) {
    return 0;
  }
  return static_cast<int>(
      write_pack_textures(*data, [&](int index) {
        return output / "textures" / "binlink" / std::to_string(resource) / (std::to_string(index) + ".png");
      }).size());
}

struct Tga {
  int width{}, height{};
  Bytes rgba;
};

// An uncompressed 32-bit TGA as RGBA rows from the top.
Tga read_tga(ByteView data) {
  if (data.size() < tga_header) {
    throw FormatError("Truncated TGA image");
  }
  const std::uint8_t id_length = data[0];
  const std::uint8_t colour_map = data[1];
  const std::uint8_t kind = data[2];
  const int width = u16(data, 12);
  const int height = u16(data, 14);
  const std::uint8_t depth = data[16];
  const std::uint8_t descriptor = data[17];
  if (kind != tga_true_colour || colour_map != 0 || depth != 32) {
    throw FormatError("only uncompressed 32-bit TGA images are read");
  }
  const std::size_t start = tga_header + id_length;
  const std::size_t row = static_cast<std::size_t>(width) * 4;
  if (start + row * static_cast<std::size_t>(height) > data.size()) {
    throw FormatError("Truncated TGA image");
  }
  Tga out{width, height, Bytes(row * static_cast<std::size_t>(height))};
  for (int y = 0; y < height; ++y) {
    const int source = (descriptor & tga_top_left) != 0 ? y : height - 1 - y;
    const std::uint8_t *line = data.data() + start + static_cast<std::size_t>(source) * row;
    std::uint8_t *target = out.rgba.data() + static_cast<std::size_t>(y) * row;
    for (std::size_t x = 0; x < row; x += 4) {
      target[x] = line[x + 2];
      target[x + 1] = line[x + 1];
      target[x + 2] = line[x];
      target[x + 3] = line[x + 3];
    }
  }
  return out;
}

ModelMesh shield(ModelMesh mesh) {
  for (ModelVertex &vertex : mesh.vertices) {
    vertex.colour = white;
    vertex.uv = {(vertex.position[0] + 0.5) * shield_repeat, (0.5 - vertex.position[1]) * shield_repeat};
  }
  using namespace model_flags;
  for (ModelBatch &batch : mesh.batches) {
    batch.flags |= batch_unlit | batch_additive | batch_two_sided;
    batch.stage_flags[0] |= stage_wrap_u | stage_wrap_v | stage_texture_alpha;
    batch.textures = {0, -1, -1, -1};
    batch.diffuse = white;
  }
  return mesh;
}

Json route_points(const XbeImage &image, std::uint32_t address, std::uint32_t count) {
  Json segments = array();
  for (std::uint32_t segment = 0; segment < count; ++segment) {
    Json points = array();
    for (std::uint32_t p = 0; p < 4; ++p) {
      push(points, reals(image.f32s(address + segment * 48 + p * 12, 3)));
    }
    push(segments, std::move(points));
  }
  return segments;
}

Json ship_table(const XbeImage &image) {
  Json routes = object();
  for (std::uint32_t stage = 0; stage < 8; ++stage) {
    put(routes, std::to_string(stage),
        route_points(image, image.u32(ship_routes + stage * 4), image.u32(ship_last_segment + stage * 4) + 1));
  }
  Json models = array();
  for (const bool night : {false, true}) {
    const int shift = night ? 4 : 0;
    const auto meshes = [&](int first) {
      return integers(std::vector<int>{first + shift, first + 1 + shift, first + 2 + shift, first + 3 + shift});
    };
    Json spinners = array();
    if (night) {
      const double pivots[4][3]{
          {3.06, -9.16, 13.33}, {-2.67, -9.16, -3.96}, {3.24, -9.16, -20.97}, {-4.54, -9.16, -38.55}};
      for (int i = 0; i < 4; ++i) {
        push(spinners, object({{"mesh", integer(i)},
                               {"pivot", reals({pivots[i][0], pivots[i][1], pivots[i][2]})}}));
      }
    }
    push(models,
         object({{"model", text(std::format("models/binlink/{}.gltf", night ? 211 : 210))},
                 {"screens", integers(std::vector<int>{4 + shift, 8 + shift})},
                 {"hinges", array({object({{"meshes", meshes(2)}, {"x", real(40.2)}, {"sign", integer(-1)}}),
                                   object({{"meshes", meshes(6)}, {"x", real(-40.2)}, {"sign", integer(1)}})})},
                 {"spinners", spinners}}));
  }
  return object({{"routes", routes},
                 {"models", models},
                 {"segment_per_second", real(image.f32(ship_segment_speed) * ship_tick_rate)},
                 {"hinge_per_second", real(image.f32(ship_hinge_speed) * ship_tick_rate)},
                 {"spin_per_second", real(image.f32(ship_spin_speed) * ship_tick_rate)},
                 {"hinge_max", real(image.f32(ship_hinge_max))},
                 {"open_segments", integers(std::vector<int>{12, 22})},
                 {"close_segments", integers(std::vector<int>{15, 27})}});
}

} // namespace

std::vector<int> write_pack_textures(ByteView data, const std::function<fs::path(int)> &target) {
  const auto textures = parse_texture_pack(data);
  parallel_for(textures.size(), [&](const std::size_t k) {
    const PackTexture &texture = textures[k];
    write_png(target(texture.index), texture.format.width, texture.format.height, texture.rgba(data));
  });
  std::vector<int> indices;
  for (const PackTexture &texture : textures) {
    indices.push_back(texture.index);
  }
  return indices;
}

std::string binlink_texture_name(std::uint32_t resource, int index) {
  return index == 0 ? std::format("{}.png", resource) : std::format("{}_{}.png", resource, index);
}

void put_summary(Json &record, const GltfSummary &summary) {
  put(record, "meshes", integer(summary.meshes));
  put(record, "vertices", integer(summary.vertices));
  put(record, "triangles", integer(summary.triangles));
  put(record, "materials", integer(summary.materials));
}

Json convert_effect_bank(const XbeImage &image, const GameTables &tables, BinlinkPacks &packs,
                         const fs::path &output, const Progress &report) {
  EffectResources resources;
  const Json bank = effect_bank(image, tables, resources);
  write_compact_json(output / "tables" / "effects.json", bank);
  int textures = write_pack(packs, atlas_resource, output) + write_pack(packs, glow_resource, output);
  for (const std::uint32_t pack : resources.packs) {
    textures += write_slot_pack(packs, pack, output);
  }
  std::set<std::uint32_t> written_packs{atlas_resource, glow_resource};
  int written = 0;
  for (const std::uint32_t id : resources.models) {
    const Resource &resource = tables.resources.at(id);
    const auto data = packs.read(id);
    if (!data) {
      continue;
    }
    std::vector<std::string> uris;
    if (const auto pack = resource.texture_resource) {
      if (written_packs.insert(*pack).second) {
        write_pack(packs, *pack, output);
      }
      if (const auto pack_data = packs.read(*pack)) {
        for (const PackTexture &texture : parse_texture_pack(*pack_data)) {
          uris.push_back("../../textures/binlink/" + binlink_texture_name(*pack, texture.index));
        }
      }
    }
    write_gltf(parse_model(*data), output / "models" / "binlink" / std::format("{}.gltf", id), uris);
    ++written;
  }
  const std::size_t definitions = field(bank, "definitions").array().size();
  report(std::format("{:>4} effect emitters, {} effect models", definitions, written));
  return object({{"groups", integer(field(bank, "groups").array().size())},
                 {"scripts", integer(field(bank, "scripts").array().size())},
                 {"definitions", integer(definitions)},
                 {"textures", integer(textures)},
                 {"models", integer(written)}});
}

Json convert_shield(const fs::path &media, BinlinkPacks &packs, const fs::path &output,
                    const Progress &report) {
  const fs::path source = media / shield_texture;
  const auto data = packs.read(shield_resource);
  if (!fs::is_regular_file(source) || !data) {
    return array();
  }
  const Bytes tga = read_file(source);
  const Tga image = read_tga(tga);
  write_png(output / "textures" / "systemdata" / "muteki01.png", image.width, image.height, image.rgba);
  Model parsed = parse_model(*data);
  Model only{parsed.magic, {shield(std::move(parsed.meshes.at(shield_mesh)))}};
  const auto target = output / "models" / "binlink" / std::format("{}_{}.gltf", shield_resource, shield_mesh);
  const GltfSummary summary = write_gltf(only, target, {"../../textures/systemdata/muteki01.png"});
  report("   1 effect model");
  Json record = object({{"resource", integer(shield_resource)}, {"mesh", integer(shield_mesh)}});
  put_summary(record, summary);
  return array({record});
}

Json convert_arena_ship(const XbeImage &image, BinlinkPacks &packs, const fs::path &output) {
  const Json table = ship_table(image);
  for (const auto [resource, texture_id] : {std::pair{210u, 1586u}, std::pair{211u, 1587u}}) {
    const auto data = packs.read(resource);
    const auto textures = packs.read(texture_id);
    if (!data || !textures) {
      throw FormatError(std::format("Missing arena ship resource {}", resource));
    }
    std::vector<std::string> uris;
    for (const int index : write_pack_textures(*textures, [&](int i) {
           return output / "textures" / "binlink" / std::format("{}_{}.png", texture_id, i);
         })) {
      uris.push_back(std::format("../../textures/binlink/{}_{}.png", texture_id, index));
    }
    Model parsed = parse_model(*data);
    // 0x06aa07 overwrites the screens' atlas coordinates to show the framebuffer.
    const Json &screens = field(field(table, "models").array().at(resource - 210), "screens");
    for (const Json &screen : screens.array()) {
      ModelMesh &mesh = parsed.meshes.at(static_cast<std::size_t>(screen.number()));
      if (mesh.vertices.size() != 4) {
        throw FormatError("Ship monitor must be a four-vertex panel");
      }
      const double uv[4][2]{{0.0, 0.0}, {0.0, 1.0}, {1.0, 0.0}, {1.0, 1.0}};
      for (std::size_t v = 0; v < 4; ++v) {
        mesh.vertices[v].uv = {uv[v][0], uv[v][1]};
      }
    }
    write_gltf(parsed, output / "models" / "binlink" / std::format("{}.gltf", resource), uris);
  }
  write_compact_json(output / "tables" / "arena_monitor.json", table);
  return object({{"models", integer(2)}, {"routes", integer(field(table, "routes").object().size())}});
}

} // namespace phantomcrash::extract
