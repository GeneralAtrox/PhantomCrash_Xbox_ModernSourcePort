#include "desktop_wreck_collision.hpp"
#include "desktop_robot.hpp"

namespace phantomcrash {

StageCollision posed_collision(const WorldRenderer &world, const std::vector<WorldItem> &items) {
  std::vector<CollisionTriangle> triangles;
  for (const auto &item : items) {
    const auto *model = world.model_data(item.model);
    if (!model) {
      continue;
    }
    for (const auto &draw : model->draws) {
      const auto &material = model->materials.at(static_cast<std::size_t>(draw.material));
      if (draw.pass != 0 || material.additive || material.alpha == AlphaMode::blend ||
          (item.node >= 0 && item.node != draw.node)) {
        continue;
      }
      const auto node = static_cast<std::size_t>(draw.node);
      const Mat4 transform = item.node_worlds.size() == model->nodes.size()
                                 ? item.node_worlds[node]
                                 : model->nodes[node].world * item.world;
      for (std::size_t offset = draw.first_index; offset + 2 < draw.first_index + draw.index_count;
           offset += 3) {
        CollisionTriangle triangle;
        Vec3 average_normal{};
        for (std::size_t corner = 0; corner < 3; ++corner) {
          const auto &vertex = model->vertices[model->indices[offset + corner]];
          triangle.points[corner] = transform_point(
              {vertex.position[0], vertex.position[1], vertex.position[2]}, transform);
          average_normal =
              average_normal +
              (transform_point({vertex.normal[0], vertex.normal[1], vertex.normal[2]}, transform) -
               transform_point({}, transform));
        }
        Vec3 normal =
            cross(triangle.points[1] - triangle.points[0], triangle.points[2] - triangle.points[0]);
        if (length(normal) < 1e-6F) {
          continue;
        }
        if (dot(normal, average_normal) < 0) {
          std::swap(triangle.points[1], triangle.points[2]);
          normal = normal * -1.0F;
        }
        triangle.normal = normalize(normal);
        triangle.surface = 0;
        triangles.push_back(triangle);
      }
    }
  }
  return StageCollision{std::move(triangles), {}};
}

StageCollision build_wreck_collision(WorldRenderer &world, const RobotWreck &wreck) {
  std::vector<WorldItem> items;
  RobotVisual visual(world, wreck.assembly);
  visual.add_items(wreck.pose, wreck.root, items, &wreck.shoulders, wreck.lost);
  return posed_collision(world, items);
}

} // namespace phantomcrash
