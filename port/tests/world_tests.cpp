// The 3D world's core: matrices, the training ground's camera and data, and
// reading converted glTF models back into the game's left-handed space.

#include "test_support.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>

#include "phantomcrash/json.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/training_ground.hpp"
#include "phantomcrash/world_model.hpp"

using namespace phantomcrash;

namespace {

bool near(const float a, const float b, const float tolerance = 1e-4F) {
  return std::fabs(a - b) <= tolerance;
}

bool near(const Vec3 a, const Vec3 b, const float tolerance = 1e-4F) {
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// One triangle (0,0,0) (1,0,1) (0,1,0) under a node moved 5 along +Z, with
// an additive material (the original's batch flag 0x200) drawn in pass 1.
constexpr const char *triangle_gltf = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "mesh0_node0", "mesh": 0, "translation": [0, 0, 5]}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0,
                              "extras": {"phantomcrash": {"pass": 1}}}]}],
  "materials": [{"doubleSided": true, "alphaMode": "BLEND",
                 "pbrMetallicRoughness": {"baseColorFactor": [1, 0.5, 0.25, 1]},
                 "extras": {"phantomcrash": {"flags": 512}}}],
  "buffers": [{"byteLength": 44, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAAAAgD8AAAAAAAABAAIAAAA="}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36},
                  {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                 "min": [0, 0, 0], "max": [1, 1, 1]},
                {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}]
})";

std::filesystem::path write_temp(const char *name, const char *text) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream(path, std::ios::binary) << text;
  return path;
}

} // namespace

TEST_CASE("matrices use row vectors: translation comes last") {
  const Mat4 m = scaling({2.0F, 2.0F, 2.0F}) * translation({1.0F, 0.0F, 0.0F});
  CHECK(near(transform_point({1.0F, 1.0F, 1.0F}, m), {3.0F, 2.0F, 2.0F}));
}

TEST_CASE("look_at puts the target straight ahead (+Z) of a left-handed view") {
  const Mat4 view = look_at({0.0F, 0.0F, -10.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F});
  CHECK(near(transform_point({0.0F, 0.0F, 0.0F}, view), {0.0F, 0.0F, 10.0F}));
  CHECK(near(transform_point({1.0F, 0.0F, 0.0F}, view), {1.0F, 0.0F, 10.0F}));
}

TEST_CASE("stages read their model, sky nodes, lighting and fog") {
  const auto stages = parse_stages(json::parse(R"({"stages": {
    "choco_day": {"model": "models/model/choco_mdl.gltf", "sky_nodes": ["mesh1_node0"],
                  "lighting": {"ambient": [0.5, 0.5, 0.5], "sun_direction": [0, -2, 0]},
                  "fog": {"enabled": true, "start": 100, "end": 900}},
    "arena": {}}})"));
  CHECK(stages.size() == 2);
  const StageDefinition &choco = stages.at("choco_day");
  CHECK(choco.model == "models/model/choco_mdl.gltf");
  CHECK(choco.sky_nodes.size() == 1 && choco.sky_nodes[0] == "mesh1_node0");
  CHECK(near(choco.lighting.sun_direction, {0.0F, -1.0F, 0.0F}));
  CHECK(choco.lighting.fog && near(choco.lighting.fog_end, 900.0F));
  CHECK(stages.at("arena").model.empty());
}

TEST_CASE("a stage's screen pass reads its tint and depth of field") {
  const auto stages = parse_stages(json::parse(R"({"stages": {
    "choco_day": {"screen": {"tint_argb": [255, 250, 245, 205],
                             "depth_of_field": {"focus": 70}}},
    "arena": {}}})"));
  const ScreenPass &screen = stages.at("choco_day").lighting.screen;
  CHECK(near(screen.tint, {250.0F / 255.0F, 245.0F / 255.0F, 205.0F / 255.0F}));
  CHECK(screen.depth_of_field && near(screen.focus, 70.0F));
  CHECK(near(screen.focus * screen.focus_scale + screen.focus_base, 0.985F));
  CHECK(screen.active());
  // Left out: white and sharp, so the pass is skipped.
  CHECK(!stages.at("arena").lighting.screen.active());
}

TEST_CASE("a stage gives the battle's scenes its own gates and door model") {
  const auto stages = parse_stages(json::parse(R"({"stages": {
    "choco_night": {"scene_stage": 7, "door_model": "night_doors.gltf",
                    "props_model": "night_props.gltf"}, "arena": {}}})"));
  CHECK(stages.at("choco_night").props_model == "night_props.gltf");
  CHECK(stages.at("arena").props_model.empty());
  SceneTuning scenes;
  scenes.doors.model = "day_doors.gltf";
  apply_stage(scenes, stages.at("arena"));
  CHECK(scenes.stage == 6);
  CHECK(scenes.doors.model == "day_doors.gltf");
  apply_stage(scenes, stages.at("choco_night"));
  CHECK(scenes.stage == 7);
  CHECK(scenes.doors.model == "night_doors.gltf");
}

TEST_CASE("the training script names its lessons and battle") {
  const auto script = parse_training(json::parse(R"({"stage": "choco_day",
    "music": "tutorial_battle",
    "lessons": {"training_basic": {"title": "Basic Controls", "mode": 0}},
    "battle": {"chase_camera": {"behind": 12},
               "courses": {"0": [{"state": 24}, {"state": 34, "end": true}]}}})"));
  CHECK(script.return_to == "pepper_guide");
  CHECK(script.lessons.at("training_basic").mode == 0);
  CHECK(near(script.battle.camera.behind, 12.0F));
  CHECK(script.battle.courses.at(0).size() == 2);
  CHECK(script.battle.buttons.exit == button::back);
  CHECK_THROWS(parse_training(json::parse(R"({"stage": "x", "lessons": {},
    "battle": {"courses": {"0": [{"goal": "swim"}]}}})")));
}

TEST_CASE("image URIs resolve against the model's folder") {
  CHECK(resolve_asset_uri("models/model/choco_mdl.gltf", "../../textures/choco_tex/3.png") ==
        "textures/choco_tex/3.png");
}

TEST_CASE("a glTF model loads mirrored back into the game's left-handed space") {
  const auto path = write_temp("phantomcrash_world_test.gltf", triangle_gltf);
  ModelData model;
  bool loaded = true;
  try {
    model = load_model(path, "models/test.gltf");
  } catch (...) {
    loaded = false;
  }
  std::filesystem::remove(path);
  CHECK(loaded);
  CHECK(model.vertices.size() == 3 && model.indices.size() == 3);
  CHECK(near(model.vertices[1].position[2], -1.0F));
  CHECK(model.nodes.size() == 1 && model.nodes[0].name == "mesh0_node0");
  CHECK(near(transform_point({0.0F, 0.0F, 0.0F}, model.nodes[0].world), {0.0F, 0.0F, -5.0F}));
  CHECK(model.draws.size() == 1 && model.draws[0].pass == 1);
  const ModelMaterial &material = model.materials.at(0);
  CHECK(material.additive && material.double_sided && material.alpha == AlphaMode::blend);
  CHECK(!material.blend_depth_write);
  CHECK(near(material.base_colour[1], 0.5F));
}

TEST_CASE("a model that cannot be read throws") {
  const auto path = write_temp("phantomcrash_world_bad.gltf", "{\"asset\": ");
  CHECK_THROWS(load_model(path, "models/bad.gltf"));
  std::filesystem::remove(path);
}

TEST_CASE("original blended model batches retain depth writes in their second pass") {
  std::string source = triangle_gltf;
  const std::string flags = "\"flags\": 512";
  source.replace(source.find(flags), flags.size(), "\"flags\": 10");
  const auto path = write_temp("phantomcrash_world_blend_test.gltf", source.c_str());
  ModelData model;
  try {
    model = load_model(path, "models/test.gltf");
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
  std::filesystem::remove(path);
  const ModelMaterial &material = model.materials.at(0);
  CHECK(material.alpha == AlphaMode::blend);
  CHECK(!material.additive);
  CHECK(material.blend_depth_write);
  CHECK(model.draws.at(0).pass == 1);
}

TEST_CASE("ordinary glTF transparency does not inherit original model depth writes") {
  std::string source = triangle_gltf;
  const std::string flags = "\"flags\": 512";
  source.replace(source.find(flags), flags.size(), "\"custom\": 10");
  const auto path = write_temp("phantomcrash_world_generic_blend_test.gltf", source.c_str());
  ModelData model;
  try {
    model = load_model(path, "models/test.gltf");
  } catch (...) {
    std::filesystem::remove(path);
    throw;
  }
  std::filesystem::remove(path);
  const ModelMaterial &material = model.materials.at(0);
  CHECK(material.alpha == AlphaMode::blend);
  CHECK(!material.additive);
  CHECK(!material.blend_depth_write);
}
