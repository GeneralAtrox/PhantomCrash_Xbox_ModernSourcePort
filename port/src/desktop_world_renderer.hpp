#pragma once

#include "desktop_world_gpu.hpp"
#include "desktop_dynamic_lights.hpp"
#include "desktop_sun_shadows.hpp"
#include "desktop_volumetric_lights.hpp"
#include "desktop_world_shaders.hpp"
#include "phantomcrash/math3d.hpp"
#include "phantomcrash/optical_camouflage.hpp"
#include "phantomcrash/training_ground.hpp"

#include <SDL3/SDL.h>

#include <d3d11_1.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace phantomcrash {

class SmaaPass;

// One model placed in the world. Nodes named in `sky_nodes` are drawn first,
// around the camera (the view without its translation) and without depth,
// as the original draws a stage's sky with its own view (layer 1 of the
// stage object list at 0x2212ec).
//
// An animated item (a robot part) gives every node its world matrix in
// `node_worlds` instead, and `paint` (from WorldRenderer::texture) replaces
// texture 0's image wherever the model uses it (keeping each texture's wrap
// modes): the robot's own paint, which the original binds per robot.
struct WorldItem {
  int robot{-1}; // replay subject owner; -1 for scenery and shared effects
  int model{-1};
  Mat4 world;
  std::vector<std::string> sky_nodes;
  std::vector<std::string> shadow_nodes;
  std::vector<int> two_sided_materials; // resolved stage texture overrides
  // Not drawn while dynamic lighting is on: the original's painted light
  // pools, which its spotlights replace.
  std::vector<int> lit_hidden_materials;
  std::vector<SkyMotion> sky_motion;
  double sky_seconds{};
  std::vector<std::string> monitor_nodes;
  std::vector<Mat4> node_worlds;
  int paint{-1};
  int part_paint{-1};
  float alpha{1.0F}; // below 1 the item is drawn see-through
  float flash{};     // 0..1 toward white (a hit)
  int node{-1};      // draws only this node of the model, when set
  // HUD models (the cockpit): a colour in place of the materials' own, a
  // shift of the texture coordinates, a stretch along the nodes' own x and
  // additive blending.
  std::optional<std::array<float, 4>> colour;
  std::array<float, 2> uv_offset{};
  float scale_x{1.0F};
  bool additive{};
  // Optic Camouflage: outside the sphere the item shows the opaque world
  // behind it, bent along its normals (optical_camouflage.hpp). Drawn after
  // the opaque world, which is copied for it.
  std::optional<OpticalSphere> optical;
  // Casts a stencil shadow volume from its nodes (WorldRenderer::
  // prepare_shadow builds it) and is itself never shadowed.
  bool casts_shadow{};
  // Flat shadow decals follow world surfaces and never occlude later draws.
  bool ground_shadow{};
  // Drawn from both sides whatever its materials say (the flat ground
  // shadows, whose quads face down).
  bool double_sided{};
  // Some world draws, including the advertising ship, disable distance fog.
  bool fogged{true};
  // The port's dynamic lights fall on it (when they are on). Off for wide
  // see-through effects, whose every pixel would pay for each light's shadow.
  bool local_light{true};
  // rainbow.xvu phase and vertical UV scroll, recorded with the world frame.
  std::optional<std::array<float, 2>> rainbow;
  // The supply drops' crates (port addition): `paint` replaces whatever
  // texture each material draws, not texture 0's alone; where the texture's
  // alpha is below 1 (lettering) `label_colour` takes the place of `colour`
  // (the texture's alpha then cuts nothing); `sheen` adds a metal glint in
  // the item's colour.
  bool paint_any{};
  std::optional<std::array<float, 3>> label_colour;
  float sheen{};
};

// A picture in the world that faces the camera, or runs along a streak from
// `position` to `end`: the particles and the pickups' glow. `size` is half
// its width, `uv` its corners in `texture` (u0, v0, u1, v1) and `colour`
// straight (not multiplied by its alpha). An additive sprite is added to
// what is behind it; the others are laid over it.
struct WorldSprite {
  int texture{-1};
  Vec3 position;
  Vec3 end;
  bool streak{};
  float size{};
  float rotation{};
  std::array<float, 4> uv{0.0F, 0.0F, 1.0F, 1.0F};
  std::array<float, 4> colour{1.0F, 1.0F, 1.0F, 1.0F};
  bool additive{};
  // Cluster particles retain their source coordinates until each view draws.
  // 1 is a world-space centre; 2 is a child relative to cluster_centre.
  int particle_kind{};
  Vec3 cluster_centre;
  Vec3 surface_normal; // nonzero fixes this sprite to a surface instead of facing the eye
  bool fixed_plane{};
  Vec3 plane_right, plane_up;
  float end_size{-1.0F}; // streak's far radius; negative keeps the same width
  float end_alpha{1.0F}; // multiplier at a streak's far end
  bool transpose_uv{};   // ribbon textures whose transparent cross-section runs along V
  bool opaque{};
  bool subtractive{};
  bool depth_test{true};
  float aspect_ratio{1};
  bool source_particle_orientation{};
  bool velocity_aligned{};
  Vec3 orientation_velocity; // PTK +0xf0, converted axes in source distance units
};

struct WorldFrame {
  int width{}, height{};
  Mat4 view;
  Mat4 projection;
  Vec3 eye;
  StageLighting lighting;
  std::vector<WorldItem> items;
  std::vector<WorldLight> lights;
  VolumetricAir air; // how the lights show in the air (with dynamic lighting)
  // Drawn last, in order, in the eye's space (+z ahead) with the same
  // projection, without fog and over everything: the cockpit (view layer
  // 0xb).
  std::vector<WorldItem> overlay;
  // Drawn in order after the see-through items, depth tested and not
  // written.
  std::vector<WorldSprite> sprites;
  float shadow_length{60.0F}; // how far the shadow volumes reach
  // Cleared see-through and laid over what the frame has drawn so far (a
  // menu's robot over its backdrop); the stage's clear colour and the
  // screen pass are left out.
  bool transparent{};
};

// Draws the 3D world with Direct3D 11 on the device of SDL's renderer, so
// the 2D screens keep drawing through SDL on top. The world renders into its
// own multisampled targets inside its own device-context state (SDL's state
// is swapped back untouched) and resolves into an SDL texture, drawn under
// the frame's 2D layers.
class WorldRenderer final {
public:
  WorldRenderer(SDL_Renderer *renderer, const DataRoots &roots);
  ~WorldRenderer();
  WorldRenderer(const WorldRenderer &) = delete;
  WorldRenderer &operator=(const WorldRenderer &) = delete;

  // False when SDL's renderer is not Direct3D 11 or setup failed (logged).
  [[nodiscard]] bool available() const noexcept {
    return device_ != nullptr;
  }
  // The model at `asset_path`, loaded on first use; -1 when it cannot load.
  [[nodiscard]] int model(const std::string &asset_path);

  // Builds `model`'s shadow volume for items that cast one (at load time,
  // so the first frame with the caster does not wait for it).
  void prepare_shadow(int model);
  // The CPU description of a loaded model, or null.
  [[nodiscard]] const ModelData *model_data(int model) const;
  // The texture at `asset_path`, loaded on first use; -1 when it cannot load.
  [[nodiscard]] int texture(const std::string &asset_path);
  // Renders `frame` and draws it over the whole window.
  void draw(const WorldFrame &frame, const WorldFrame *monitor = nullptr);
  void set_post_process(const PostProcessOptions &options) {
    if (post_process_.smaa != options.smaa) {
      smaa_failed_ = false;
      SDL_Log("SMAA: %s", options.smaa ? "enabled" : "disabled");
    }
    if (post_process_.dynamic_lighting != options.dynamic_lighting) {
      dynamic_failed_ = false;
      SDL_Log("Dynamic lighting: %s", options.dynamic_lighting ? "enabled" : "disabled");
    }
    post_process_ = options;
  }

private:
  struct Draw {
    const GpuModel *model;
    const ModelDraw *draw;
    Mat4 world;
    float distance;
    ID3D11ShaderResourceView *paint;
    float alpha;
    float flash;
    int group; // the see-through item an opaque batch belongs to, or -1
    const WorldItem *item;
    bool sky_node;
    bool blend;
    bool monitor;
    std::array<float, 2> uv_offset{};
  };

  [[nodiscard]] static bool surface_layer(const Draw &draw);
  void create_states();
  void ensure_targets(int width, int height);
  void render_monitor(const WorldFrame &frame);
  void render_lights(const WorldFrame &frame);
  // Draws the sun's shadow map; on failure or when tuned off, the stencil
  // shadows are drawn instead.
  void render_sun_shadows(const WorldFrame &frame);
  std::unique_ptr<SunShadows> sun_shadows_;
  bool sun_failed_{};
  bool sun_shadowed_{}; // this frame's map is ready
  std::unique_ptr<DynamicLightPass> dynamic_lights_;
  bool dynamic_failed_{};
  // Adds the light the air scatters, after the world and before the screen pass.
  void render_air(const WorldFrame &frame);
  std::unique_ptr<VolumetricLightPass> volumetric_;
  bool volumetric_failed_{};
  [[nodiscard]] std::array<float, 4> monitor_uv(const Draw &draw) const;
  void render(const WorldFrame &frame);
  // Appends the batches of `item` (item `index` of the frame) to `out`.
  void item_draws(const WorldItem &item, int index, Vec3 eye, std::vector<Draw> &out) const;
  void render_overlay(const WorldFrame &frame);
  // Copies the world drawn so far for the camouflaged items to bend, and
  // draws them.
  void render_optical(const std::vector<Draw> &draws, const StageLighting &lighting);
  void create_shadow_states();
  // Counts the casters' volumes into the stencil; false when there were none.
  [[nodiscard]] bool render_volumes(const WorldFrame &frame);
  [[nodiscard]] bool render_static_volumes(const WorldFrame &frame);
  // Lays the stage's shadow colour where the count is not 0.
  void cover_shadows(const StageLighting &lighting);
  void create_sprite_states();
  void render_sprites(const WorldFrame &frame);
  void set_frame_constants(const WorldFrame &frame, const Mat4 &view);
  void create_screen_states();
  // The stage's screen pass (ScreenPass) over the world drawn so far, ahead
  // of the cockpit (desktop_world_screen.cpp).
  void render_screen(const WorldFrame &frame);
  // Marks a transparent frame's drawn pixels opaque (desktop_world_screen.cpp).
  void render_coverage();
  void ensure_blur_targets();
  // `depth_prepass` writes only depth, so that a see-through item blends
  // just its nearest surface instead of its inner faces.
  void draw_one(const Draw &draw, bool sky, const StageLighting &lighting,
                bool depth_prepass = false, bool overlay = false);

  PostProcessOptions post_process_;
  std::unique_ptr<SmaaPass> smaa_;
  bool smaa_failed_{};
  SDL_Renderer *renderer_;
  const DataRoots &roots_;
  Com<ID3D11Device1> device_;
  Com<ID3D11DeviceContext1> context_;
  Com<ID3DDeviceContextState> state_;
  WorldShaders shaders_;
  ShadowShaders shadow_shaders_;
  std::unique_ptr<GpuResources> resources_;
  std::vector<std::unique_ptr<GpuModel>> models_;
  std::map<std::string, int> model_ids_;
  std::vector<Com<ID3D11ShaderResourceView>> textures_;
  std::map<std::string, int> texture_ids_;
  Com<ID3D11Buffer> frame_constants_;
  Com<ID3D11Buffer> draw_constants_;
  Com<ID3D11RasterizerState> cull_back_;
  Com<ID3D11RasterizerState> cull_none_;
  Com<ID3D11RasterizerState> layer_cull_back_;
  Com<ID3D11RasterizerState> layer_cull_none_;
  Com<ID3D11RasterizerState> decal_cull_none_; // ground shadows, above the layers
  Com<ID3D11BlendState> opaque_;
  Com<ID3D11BlendState> alpha_;
  Com<ID3D11BlendState> additive_;
  Com<ID3D11BlendState> depth_only_;
  Com<ID3D11DepthStencilState> depth_write_;
  Com<ID3D11DepthStencilState> depth_read_;
  Com<ID3D11DepthStencilState> depth_off_;
  Com<ID3D11RasterizerState> volume_raster_;
  Com<ID3D11DepthStencilState> volume_depth_;  // counts faces behind the stage
  Com<ID3D11DepthStencilState> clear_stencil_; // the casters: depth as usual, count to 0
  Com<ID3D11DepthStencilState> shadowed_;      // where the count is not 0
  Com<ID3D11VertexShader> sprite_vertex_;
  Com<ID3D11PixelShader> sprite_pixel_;
  Com<ID3D11InputLayout> sprite_layout_;
  Com<ID3D11BlendState> premultiplied_;      // ONE, INVSRCALPHA
  Com<ID3D11BlendState> sprite_subtractive_; // destination minus premultiplied source
  Com<ID3D11SamplerState> sprite_sampler_;
  Com<ID3D11Buffer> sprite_vertices_;
  UINT sprite_capacity_{}; // sprites the buffer holds
  // Targets at the window's size.
  int width_{}, height_{};
  UINT samples_{1};
  DXGI_FORMAT format_{DXGI_FORMAT_R8G8B8A8_UNORM};
  Com<ID3D11Texture2D> colour_;
  Com<ID3D11RenderTargetView> colour_view_;
  Com<ID3D11Texture2D> depth_;
  Com<ID3D11DepthStencilView> depth_view_;
  Com<ID3D11Texture2D> scene_; // the copy Optic Camouflage samples
  Com<ID3D11ShaderResourceView> scene_view_;
  Com<ID3D11Texture2D> monitor_texture_;
  Com<ID3D11ShaderResourceView> monitor_view_;
  bool capturing_monitor_{};
  Com<ID3D11SamplerState> scene_sampler_;
  Com<ID3D11ShaderResourceView> depth_read_view_; // the depth, for the screen pass
  // Reference-height 320x240 / 160x120 blur targets (widescreen extends width).
  Com<ID3D11VertexShader> screen_vertex_;
  Com<ID3D11PixelShader> blur_pixel_;
  Com<ID3D11PixelShader> screen_pixel_;    // reads a single-sampled depth
  Com<ID3D11PixelShader> screen_pixel_ms_; // reads sample 0 of a multisampled one
  // A transparent frame's coverage: alpha 1 wherever depth was written.
  Com<ID3D11VertexShader> far_vertex_;
  Com<ID3D11PixelShader> cover_pixel_;
  Com<ID3D11BlendState> alpha_only_;
  Com<ID3D11DepthStencilState> behind_drawn_;
  Com<ID3D11Buffer> screen_constants_;
  std::array<Com<ID3D11Texture2D>, 2> blur_;
  std::array<Com<ID3D11RenderTargetView>, 2> blur_targets_;
  std::array<Com<ID3D11ShaderResourceView>, 2> blur_views_;
  std::array<int, 2> blur_width_{}, blur_height_{};
  Com<ID3D11Texture2D> wave_;
  Com<ID3D11ShaderResourceView> wave_view_;
  SDL_Texture *output_{};
  ID3D11Texture2D *output_texture_{}; // owned by output_
};

} // namespace phantomcrash
