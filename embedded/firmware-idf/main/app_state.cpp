#include "app_state.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "buzzer_player.h"
#include "display_ui.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "presence_detector.h"
#include "sample_store.h"
#include "wifi_net.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
constexpr char kTimerNvsNamespace[] = "timer";
constexpr char kSitMinutesKey[] = "sit_min";
constexpr char kAwayMinutesKey[] = "away_min";
constexpr char kCloudNvsNamespace[] = "cloud";
constexpr char kCloudSsidKey[] = "sta_ssid";
constexpr char kCloudPassKey[] = "sta_pass";
constexpr char kCloudServerKey[] = "server_url";
constexpr char kCloudDeviceIdKey[] = "device_id";
constexpr char kCloudTokenKey[] = "token";
constexpr char kLegacyDefaultDeviceId[] = "bell-robot-1";
constexpr char kDefaultCloudServerUrl[] = "";  // 通过 NVS 或 AP 配置页设置实际地址

// 按钮状态。
uint32_t lastButtonChangeMs = 0;
uint32_t lastCaptureButtonChangeMs = 0;
bool lastButtonLevel = true;
bool lastCaptureButtonLevel = true;
bool buttonPressedEvent = false;
bool buttonResetConsumed = false;
bool captureButtonPressedEvent = false;
bool captureButtonResetConsumed = false;

bool isPinDown(int pin) {
  return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
}

void buildAutoDeviceId(char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    esp_fill_random(mac, sizeof(mac));
  }
  snprintf(out,
           outSize,
           "bell-robot-%02x%02x%02x",
           mac[3],
           mac[4],
           mac[5]);
}

void generateDeviceToken(char *out, size_t outSize) {
  if (out == nullptr || outSize < 33) {
    return;
  }
  uint8_t bytes[16] = {};
  esp_fill_random(bytes, sizeof(bytes));
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    snprintf(out + i * 2, outSize - i * 2, "%02x", bytes[i]);
  }
  out[32] = '\0';
}

void readNvsString(nvs_handle_t handle, const char *key, char *dest, size_t destSize) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  dest[0] = '\0';
  size_t length = destSize;
  if (nvs_get_str(handle, key, dest, &length) != ESP_OK) {
    dest[0] = '\0';
  }
}

void updateButton(uint32_t nowMs) {
  const bool currentLevel = !isButtonDown();
  if (currentLevel != lastButtonLevel) {
    lastButtonChangeMs = nowMs;
    lastButtonLevel = currentLevel;
    return;
  }

  if (currentLevel) {
    buttonResetConsumed = false;
    return;
  }

  if (!buttonResetConsumed && nowMs - lastButtonChangeMs > BUTTON_DEBOUNCE_MS) {
    buttonResetConsumed = true;
    buttonPressedEvent = true;
  }
}

void updateCaptureButton(uint32_t nowMs) {
  const bool currentLevel = !isCaptureButtonDown();
  if (currentLevel != lastCaptureButtonLevel) {
    lastCaptureButtonChangeMs = nowMs;
    lastCaptureButtonLevel = currentLevel;
    return;
  }

  if (currentLevel) {
    captureButtonResetConsumed = false;
    return;
  }

  if (!captureButtonResetConsumed &&
      nowMs - lastCaptureButtonChangeMs > CAPTURE_BUTTON_DEBOUNCE_MS) {
    captureButtonResetConsumed = true;
    captureButtonPressedEvent = true;
  }
}

void handleButton() {
  if (!buttonPressedEvent) {
    return;
  }
  buttonPressedEvent = false;
  resetTimer();
  presenceDetector.recalibrate();
}

void handleCaptureButton(uint32_t nowMs) {
  if (!captureButtonPressedEvent) {
    return;
  }
  captureButtonPressedEvent = false;

  char sampleId[48] = {};
  if (captureAndStoreSample("button2", nowMs, sampleId, sizeof(sampleId))) {
    buzzerPlayer.startSequence(nowMs, 1, kCaptureBeepOnMs, kCaptureBeepOffMs);
    showDisplayOverlay("CAPTURED", nowMs, kTransientDisplayMs);
    ESP_LOGI(kTag, "sample saved: %s", sampleId);
    return;
  }

  buzzerPlayer.startSequence(nowMs, 2, kCaptureBeepOnMs, kCaptureBeepOffMs);
  showDisplayOverlay("SAVE ERR", nowMs, kTransientDisplayMs);
}
} // namespace

SedentaryTimer sedentaryTimer;
CloudSettings cloudSettings;
uint32_t rebootAtMs = 0;
uint32_t cloudLastPollMs = 0;
uint32_t cloudLastSuccessMs = 0;
char cloudLastError[64] = "not_started";
uint32_t sampleCounter = 0;

uint32_t millis32() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void setCloudError(const char *message) {
  safeCopy(cloudLastError, sizeof(cloudLastError), message == nullptr ? "" : message);
}

void safeCopy(char *dest, size_t destSize, const char *src) {
  if (dest == nullptr || destSize == 0) {
    return;
  }
  dest[0] = '\0';
  if (src != nullptr) {
    strlcpy(dest, src, destSize);
  }
}

bool startsWith(const char *value, const char *prefix) {
  return value != nullptr && prefix != nullptr &&
         strncmp(value, prefix, strlen(prefix)) == 0;
}

bool validServerUrl(const char *url) {
  return startsWith(url, "http://") || startsWith(url, "https://");
}

bool jsonFindString(const char *json, const char *key, char *out, size_t outSize) {
  if (json == nullptr || key == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  char pattern[40] = {};
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char *start = strstr(json, pattern);
  if (start == nullptr) {
    return false;
  }
  start += strlen(pattern);
  const char *end = strchr(start, '"');
  if (end == nullptr || end == start) {
    return false;
  }
  const size_t length = std::min(static_cast<size_t>(end - start), outSize - 1);
  memcpy(out, start, length);
  out[length] = '\0';
  return true;
}

bool jsonFindUint(const char *json, const char *key, uint32_t *out) {
  if (json == nullptr || key == nullptr || out == nullptr) {
    return false;
  }
  char pattern[40] = {};
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char *start = strstr(json, pattern);
  if (start == nullptr) {
    return false;
  }
  start += strlen(pattern);
  char *end = nullptr;
  const unsigned long value = strtoul(start, &end, 10);
  if (end == start || value > UINT32_MAX) {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

bool jsonFindBool(const char *json, const char *key, bool *out) {
  if (json == nullptr || key == nullptr || out == nullptr) {
    return false;
  }
  char pattern[40] = {};
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char *start = strstr(json, pattern);
  if (start == nullptr) {
    return false;
  }
  start += strlen(pattern);
  if (strncmp(start, "true", 4) == 0) {
    *out = true;
    return true;
  }
  if (strncmp(start, "false", 5) == 0) {
    *out = false;
    return true;
  }
  return false;
}

bool jsonFindFloat(const char *json, const char *key, float *out) {
  if (json == nullptr || key == nullptr || out == nullptr) {
    return false;
  }
  char pattern[40] = {};
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char *start = strstr(json, pattern);
  if (start == nullptr) {
    return false;
  }
  start += strlen(pattern);
  char *end = nullptr;
  const double value = strtod(start, &end);
  if (end == start) {
    return false;
  }
  *out = static_cast<float>(value);
  return true;
}

bool cloudSettingsComplete() {
  return cloudSettings.ssid[0] != '\0' &&
         cloudSettings.serverUrl[0] != '\0' &&
         cloudSettings.deviceId[0] != '\0' &&
         cloudSettings.token[0] != '\0' &&
         validServerUrl(cloudSettings.serverUrl);
}

void nvsInit() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

void loadTimerSettings() {
  uint32_t sitMinutes = minutesFromMs(DEFAULT_SIT_TARGET_MS);
  uint32_t awayMinutes = minutesFromMs(DEFAULT_AWAY_RESET_MS);
  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kTimerNvsNamespace, NVS_READONLY, &handle);
  if (openErr == ESP_OK) {
    uint32_t storedSitMinutes = sitMinutes;
    uint32_t storedAwayMinutes = awayMinutes;
    if (nvs_get_u32(handle, kSitMinutesKey, &storedSitMinutes) == ESP_OK &&
        nvs_get_u32(handle, kAwayMinutesKey, &storedAwayMinutes) == ESP_OK &&
        validTimerMinutes(storedSitMinutes, storedAwayMinutes)) {
      sitMinutes = storedSitMinutes;
      awayMinutes = storedAwayMinutes;
    }
    nvs_close(handle);
  }
  sedentaryTimer.configure(msFromMinutes(sitMinutes), msFromMinutes(awayMinutes));
  ESP_LOGI(kTag, "timer settings: sit=%lu min away=%lu min",
           static_cast<unsigned long>(sitMinutes),
           static_cast<unsigned long>(awayMinutes));
}

esp_err_t saveTimerSettings(uint32_t sitMinutes, uint32_t awayMinutes) {
  if (!validTimerMinutes(sitMinutes, awayMinutes)) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kTimerNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  err = nvs_set_u32(handle, kSitMinutesKey, sitMinutes);
  if (err == ESP_OK) {
    err = nvs_set_u32(handle, kAwayMinutesKey, awayMinutes);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err == ESP_OK) {
    sedentaryTimer.configure(msFromMinutes(sitMinutes), msFromMinutes(awayMinutes));
  }
  return err;
}

void loadCloudSettings() {
  cloudSettings = CloudSettings{};
  char autoDeviceId[48] = {};
  buildAutoDeviceId(autoDeviceId, sizeof(autoDeviceId));
  safeCopy(cloudSettings.deviceId, sizeof(cloudSettings.deviceId), autoDeviceId);
  safeCopy(cloudSettings.serverUrl, sizeof(cloudSettings.serverUrl), kDefaultCloudServerUrl);

  nvs_handle_t handle = 0;
  const esp_err_t openErr = nvs_open(kCloudNvsNamespace, NVS_READWRITE, &handle);
  if (openErr == ESP_OK) {
    readNvsString(handle, kCloudSsidKey, cloudSettings.ssid, sizeof(cloudSettings.ssid));
    readNvsString(handle, kCloudPassKey, cloudSettings.password, sizeof(cloudSettings.password));
    readNvsString(handle, kCloudServerKey, cloudSettings.serverUrl, sizeof(cloudSettings.serverUrl));
    readNvsString(handle, kCloudDeviceIdKey, cloudSettings.deviceId, sizeof(cloudSettings.deviceId));
    readNvsString(handle, kCloudTokenKey, cloudSettings.token, sizeof(cloudSettings.token));

    bool changed = false;
    if (cloudSettings.deviceId[0] == '\0' ||
        strcmp(cloudSettings.deviceId, kLegacyDefaultDeviceId) == 0) {
      safeCopy(cloudSettings.deviceId, sizeof(cloudSettings.deviceId), autoDeviceId);
      nvs_set_str(handle, kCloudDeviceIdKey, cloudSettings.deviceId);
      changed = true;
    }
    if (cloudSettings.token[0] == '\0') {
      generateDeviceToken(cloudSettings.token, sizeof(cloudSettings.token));
      nvs_set_str(handle, kCloudTokenKey, cloudSettings.token);
      changed = true;
    }
    if (cloudSettings.serverUrl[0] == '\0') {
      safeCopy(cloudSettings.serverUrl, sizeof(cloudSettings.serverUrl), kDefaultCloudServerUrl);
      nvs_set_str(handle, kCloudServerKey, cloudSettings.serverUrl);
      changed = true;
    }
    if (changed) {
      nvs_commit(handle);
    }
    nvs_close(handle);
  } else {
    if (cloudSettings.token[0] == '\0') {
      generateDeviceToken(cloudSettings.token, sizeof(cloudSettings.token));
    }
  }

  ESP_LOGI(kTag,
           "cloud settings: ssid=%s server=%s device=%s token=%s",
           cloudSettings.ssid[0] == '\0' ? "(none)" : cloudSettings.ssid,
           cloudSettings.serverUrl[0] == '\0' ? "(none)" : cloudSettings.serverUrl,
           cloudSettings.deviceId,
           cloudSettings.token[0] == '\0' ? "missing" : "set");
}

esp_err_t saveCloudSettings(const CloudSettings &settings) {
  if (settings.ssid[0] == '\0' ||
      settings.serverUrl[0] == '\0' ||
      settings.deviceId[0] == '\0' ||
      settings.token[0] == '\0' ||
      !validServerUrl(settings.serverUrl)) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kCloudNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kCloudSsidKey, settings.ssid);
  }
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kCloudPassKey, settings.password);
  }
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kCloudServerKey, settings.serverUrl);
  }
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kCloudDeviceIdKey, settings.deviceId);
  }
  if (err == ESP_OK) {
    err = nvs_set_str(handle, kCloudTokenKey, settings.token);
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  if (err == ESP_OK) {
    cloudSettings = settings;
  }
  return err;
}

esp_err_t forgetCloudSettings() {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open(kCloudNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }
  nvs_erase_key(handle, kCloudSsidKey);
  nvs_erase_key(handle, kCloudPassKey);
  nvs_erase_key(handle, kCloudServerKey);
  err = nvs_commit(handle);
  nvs_close(handle);
  if (err == ESP_OK) {
    cloudSettings.ssid[0] = '\0';
    cloudSettings.password[0] = '\0';
    safeCopy(cloudSettings.serverUrl, sizeof(cloudSettings.serverUrl), kDefaultCloudServerUrl);
  }
  return err;
}

bool isButtonDown() {
  return isPinDown(PIN_BUTTON);
}

bool isCaptureButtonDown() {
  return isPinDown(PIN_CAPTURE_BUTTON);
}

void setupButtons() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << PIN_BUTTON) | (1ULL << PIN_CAPTURE_BUTTON);
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&io);
  lastButtonLevel = !isButtonDown();
  lastCaptureButtonLevel = !isCaptureButtonDown();
}

void updateButtons(uint32_t nowMs) {
  updateButton(nowMs);
  updateCaptureButton(nowMs);
  handleButton();
  handleCaptureButton(nowMs);
}

bool captureAndStoreSample(const char *source, uint32_t nowMs, char *outId, size_t outIdSize) {
  CapturedCameraFrame capture = {};
  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  bool converted = false;
  if (!captureFrameAsJpeg(&capture, &jpgBuffer, &jpgLength, &converted)) {
    return false;
  }
  const camera_fb_t *frame = &capture.logicalFrame;

  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  const uint32_t sequence = sample_store::nextSequence();
  char sampleId[48] = {};
  snprintf(sampleId,
           sizeof(sampleId),
           "sample_%06lu_%09lu",
           static_cast<unsigned long>(sequence),
           static_cast<unsigned long>(nowMs));

  char metadata[640] = {};
  snprintf(metadata,
           sizeof(metadata),
           "{\"sample_id\":\"%s\",\"boot_ms\":%lu,\"source\":\"%s\",\"state\":\"%s\","
           "\"present\":%s,\"raw_present\":%s,\"model_ready\":%s,\"model_prob\":%.3f,"
           "\"inference_ms\":%lu,\"wifi_mode\":\"%s\",\"cloud_enabled\":%s,"
           "\"camera_rotation\":\"%s\",\"frame_width\":%u,\"frame_height\":%u,\"jpeg_bytes\":%u}",
           sampleId,
           static_cast<unsigned long>(nowMs),
           source == nullptr ? "button2" : source,
           stateLabel(sedentaryTimer.state()),
           diag.present ? "true" : "false",
           diag.rawPresent ? "true" : "false",
           diag.modelReady ? "true" : "false",
           static_cast<double>(diag.modelProbability),
           static_cast<unsigned long>(diag.inferenceMs),
           wifiModeIsSta() ? "sta" : "ap",
           ENABLE_CLOUD_REMOTE ? "true" : "false",
           CAMERA_ROTATE_CW_90 ? "cw90" : "none",
           static_cast<unsigned>(frame->width),
           static_cast<unsigned>(frame->height),
           static_cast<unsigned>(jpgLength));

  const esp_err_t err = sample_store::save(sampleId, jpgBuffer, jpgLength, metadata);
  releaseCapturedJpeg(&capture, jpgBuffer, converted);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "save sample failed: %s", esp_err_to_name(err));
    return false;
  }

  if (outId != nullptr && outIdSize > 0) {
    safeCopy(outId, outIdSize, sampleId);
  }
  return true;
}

void buildStatusPayload(char *payload, size_t payloadSize) {
  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  snprintf(payload,
           payloadSize,
           "{\"state\":\"%s\",\"present\":%s,\"calibrated\":%s,\"score\":%u,"
           "\"baseline\":%u,\"diff\":%u,\"button\":%s,\"raw_present\":%s,"
           "\"on_frames\":%u,\"off_frames\":%u,\"on_required\":%u,"
           "\"model_ready\":%s,\"model_prob\":%.3f,\"model_threshold\":%.2f,"
           "\"model_version\":\"%s\",\"inference_ms\":%lu,"
           "\"fallback_reason\":\"%s\",\"sit_minutes\":%lu,\"away_minutes\":%lu,"
           "\"camera_rotation\":\"%s\",\"cloud_enabled\":%s,"
           "\"device_id\":\"%s\",\"wifi_mode\":\"%s\",\"wifi_connected\":%s,"
           "\"cloud_configured\":%s,\"cloud_last_poll_ms\":%lu,"
           "\"cloud_last_success_ms\":%lu,\"cloud_last_error\":\"%s\"}",
           stateLabel(sedentaryTimer.state()),
           diag.present ? "true" : "false",
           diag.calibrated ? "true" : "false",
           diag.score,
           diag.baseline,
           diag.diff,
           isButtonDown() ? "true" : "false",
           diag.rawPresent ? "true" : "false",
           diag.onFrames,
           diag.offFrames,
           PRESENCE_ON_FRAMES,
           diag.modelReady ? "true" : "false",
           static_cast<double>(diag.modelProbability),
           static_cast<double>(MODEL_OCCUPIED_THRESHOLD),
           seatModel.version(),
           static_cast<unsigned long>(diag.inferenceMs),
           diag.fallbackReason == nullptr ? "" : diag.fallbackReason,
           static_cast<unsigned long>(minutesFromMs(sedentaryTimer.sitTargetMs())),
           static_cast<unsigned long>(minutesFromMs(sedentaryTimer.awayResetMs())),
           CAMERA_ROTATE_CW_90 ? "cw90" : "none",
           ENABLE_CLOUD_REMOTE ? "true" : "false",
           cloudSettings.deviceId,
           wifiModeIsSta() ? "sta" : "ap",
           wifiIsConnected() ? "true" : "false",
           cloudSettingsComplete() ? "true" : "false",
           static_cast<unsigned long>(cloudLastPollMs),
           static_cast<unsigned long>(cloudLastSuccessMs),
           cloudLastError);
}

void resetTimer() {
  sedentaryTimer.reset();
  buzzerPlayer.stopAll();
}

void requestReboot(uint32_t delayMs) {
  rebootAtMs = millis32() + delayMs;
}

} // namespace bell_robot
