#include "cloud_client.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "app_config.h"
#include "app_state.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "presence_detector.h"
#include "wifi_net.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
constexpr uint32_t kCloudPollIntervalMs = 1000;
constexpr uint32_t kCloudHttpTimeoutMs = 20000;
constexpr uint32_t kCloudFailureApFallbackMs = 90000;
constexpr size_t kMaxCloudResponseBytes = 2048;

uint32_t cloudFirstFailureMs = 0;

struct CloudResponseBuffer {
  char *data = nullptr;
  size_t capacity = 0;
  size_t length = 0;
};

esp_err_t cloudHttpEvent(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == nullptr) {
    return ESP_OK;
  }
  CloudResponseBuffer *buffer = static_cast<CloudResponseBuffer *>(event->user_data);
  if (buffer->data == nullptr || buffer->capacity == 0 || event->data_len <= 0) {
    return ESP_OK;
  }
  const size_t copyLen = std::min(static_cast<size_t>(event->data_len),
                                  buffer->capacity - buffer->length - 1);
  if (copyLen > 0) {
    memcpy(buffer->data + buffer->length, event->data, copyLen);
    buffer->length += copyLen;
    buffer->data[buffer->length] = '\0';
  }
  return ESP_OK;
}

void buildCloudUrl(const char *path, char *url, size_t urlSize) {
  const size_t length = strlen(cloudSettings.serverUrl);
  const bool hasSlash = length > 0 && cloudSettings.serverUrl[length - 1] == '/';
  snprintf(url, urlSize, "%s%s%s", cloudSettings.serverUrl, hasSlash ? "" : "/", path);
}

esp_err_t cloudPost(const char *path,
                    const char *contentType,
                    const uint8_t *body,
                    size_t bodyLength,
                    char *response,
                    size_t responseSize) {
  char url[240] = {};
  buildCloudUrl(path, url, sizeof(url));

  CloudResponseBuffer responseBuffer = {response, responseSize, 0};
  if (response != nullptr && responseSize > 0) {
    response[0] = '\0';
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = kCloudHttpTimeoutMs;
  config.event_handler = cloudHttpEvent;
  config.user_data = &responseBuffer;
  if (startsWith(url, "https://")) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return ESP_FAIL;
  }

  char auth[128] = {};
  snprintf(auth, sizeof(auth), "Bearer %s", cloudSettings.token);
  esp_http_client_set_header(client, "Authorization", auth);
  esp_http_client_set_header(client, "Content-Type", contentType);
  esp_http_client_set_post_field(client,
                                 reinterpret_cast<const char *>(body),
                                 static_cast<int>(bodyLength));

  esp_err_t err = esp_http_client_perform(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "cloud post failed: %s %s", path, esp_err_to_name(err));
    return err;
  }
  if (status < 200 || status >= 300) {
    ESP_LOGW(kTag, "cloud post status=%d path=%s response=%s", status, path, response == nullptr ? "" : response);
    return ESP_FAIL;
  }
  return ESP_OK;
}

void postCommandResult(const char *commandId, bool ok, const char *message) {
  char path[] = "device/result";
  char body[256] = {};
  snprintf(body,
           sizeof(body),
           "{\"device_id\":\"%s\",\"command_id\":\"%s\",\"ok\":%s,\"message\":\"%s\"}",
           cloudSettings.deviceId,
           commandId == nullptr ? "" : commandId,
           ok ? "true" : "false",
           message == nullptr ? "" : message);
  char response[256] = {};
  cloudPost(path,
            "application/json",
            reinterpret_cast<const uint8_t *>(body),
            strlen(body),
            response,
            sizeof(response));
}

bool captureJpegForCloud(const char *commandId) {
  CapturedCameraFrame capture = {};
  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  bool converted = false;
  if (!captureFrameAsJpeg(&capture, &jpgBuffer, &jpgLength, &converted)) {
    return false;
  }

  char path[160] = {};
  snprintf(path,
           sizeof(path),
           "device/capture?device_id=%s&command_id=%s",
           cloudSettings.deviceId,
           commandId == nullptr ? "" : commandId);
  char response[256] = {};
  const esp_err_t err = cloudPost(path, "image/jpeg", jpgBuffer, jpgLength, response, sizeof(response));
  releaseCapturedJpeg(&capture, jpgBuffer, converted);
  return err == ESP_OK;
}

void handleCloudCommand(const char *response) {
  if (response == nullptr || strstr(response, "\"command\":null") != nullptr) {
    return;
  }
  char commandId[48] = {};
  char commandType[24] = {};
  if (!jsonFindString(response, "id", commandId, sizeof(commandId)) ||
      !jsonFindString(response, "type", commandType, sizeof(commandType))) {
    return;
  }

  if (strcmp(commandType, "capture") == 0) {
    const bool ok = captureJpegForCloud(commandId);
    if (!ok) {
      postCommandResult(commandId, false, "capture_failed");
    }
    return;
  }

  if (strcmp(commandType, "set_settings") == 0) {
    uint32_t sitMinutes = 0;
    uint32_t awayMinutes = 0;
    const bool ok = jsonFindUint(response, "sit_minutes", &sitMinutes) &&
                    jsonFindUint(response, "away_minutes", &awayMinutes) &&
                    saveTimerSettings(sitMinutes, awayMinutes) == ESP_OK;
    postCommandResult(commandId, ok, ok ? "settings_saved" : "settings_failed");
    return;
  }

  if (strcmp(commandType, "reset") == 0) {
    resetTimer();
    presenceDetector.recalibrate();
    postCommandResult(commandId, true, "reset_ok");
    return;
  }
}

void cloudPollTask(void *arg) {
  (void)arg;
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(kCloudPollIntervalMs));
    if (!cloudSettingsComplete() || !wifiModeIsSta() || !wifiIsConnected()) {
      continue;
    }

    char status[1024] = {};
    buildStatusPayload(status, sizeof(status));
    char body[1280] = {};
    snprintf(body,
             sizeof(body),
             "{\"device_id\":\"%s\",\"status\":%s}",
             cloudSettings.deviceId,
             status);

    char response[kMaxCloudResponseBytes] = {};
    cloudLastPollMs = millis32();
    const esp_err_t err = cloudPost("device/poll",
                                    "application/json",
                                    reinterpret_cast<const uint8_t *>(body),
                                    strlen(body),
                                    response,
                                    sizeof(response));
    if (err == ESP_OK) {
      cloudLastSuccessMs = millis32();
      setCloudError("ok");
      handleCloudCommand(response);
    } else {
      const uint32_t nowMs = millis32();
      if (cloudFirstFailureMs == 0) {
        cloudFirstFailureMs = nowMs;
      }
      setCloudError("poll_failed");
      if (static_cast<int32_t>(nowMs - cloudFirstFailureMs) >=
          static_cast<int32_t>(kCloudFailureApFallbackMs)) {
        ESP_LOGW(kTag, "cloud poll failed for too long, fallback to AP");
        setCloudError("cloud_fallback_ap");
        esp_wifi_stop();
        cloudFirstFailureMs = 0;
        restartWifiApFallback();
      }
    }
  }
}

} // namespace

void startCloudPollTask() {
  xTaskCreate(cloudPollTask, "cloud_poll", 8192, nullptr, 5, nullptr);
}

} // namespace bell_robot
