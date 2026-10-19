#include "phantomcrash/robot.hpp"

namespace phantomcrash {
void read_body_model_variants(const json::JsonValue &value, RobotCatalogue::BodyType &body) {
  if (const auto *models = value.find("models_by_leg_type")) {
    for (const auto &[key, model] : models->object()) {
      body.models_by_leg_type.emplace(std::stoi(key), static_cast<int>(model.number()));
    }
  }
}

int body_model_for_legs(const RobotCatalogue::BodyType &body, const int leg_type) {
  const auto choice = body.models_by_leg_type.find(leg_type);
  return choice == body.models_by_leg_type.end() ? body.model : choice->second;
}
} // namespace phantomcrash
