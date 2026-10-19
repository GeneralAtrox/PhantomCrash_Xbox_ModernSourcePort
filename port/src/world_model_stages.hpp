#pragma once

// The original's batch values the model loader reads from
// extras.phantomcrash (docs/TRAINING_GROUND.md, 0x0b3240).

#include "phantomcrash/json.hpp"
#include "phantomcrash/world_model.hpp"

#include <optional>
#include <string>

namespace phantomcrash {

// The specular power (D3DMATERIAL8.Power). Older extractions wrote the
// float's bits as an integer (50.0 as 1112014848); no real power comes near
// that.
[[nodiscard]] float specular_power(double power);

// A reflect stage's texture (stage 1 flag 0x80: program 0x1a, reflect.xvu
// and reflect.xpu), which the converter leaves out of the glTF: the texture
// resource's slot beside the base texture's image, addressed by the stage's
// mirror and wrap flags. None without one.
[[nodiscard]] std::optional<ModelTexture> reflect_stage_texture(const json::JsonValue &game,
                                                                const std::string &base_image);

} // namespace phantomcrash
