#include "sample_store.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "app_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"

namespace sample_store {
namespace {

constexpr char kTag[] = "sample_store";
constexpr char kBasePath[] = "/spiffs";
constexpr char kPartitionLabel[] = "samples";

bool gMounted = false;
uint32_t gNextSequence = 1;

bool hasSuffix(const char *value, const char *suffix) {
  if (value == nullptr || suffix == nullptr) {
    return false;
  }
  const size_t valueLength = strlen(value);
  const size_t suffixLength = strlen(suffix);
  if (valueLength < suffixLength) {
    return false;
  }
  return strcmp(value + valueLength - suffixLength, suffix) == 0;
}

bool validateId(const char *id) {
  if (id == nullptr || *id == '\0') {
    return false;
  }
  for (const char *cursor = id; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    const bool valid = (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') ||
                       c == '_' ||
                       c == '-';
    if (!valid) {
      return false;
    }
  }
  return true;
}

void buildPath(const char *id, const char *extension, char *out, size_t outSize) {
  snprintf(out, outSize, "%s/%s%s", kBasePath, id, extension);
}

uint32_t extractSequence(const std::string &id) {
  unsigned int sequence = 0;
  if (sscanf(id.c_str(), "sample_%u_", &sequence) != 1) {
    return 0;
  }
  return static_cast<uint32_t>(sequence);
}

esp_err_t removeSample(const char *id) {
  if (!validateId(id)) {
    return ESP_ERR_INVALID_ARG;
  }

  char jpgPath[96] = {};
  char metaPath[96] = {};
  buildPath(id, ".jpg", jpgPath, sizeof(jpgPath));
  buildPath(id, ".json", metaPath, sizeof(metaPath));

  if (remove(jpgPath) != 0 && errno != ENOENT) {
    return ESP_FAIL;
  }
  if (remove(metaPath) != 0 && errno != ENOENT) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t readFile(const char *path, std::vector<uint8_t> *out) {
  if (path == nullptr || out == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *file = fopen(path, "rb");
  if (file == nullptr) {
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return ESP_FAIL;
  }
  const long fileSize = ftell(file);
  if (fileSize < 0) {
    fclose(file);
    return ESP_FAIL;
  }
  rewind(file);

  out->assign(static_cast<size_t>(fileSize), 0);
  if (!out->empty()) {
    const size_t read = fread(out->data(), 1, out->size(), file);
    if (read != out->size()) {
      fclose(file);
      return ESP_FAIL;
    }
  }

  fclose(file);
  return ESP_OK;
}

esp_err_t writeFile(const char *path, const uint8_t *data, size_t length) {
  if (path == nullptr || (length > 0 && data == nullptr)) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *file = fopen(path, "wb");
  if (file == nullptr) {
    return ESP_FAIL;
  }

  if (length > 0) {
    const size_t written = fwrite(data, 1, length, file);
    if (written != length) {
      fclose(file);
      return ESP_FAIL;
    }
  }

  fclose(file);
  return ESP_OK;
}

esp_err_t scanIds(std::vector<std::string> *outIds) {
  if (outIds == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  outIds->clear();
  DIR *dir = opendir(kBasePath);
  if (dir == nullptr) {
    return ESP_FAIL;
  }

  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    if (!hasSuffix(entry->d_name, ".jpg")) {
      continue;
    }
    std::string id(entry->d_name, strlen(entry->d_name) - 4);
    if (!validateId(id.c_str())) {
      continue;
    }

    char metaPath[96] = {};
    buildPath(id.c_str(), ".json", metaPath, sizeof(metaPath));
    struct stat metadataStat = {};
    if (stat(metaPath, &metadataStat) != 0) {
      continue;
    }
    outIds->push_back(id);
  }

  closedir(dir);
  std::sort(outIds->begin(), outIds->end());
  return ESP_OK;
}

void updateNextSequenceFromDisk() {
  std::vector<std::string> ids;
  if (scanIds(&ids) != ESP_OK) {
    gNextSequence = 1;
    return;
  }

  uint32_t maxSequence = 0;
  for (const std::string &id : ids) {
    maxSequence = std::max(maxSequence, extractSequence(id));
  }
  gNextSequence = maxSequence + 1;
}

} // namespace

esp_err_t init() {
  if (gMounted) {
    return ESP_OK;
  }

  esp_vfs_spiffs_conf_t conf = {};
  conf.base_path = kBasePath;
  conf.partition_label = kPartitionLabel;
  conf.max_files = 8;
  conf.format_if_mount_failed = true;

  const esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK) {
    return err;
  }

  gMounted = true;
  updateNextSequenceFromDisk();

  size_t total = 0;
  size_t used = 0;
  if (esp_spiffs_info(kPartitionLabel, &total, &used) == ESP_OK) {
    ESP_LOGI(kTag, "mounted samples partition total=%u used=%u",
             static_cast<unsigned>(total),
             static_cast<unsigned>(used));
  }
  return ESP_OK;
}

uint32_t nextSequence() {
  return gNextSequence++;
}

esp_err_t save(const char *id, const uint8_t *jpegData, size_t jpegLength, const char *metadataJson) {
  if (!validateId(id) || jpegData == nullptr || jpegLength == 0 || metadataJson == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!gMounted) {
    return ESP_ERR_INVALID_STATE;
  }

  std::vector<std::string> ids;
  const esp_err_t listErr = scanIds(&ids);
  if (listErr != ESP_OK) {
    return listErr;
  }

  while (ids.size() >= SAMPLE_STORE_MAX_COUNT) {
    const esp_err_t removeErr = removeSample(ids.front().c_str());
    if (removeErr != ESP_OK) {
      return removeErr;
    }
    ids.erase(ids.begin());
  }

  char jpgPath[96] = {};
  char metaPath[96] = {};
  buildPath(id, ".jpg", jpgPath, sizeof(jpgPath));
  buildPath(id, ".json", metaPath, sizeof(metaPath));

  ESP_RETURN_ON_ERROR(removeSample(id), kTag, "remove old sample");
  ESP_RETURN_ON_ERROR(writeFile(jpgPath, jpegData, jpegLength), kTag, "write jpeg");
  const esp_err_t metaErr = writeFile(metaPath,
                                      reinterpret_cast<const uint8_t *>(metadataJson),
                                      strlen(metadataJson));
  if (metaErr != ESP_OK) {
    remove(jpgPath);
    return metaErr;
  }
  return ESP_OK;
}

esp_err_t listIds(std::vector<std::string> *outIds) {
  if (!gMounted) {
    return ESP_ERR_INVALID_STATE;
  }
  return scanIds(outIds);
}

esp_err_t loadJpeg(const char *id, std::vector<uint8_t> *outData) {
  if (!validateId(id) || outData == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!gMounted) {
    return ESP_ERR_INVALID_STATE;
  }

  char path[96] = {};
  buildPath(id, ".jpg", path, sizeof(path));
  return readFile(path, outData);
}

esp_err_t loadMetadata(const char *id, std::string *outData) {
  if (!validateId(id) || outData == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!gMounted) {
    return ESP_ERR_INVALID_STATE;
  }

  char path[96] = {};
  buildPath(id, ".json", path, sizeof(path));
  std::vector<uint8_t> bytes;
  ESP_RETURN_ON_ERROR(readFile(path, &bytes), kTag, "read metadata");
  outData->assign(bytes.begin(), bytes.end());
  return ESP_OK;
}

esp_err_t clear() {
  if (!gMounted) {
    return ESP_ERR_INVALID_STATE;
  }

  std::vector<std::string> ids;
  ESP_RETURN_ON_ERROR(scanIds(&ids), kTag, "scan ids");
  for (const std::string &id : ids) {
    ESP_RETURN_ON_ERROR(removeSample(id.c_str()), kTag, "remove sample");
  }
  updateNextSequenceFromDisk();
  return ESP_OK;
}

} // namespace sample_store
