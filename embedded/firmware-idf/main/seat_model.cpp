#include "seat_model.h"

#include <math.h>

#include "esp_timer.h"
#include "seat_model_data.h"

bool SeatModel::begin() {
  return kSeatModelTrained && kSeatModelFeatureCount > 0;
}

SeatModelResult SeatModel::infer(const int8_t *features, size_t featureCount) {
  const int64_t startUs = esp_timer_get_time();
  SeatModelResult result = {};

  if (!kSeatModelTrained) {
    result.fallbackReason = "model_untrained";
    return result;
  }
  if (features == nullptr || featureCount != kSeatModelFeatureCount) {
    result.fallbackReason = "bad_feature_shape";
    return result;
  }

  int32_t score = kSeatModelBias;
  for (size_t i = 0; i < featureCount; ++i) {
    score += static_cast<int32_t>(features[i]) * static_cast<int32_t>(kSeatModelWeights[i]);
  }

  // The trainer emits weights scaled for this denominator.
  const float logit = static_cast<float>(score) / 4096.0f;
  result.occupiedProbability = 1.0f / (1.0f + expf(-logit));
  result.ready = true;
  result.inferenceMs = static_cast<uint32_t>((esp_timer_get_time() - startUs + 999) / 1000);
  result.fallbackReason = "";
  return result;
}
