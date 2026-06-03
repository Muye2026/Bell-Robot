#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "esp_err.h"

namespace sample_store {

esp_err_t init();
uint32_t nextSequence();
esp_err_t save(const char *id, const uint8_t *jpegData, size_t jpegLength, const char *metadataJson);
esp_err_t listIds(std::vector<std::string> *outIds);
esp_err_t loadJpeg(const char *id, std::vector<uint8_t> *outData);
esp_err_t loadMetadata(const char *id, std::string *outData);
esp_err_t clear();

} // namespace sample_store
