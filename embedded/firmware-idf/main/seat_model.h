#pragma once

#include <stddef.h>
#include <stdint.h>

struct SeatModelResult {
  bool ready = false;
  float occupiedProbability = 0.0f;
  uint32_t inferenceMs = 0;
  const char *fallbackReason = "model_not_run";
};

class SeatModel {
public:
  bool begin();
  SeatModelResult infer(const int8_t *features, size_t featureCount);
};
