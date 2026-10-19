#include "desktop_world_renderer.hpp"
#include "desktop_world_constants.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace phantomcrash {
namespace {

using namespace world_constants;

// Below half of one 8-bit alpha step: only fully transparent texels fail.
constexpr float invisible_alpha = 0.5F / 255.0F;

bool blended(const ModelMaterial &material) {
  return material.alpha == AlphaMode::blend || material.additive;
}

} // namespace

void WorldRenderer::set_frame_constants(const WorldFrame &frame, const Mat4 &view) {
  const StageLighting &light = frame.lighting;
  FrameConstants constants{};
  constants.view_projection = view * frame.projection;
  constants.eye = frame.eye;
  constants.fog_start = light.fog_start;
  constants.ambient = light.ambient;
  constants.fog_end = light.fog_end;
  constants.sun_direction = light.sun_direction;
  constants.fog_enabled = light.fog ? 1.0F : 0.0F;
  constants.sun_colour = light.sun_colour;
  constants.shadow_length = frame.shadow_length;
  constants.fog_colour = light.fog_colour;
  upload(context_.Get(), frame_constants_.Get(), constants);
}

// Original second-pass surface layers share vertices/planes with the base
// mesh (dirt on pavement, choco's helipad): blended, yet writing depth.
bool WorldRenderer::surface_layer(const Draw &draw) {
  const ModelMaterial &material =
      draw.model->data.materials[static_cast<std::size_t>(draw.draw->material)];
  return !draw.sky_node && !draw.item->optical && !draw.item->additive && draw.alpha == 1.0F &&
         draw.draw->pass == 1 && material.alpha == AlphaMode::blend && material.original_material &&
         material.blend_depth_write;
}

void WorldRenderer::render(const WorldFrame &frame) {
  if (post_process_.dynamic_lighting && dynamic_lights_ && !dynamic_failed_) {
    dynamic_lights_->bind(context_.Get());
  }
  if (sun_shadowed_) {
    sun_shadows_->bind(context_.Get());
  } else {
    SunShadows::unbind(context_.Get());
  }
  const D3D11_VIEWPORT viewport{
      0.0F, 0.0F, static_cast<float>(frame.width), static_cast<float>(frame.height), 0.0F, 1.0F};
  context_->RSSetViewports(1, &viewport);
  ID3D11RenderTargetView *target = colour_view_.Get();
  context_->OMSetRenderTargets(1, &target, depth_view_.Get());
  const Vec3 clear = frame.transparent ? Vec3{} : frame.lighting.clear_colour;
  const float clear_colour[4] = {clear.x, clear.y, clear.z, frame.transparent ? 0.0F : 1.0F};
  context_->ClearRenderTargetView(colour_view_.Get(), clear_colour);
  context_->ClearDepthStencilView(depth_view_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0F,
                                  0);
  context_->IASetInputLayout(shaders_.layout.Get());
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(shaders_.vertex.Get(), nullptr, 0);
  context_->PSSetShader(shaders_.pixel.Get(), nullptr, 0);
  ID3D11Buffer *constants[] = {frame_constants_.Get(), draw_constants_.Get()};
  context_->VSSetConstantBuffers(0, 2, constants);
  context_->PSSetConstantBuffers(0, 2, constants);

  std::vector<Draw> draws;
  for (std::size_t item_index = 0; item_index < frame.items.size(); ++item_index) {
    item_draws(frame.items[item_index], static_cast<int>(item_index), frame.eye, draws);
  }
  std::vector<Draw> sky;
  std::vector<Draw> opaque;
  std::vector<Draw> casting; // opaque batches of shadow casters, drawn after the volumes
  std::vector<Draw> layers;  // ground markings, which the shadows fall on
  std::vector<Draw> translucent;
  std::vector<Draw> ground_shadows;
  std::vector<Draw> optical;
  for (const Draw &entry : draws) {
    if (entry.sky_node) {
      sky.push_back(entry);
    } else if (entry.item->ground_shadow) {
      if (!sun_shadowed_) { // with sun shadows, robots cast into the map instead
        ground_shadows.push_back(entry);
      }
    } else if (entry.item->optical) {
      optical.push_back(entry);
    } else if (surface_layer(entry)) {
      layers.push_back(entry);
    } else if (entry.alpha < 1.0F || entry.blend) {
      translucent.push_back(entry);
    } else {
      (entry.item->casts_shadow ? casting : opaque).push_back(entry);
    }
  }
  std::ranges::stable_sort(sky, {}, [](const Draw &d) { return d.draw->pass; });
  std::ranges::stable_sort(layers, std::ranges::greater{}, &Draw::distance);
  std::ranges::stable_sort(translucent, std::ranges::greater{}, &Draw::distance);

  // The sky: around the camera, depth-tested only against itself.
  if (!sky.empty()) {
    Mat4 sky_view = frame.view;
    sky_view.at(3, 0) = sky_view.at(3, 1) = sky_view.at(3, 2) = 0.0F;
    set_frame_constants(frame, sky_view);
    for (const Draw &draw : sky) {
      draw_one(draw, true, frame.lighting);
    }
    context_->ClearDepthStencilView(depth_view_.Get(), D3D11_CLEAR_DEPTH, 1.0F, 0);
  }
  set_frame_constants(frame, frame.view);
  for (const Draw &draw : opaque) {
    draw_one(draw, false, frame.lighting);
  }
  // As the original's layers 3 to 7: the volumes count against the stage,
  // the casters clear the count where they show, and what is left is
  // shadowed.
  const bool shadowed = !sun_shadowed_ && render_volumes(frame);
  for (const Draw &draw : casting) {
    draw_one(draw, false, frame.lighting);
  }
  // Layer 7 follows the robots, so scenery shadows may fall on them too.
  const bool static_shadowed = !sun_shadowed_ && render_static_volumes(frame);
  // The stage's own markings go down before the shadow is laid over them.
  for (const Draw &draw : layers) {
    draw_one(draw, false, frame.lighting);
  }
  if (shadowed || static_shadowed) {
    cover_shadows(frame.lighting);
  }
  render_optical(optical, frame.lighting);
  // Back to front; each see-through item lays down its depth before its
  // first batch is drawn.
  std::vector<int> prepassed;
  for (const Draw &draw : translucent) {
    if (draw.group >= 0 && std::ranges::find(prepassed, draw.group) == prepassed.end()) {
      prepassed.push_back(draw.group);
      for (const Draw &part : translucent) {
        if (part.group == draw.group) {
          draw_one(part, false, frame.lighting, true);
        }
      }
    }
    draw_one(draw, false, frame.lighting);
  }
  // Transparent corners must neither mask floor overlays nor overwrite their
  // depth. The completed ground receives the shadow, not its rectangular quad.
  for (const Draw &draw : ground_shadows) {
    draw_one(draw, false, frame.lighting);
  }
  render_sprites(frame);
  // As 0x0b7900, ahead of the cockpit's layer.
  if (frame.transparent) {
    render_coverage();
  } else {
    render_air(frame);
    render_screen(frame);
  }
  render_overlay(frame);
}

void WorldRenderer::item_draws(const WorldItem &item, const int index, const Vec3 eye,
                               std::vector<Draw> &out) const {
  if (item.model < 0 || item.model >= static_cast<int>(models_.size()) || !models_[item.model]) {
    return;
  }
  const GpuModel &model = *models_[static_cast<std::size_t>(item.model)];
  const bool animated = item.node_worlds.size() == model.data.nodes.size();
  ID3D11ShaderResourceView *paint =
      item.paint >= 0 && item.paint < static_cast<int>(textures_.size())
          ? textures_[static_cast<std::size_t>(item.paint)].Get()
          : nullptr;
  const Mat4 stretch = scaling({item.scale_x, 1.0F, 1.0F});
  const bool lit = post_process_.dynamic_lighting && dynamic_lights_ && !dynamic_failed_;
  for (const ModelDraw &draw : model.data.draws) {
    if (item.node >= 0 && draw.node != item.node) {
      continue;
    }
    if (lit && std::ranges::find(item.lit_hidden_materials, draw.material) !=
                   item.lit_hidden_materials.end()) {
      continue;
    }
    const auto node_index = static_cast<std::size_t>(draw.node);
    const ModelNode &node = model.data.nodes[node_index];
    if (std::ranges::find(item.shadow_nodes, node.name) != item.shadow_nodes.end()) {
      continue;
    }
    Mat4 world = animated ? item.node_worlds[node_index] : stretch * node.world * item.world;
    auto uv_offset = item.uv_offset;
    const bool sky = std::ranges::find(item.sky_nodes, node.name) != item.sky_nodes.end();
    if (sky) {
      const auto motion = std::ranges::find(item.sky_motion, node.name, &SkyMotion::node);
      if (motion != item.sky_motion.end()) {
        const float angle =
            static_cast<float>(std::fmod(item.sky_seconds * motion->rotation_per_second, 2.0 * pi));
        world = world * rotation_y(angle);
        for (std::size_t axis = 0; axis < uv_offset.size(); ++axis) {
          uv_offset[axis] +=
              static_cast<float>(std::fmod(item.sky_seconds * motion->uv_per_second[axis], 1.0));
        }
      }
    }
    const float distance = length(transform_point(draw.centre, world) - eye);
    const bool blend = blended(model.data.materials[static_cast<std::size_t>(draw.material)]);
    const bool faded = item.alpha < 1.0F;
    const auto &material = model.data.materials[static_cast<std::size_t>(draw.material)];
    ID3D11ShaderResourceView *atlas =
        material.part_paint
            ? (item.part_paint >= 0 && item.part_paint < static_cast<int>(textures_.size())
                   ? textures_[static_cast<std::size_t>(item.part_paint)].Get()
                   : nullptr)
            : paint;
    out.push_back({&model, &draw, world, distance, atlas, item.alpha, item.flash,
                   faded && !blend ? index : -1, &item, sky, blend,
                   std::ranges::find(item.monitor_nodes, node.name) != item.monitor_nodes.end(),
                   uv_offset});
  }
}

void WorldRenderer::render_optical(const std::vector<Draw> &draws, const StageLighting &lighting) {
  if (draws.empty() || !scene_view_) {
    return;
  }
  if (samples_ > 1) {
    context_->ResolveSubresource(scene_.Get(), 0, colour_.Get(), 0, format_);
  } else {
    context_->CopyResource(scene_.Get(), colour_.Get());
  }
  ID3D11ShaderResourceView *scene = scene_view_.Get();
  ID3D11SamplerState *sampler = scene_sampler_.Get();
  context_->PSSetShaderResources(2, 1, &scene);
  context_->PSSetSamplers(2, 1, &sampler);
  for (const Draw &draw : draws) {
    draw_one(draw, false, lighting);
  }
  // Unbound, so the next frame's copy may write it.
  ID3D11ShaderResourceView *none = nullptr;
  context_->PSSetShaderResources(2, 1, &none);
}

void WorldRenderer::render_overlay(const WorldFrame &frame) {
  if (frame.overlay.empty()) {
    return;
  }
  std::vector<Draw> draws;
  for (const WorldItem &item : frame.overlay) {
    item_draws(item, -1, {}, draws);
  }
  context_->ClearDepthStencilView(depth_view_.Get(), D3D11_CLEAR_DEPTH, 1.0F, 0);
  set_frame_constants(frame, Mat4{});
  for (const Draw &draw : draws) {
    draw_one(draw, true, frame.lighting, false, true);
  }
}

void WorldRenderer::draw_one(const Draw &draw, const bool sky, const StageLighting &lighting,
                             const bool depth_prepass, const bool overlay) {
  const GpuModel &model = *draw.model;
  const ModelMaterial &material =
      model.data.materials[static_cast<std::size_t>(draw.draw->material)];
  const auto view_of = [&](const int texture) -> ID3D11ShaderResourceView * {
    return texture >= 0 ? model.texture_views[static_cast<std::size_t>(texture)].Get() : nullptr;
  };
  const auto sampler_of = [&](const int texture) -> ID3D11SamplerState * {
    return texture >= 0 ? model.samplers[static_cast<std::size_t>(texture)].Get() : nullptr;
  };
  const bool painted = draw.paint != nullptr && material.base_texture >= 0 &&
                       (material.part_paint || draw.item->paint_any ||
                        model.paintable[static_cast<std::size_t>(material.base_texture)]);
  // A reflect stage's texture takes the normal map's slot (none has both).
  const int second =
      material.reflect_texture >= 0 ? material.reflect_texture : material.normal_texture;
  ID3D11ShaderResourceView *views[] = {painted ? draw.paint : view_of(material.base_texture),
                                       view_of(second)};
  ID3D11SamplerState *samplers[] = {sampler_of(material.base_texture), sampler_of(second)};

  DrawConstants constants{};
  constants.world = draw.world;
  constants.base_colour = draw.item->colour.value_or(material.base_colour);
  constants.uv_offset = draw.uv_offset;
  constants.alpha_cutoff = material.alpha_cutoff;
  constants.unlit = material.unlit ? 1.0F : 0.0F;
  if (!sky && !overlay && !material.unlit && !draw.item->ground_shadow && !draw.item->rainbow) {
    const auto source =
        sample_source_lights(lighting.source_lights, transform_point({}, draw.item->world));
    for (std::size_t i = 0; i < source.size(); ++i) {
      const auto &s = source[i];
      constants.source_directions[i] = {s.toward.x, s.toward.y, s.toward.z, 0};
      constants.source_colours[i] = {s.colour.x, s.colour.y, s.colour.z, 0};
    }
  }
  constants.has_base = views[0] != nullptr ? 1.0F : 0.0F;
  constants.has_normal = views[1] != nullptr ? 1.0F : 0.0F;
  constants.reflect_modes[0] = material.reflect_texture >= 0 && views[1] != nullptr ? 1.0F : 0.0F;
  constants.reflect_modes[1] = draw.item->sheen;
  if (const auto &label = draw.item->label_colour) {
    constants.label_colour = {(*label)[0], (*label)[1], (*label)[2], 1.0F};
  }
  constants.alpha_test = material.alpha == AlphaMode::mask ? 1.0F : 0.0F;
  // A blended batch that writes depth (grated stairs, fences) keeps its
  // invisible texels out of the depth buffer, so the faces behind its gaps,
  // later in the same batch, still show (port fix; 0x0b3240's own test,
  // GREATEREQUAL 0, passes every texel).
  if (material.alpha == AlphaMode::blend && material.blend_depth_write && !material.additive) {
    constants.alpha_test = 1.0F;
    constants.alpha_cutoff = invisible_alpha;
  }
  // Fog hides what is added (2: fade toward black) and turns the rest toward
  // its colour (1); lerping an additive draw's black texels to the fog colour
  // would add a flat box of it.
  const bool adds = material.additive || draw.item->additive;
  constants.fogged = !sky && lighting.fog && draw.item->fogged ? (adds ? 2.0F : 1.0F) : 0.0F;
  constants.item_alpha = draw.alpha;
  constants.flash = draw.flash;
  constants.local_light = post_process_.dynamic_lighting && !dynamic_failed_ && !sky && !overlay &&
                                  draw.item->local_light && !draw.monitor &&
                                  !draw.item->ground_shadow && !draw.item->rainbow &&
                                  !draw.item->additive && !material.additive
                              ? 1.0F
                              : 0.0F;
  constants.material_light = {material.ambient_colour.x, material.ambient_colour.y,
                              material.ambient_colour.z, material.specular_power};
  constants.material_modes = {material.specular ? 1.0F : 0.0F, material.texture_add ? 1.0F : 0.0F,
                              material.texture_alpha ? 1.0F : 0.0F,
                              material.original_material ? 1.0F : 0.0F};
  if (const auto &rainbow = draw.item->rainbow) {
    constants.rainbow = {(*rainbow)[0], (*rainbow)[1], 1.0F, 0.0F};
  }
  if (draw.monitor) {
    constants.uv_transform = monitor_uv(draw);
    views[0] = capturing_monitor_ ? nullptr : monitor_view_.Get();
    views[1] = nullptr;
    samplers[0] = scene_sampler_.Get();
    constants.base_colour =
        views[0] ? std::array<float, 4>{1, 1, 1, 1} : std::array<float, 4>{0, 0, 0, 1};
    constants.unlit = 1.0F;
    constants.has_base = views[0] ? 1.0F : 0.0F;
    constants.has_normal = 0.0F;
    constants.reflect_modes[0] = 0.0F;
    constants.material_modes = {0, 0, 1, 0};
  }
  if (const auto &optical = draw.item->optical) {
    constants.optical = 1.0F;
    constants.optical_sphere = {optical->centre.x, optical->centre.y, optical->centre.z,
                                optical->radius};
    constants.optical_shape = {optical->edge, optical->reach, optical->least, optical->push};
  }
  upload(context_.Get(), draw_constants_.Get(), constants);

  context_->PSSetShaderResources(0, 2, views);
  context_->PSSetSamplers(0, 2, samplers);
  const bool both_sides = material.double_sided || draw.item->double_sided ||
                          std::ranges::find(draw.item->two_sided_materials, draw.draw->material) !=
                              draw.item->two_sided_materials.end();
  // Surface layers keep depth testing and writing, but win ties with the base
  // instead of alternating as the eye moves; ground shadows sit above both.
  const bool layer = !sky && !overlay && !depth_prepass && surface_layer(draw);
  context_->RSSetState(draw.item->ground_shadow ? decal_cull_none_.Get()
                       : layer ? (both_sides ? layer_cull_none_.Get() : layer_cull_back_.Get())
                               : (both_sides ? cull_none_.Get() : cull_back_.Get()));
  const bool faded = draw.alpha < 1.0F;
  const bool see_through = material.alpha == AlphaMode::blend || faded || overlay;
  ID3D11BlendState *blend = material.additive || draw.item->additive ? additive_.Get()
                            : see_through                            ? alpha_.Get()
                                                                     : opaque_.Get();
  const bool reads_depth = !sky && !depth_prepass &&
                           ((blended(material) && !material.blend_depth_write) ||
                            draw.item->additive || faded || draw.item->ground_shadow);
  ID3D11DepthStencilState *depth = overlay                   ? depth_off_.Get()
                                   : reads_depth             ? depth_read_.Get()
                                   : draw.item->casts_shadow ? clear_stencil_.Get()
                                                             : depth_write_.Get();
  context_->OMSetBlendState(depth_prepass ? depth_only_.Get() : blend, nullptr, 0xffffffff);
  context_->OMSetDepthStencilState(depth, 0);
  ID3D11Buffer *vertices = model.vertices.Get();
  const UINT stride = sizeof(ModelVertex);
  const UINT offset = 0;
  context_->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
  context_->IASetIndexBuffer(model.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
  context_->DrawIndexed(draw.draw->index_count, draw.draw->first_index, 0);
}

} // namespace phantomcrash
