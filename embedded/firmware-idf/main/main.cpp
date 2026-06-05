#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "app_config.h"
#include "esp_camera.h"
#include "esp_camera_af.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "lwip/ip4_addr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sample_store.h"
#include "seat_model.h"
#include "ssd1306_spi.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

namespace {
constexpr char kTag[] = "bell_robot";
constexpr uint32_t kFeatureCount = 64;
constexpr int kTimerDigitScale = 4;
constexpr uint32_t kMsPerMinute = 60UL * 1000UL;
constexpr uint32_t kSitMinMinutes = 1;
constexpr uint32_t kSitMaxMinutes = 180;
constexpr uint32_t kAwayMinMinutes = 1;
constexpr uint32_t kAwayMaxMinutes = 5;
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
constexpr char kDefaultCloudServerUrl[] = "http://43.134.30.245:8080";
constexpr uint32_t kStaConnectTimeoutMs = 15000;
constexpr uint32_t kCloudPollIntervalMs = 1000;
constexpr uint32_t kCloudHttpTimeoutMs = 20000;
constexpr uint32_t kCloudFailureApFallbackMs = 90000;
constexpr size_t kMaxCloudResponseBytes = 2048;
constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr uint32_t kTransientDisplayMs = 1000;
constexpr uint32_t kCaptureBeepOnMs = 100;
constexpr uint32_t kCaptureBeepOffMs = 90;
constexpr framesize_t kCameraFrameSize = FRAMESIZE_QVGA;
constexpr uint16_t kCameraFrameWidth = 320;
constexpr uint16_t kCameraFrameHeight = 240;
constexpr framesize_t kCameraCaptureFrameSize = FRAMESIZE_VGA;
constexpr uint16_t kCameraCaptureFrameWidth = 640;
constexpr uint16_t kCameraCaptureFrameHeight = 480;
constexpr uint8_t kCameraJpegQuality = 90;
constexpr uint8_t kCameraSensorJpegQuality = 12;
constexpr uint32_t kCameraStreamFrameDelayMs = 1;
constexpr uint32_t kCameraStreamLogFrames = 60;
const TickType_t kCameraMutexTimeoutTicks = pdMS_TO_TICKS(1500);
constexpr uint32_t kCameraAfTimeoutMs = 3000;

enum class TimerState {
  Idle,
  Sitting,
  AwayGrace,
  AwayWarning,
  Alerting,
};

struct TimerContext {
  TimerState state = TimerState::Idle;
  uint32_t sitStartMs = 0;
  uint32_t awayStartMs = 0;
};

struct TimerSettings {
  uint32_t sitTargetMs = DEFAULT_SIT_TARGET_MS;
  uint32_t awayResetMs = DEFAULT_AWAY_RESET_MS;
};

struct CloudSettings {
  char ssid[33] = {};
  char password[65] = {};
  char serverUrl[160] = {};
  char deviceId[48] = {};
  char token[96] = {};
};

struct PresenceDiagnostics {
  bool present = false;
  bool calibrated = false;
  bool modelReady = false;
  bool rawPresent = false;
  uint8_t onFrames = 0;
  uint8_t offFrames = 0;
  float modelProbability = 0.0f;
  uint32_t inferenceMs = 0;
  const char *fallbackReason = "not_sampled";
  uint16_t score = 0;
  uint16_t baseline = 0;
  uint16_t diff = 0;
};

struct BuzzerSequence {
  bool active = false;
  bool toneOn = false;
  uint8_t remainingPhases = 0;
  uint32_t nextToggleMs = 0;
  uint32_t onMs = 0;
  uint32_t offMs = 0;
};

struct DisplayOverlay {
  char message[16] = {};
  uint32_t untilMs = 0;
};

struct CapturedCameraFrame {
  camera_fb_t *rawFrame = nullptr;
  camera_fb_t logicalFrame = {};
  uint8_t *ownedBuffer = nullptr;
  bool cameraLocked = false;
};

Ssd1306Spi display;
SeatModel seatModel;
TimerContext timerContext;
TimerSettings timerSettings;
CloudSettings cloudSettings;
httpd_handle_t httpServer = nullptr;
EventGroupHandle_t wifiEventGroup = nullptr;
SemaphoreHandle_t cameraMutex = nullptr;
esp_netif_t *apNetif = nullptr;
esp_netif_t *staNetif = nullptr;

uint32_t lastDisplayMs = 0;
uint32_t lastStatusLogMs = 0;
uint32_t lastButtonChangeMs = 0;
uint32_t lastCaptureButtonChangeMs = 0;
uint32_t cloudLastPollMs = 0;
uint32_t cloudLastSuccessMs = 0;
uint32_t cloudFirstFailureMs = 0;
uint32_t rebootAtMs = 0;
bool lastButtonLevel = true;
bool lastCaptureButtonLevel = true;
bool buttonPressedEvent = false;
bool buttonResetConsumed = false;
bool captureButtonPressedEvent = false;
bool captureButtonResetConsumed = false;
bool buzzerActive = false;
bool wifiStartedAsSta = false;
bool wifiStartedAsAp = false;
uint32_t sampleCounter = 0;
char cloudLastError[64] = "not_started";
BuzzerSequence buzzerSequence;
DisplayOverlay displayOverlay;

esp_err_t sendSamplesList(httpd_req_t *req);
esp_err_t sendSampleFile(httpd_req_t *req);
esp_err_t sendSampleMetadata(httpd_req_t *req);
esp_err_t handleSamplesClear(httpd_req_t *req);
esp_err_t handleSampleCapture(httpd_req_t *req);
bool captureCameraFrame(CapturedCameraFrame *capture, framesize_t frameSize = kCameraFrameSize);
void releaseCameraFrame(CapturedCameraFrame *capture);
bool lockCamera(CapturedCameraFrame *capture);
void unlockCamera(CapturedCameraFrame *capture);
bool captureFrameAsJpeg(CapturedCameraFrame *capture,
                        uint8_t **outJpgBuffer,
                        size_t *outJpgLength,
                        bool *outConverted,
                        uint8_t jpegQuality = kCameraJpegQuality,
                        framesize_t frameSize = kCameraCaptureFrameSize);
void releaseCapturedJpeg(CapturedCameraFrame *capture, uint8_t *jpgBuffer, bool converted);
bool setCameraFrameSize(framesize_t frameSize);
void configureCameraSensor(sensor_t *sensor);
void configureCameraAutofocus(sensor_t *sensor);

uint32_t millis32() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

size_t cameraBytesPerPixel(pixformat_t format) {
  switch (format) {
  case PIXFORMAT_GRAYSCALE:
    return 1;
  case PIXFORMAT_RGB565:
    return 2;
  default:
    return 0;
  }
}

bool lockCamera(CapturedCameraFrame *capture) {
  if (capture == nullptr) {
    return false;
  }
  if (cameraMutex == nullptr) {
    capture->cameraLocked = true;
    return true;
  }
  if (xSemaphoreTake(cameraMutex, kCameraMutexTimeoutTicks) != pdTRUE) {
    return false;
  }
  capture->cameraLocked = true;
  return true;
}

void unlockCamera(CapturedCameraFrame *capture) {
  if (capture == nullptr || !capture->cameraLocked) {
    return;
  }
  capture->cameraLocked = false;
  if (cameraMutex != nullptr) {
    xSemaphoreGive(cameraMutex);
  }
}

bool setCameraFrameSize(framesize_t frameSize) {
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr || sensor->set_framesize == nullptr) {
    return false;
  }
  if (sensor->status.framesize == frameSize) {
    return true;
  }
  if (sensor->set_framesize(sensor, frameSize) != 0) {
    ESP_LOGW(kTag, "camera set framesize failed: %d", static_cast<int>(frameSize));
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(80));
  return true;
}

uint8_t frameLumaAt(const camera_fb_t *frame, size_t x, size_t y) {
  if (frame == nullptr || frame->buf == nullptr || x >= frame->width || y >= frame->height) {
    return 0;
  }

  const size_t pixelIndex = y * frame->width + x;
  if (frame->format == PIXFORMAT_GRAYSCALE) {
    return pixelIndex < frame->len ? frame->buf[pixelIndex] : 0;
  }

  if (frame->format == PIXFORMAT_RGB565) {
    const size_t byteIndex = pixelIndex * 2;
    if (byteIndex + 1 >= frame->len) {
      return 0;
    }
    const uint8_t high = frame->buf[byteIndex];
    const uint8_t low = frame->buf[byteIndex + 1];
    const uint8_t r = high & 0xf8;
    const uint8_t g = static_cast<uint8_t>(((high & 0x07) << 5) | ((low & 0xe0) >> 3));
    const uint8_t b = static_cast<uint8_t>((low & 0x1f) << 3);
    return static_cast<uint8_t>((static_cast<uint16_t>(r) * 30 +
                                 static_cast<uint16_t>(g) * 59 +
                                 static_cast<uint16_t>(b) * 11) /
                                100);
  }

  return 0;
}

bool captureCameraFrame(CapturedCameraFrame *capture, framesize_t frameSize) {
  if (capture == nullptr) {
    return false;
  }

  releaseCameraFrame(capture);
  if (!lockCamera(capture)) {
    ESP_LOGW(kTag, "camera lock timeout");
    return false;
  }

  if (!setCameraFrameSize(frameSize)) {
    releaseCameraFrame(capture);
    return false;
  }

  camera_fb_t *rawFrame = esp_camera_fb_get();
  if (rawFrame == nullptr) {
    if (frameSize != kCameraFrameSize) {
      setCameraFrameSize(kCameraFrameSize);
    }
    releaseCameraFrame(capture);
    return false;
  }

  capture->rawFrame = rawFrame;
  capture->logicalFrame = *rawFrame;
  if (frameSize != kCameraFrameSize) {
    setCameraFrameSize(kCameraFrameSize);
  }

  if (rawFrame->format == PIXFORMAT_JPEG) {
    const size_t decodedLength = static_cast<size_t>(rawFrame->width) *
                                 static_cast<size_t>(rawFrame->height) *
                                 cameraBytesPerPixel(PIXFORMAT_RGB565);
    uint8_t *decoded = static_cast<uint8_t *>(heap_caps_malloc(decodedLength, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decoded == nullptr) {
      decoded = static_cast<uint8_t *>(heap_caps_malloc(decodedLength, MALLOC_CAP_8BIT));
    }
    if (decoded == nullptr) {
      ESP_LOGE(kTag, "camera JPEG decode buffer allocation failed: %u bytes",
               static_cast<unsigned>(decodedLength));
      releaseCameraFrame(capture);
      return false;
    }

    if (!jpg2rgb565(rawFrame->buf, rawFrame->len, decoded, JPG_SCALE_NONE)) {
      ESP_LOGE(kTag, "camera JPEG decode failed");
      free(decoded);
      releaseCameraFrame(capture);
      return false;
    }

    capture->ownedBuffer = decoded;
    capture->logicalFrame.buf = decoded;
    capture->logicalFrame.len = decodedLength;
    capture->logicalFrame.width = rawFrame->width;
    capture->logicalFrame.height = rawFrame->height;
    capture->logicalFrame.format = PIXFORMAT_RGB565;
  }

  if (!CAMERA_ROTATE_CW_90) {
    return true;
  }

  const camera_fb_t sourceFrame = capture->logicalFrame;
  const size_t bytesPerPixel = cameraBytesPerPixel(sourceFrame.format);
  if (bytesPerPixel == 0 || sourceFrame.width == 0 || sourceFrame.height == 0) {
    ESP_LOGE(kTag, "camera rotate requires supported frame format with dimensions: format=%d",
             static_cast<int>(sourceFrame.format));
    releaseCameraFrame(capture);
    return false;
  }

  const size_t sourceWidth = sourceFrame.width;
  const size_t sourceHeight = sourceFrame.height;
  const size_t pixelCount = sourceWidth * sourceHeight;
  const size_t dataLength = pixelCount * bytesPerPixel;
  if (sourceFrame.len < dataLength) {
    ESP_LOGE(kTag, "camera frame too small for rotation: len=%u expected=%u",
             static_cast<unsigned>(sourceFrame.len),
             static_cast<unsigned>(dataLength));
    releaseCameraFrame(capture);
    return false;
  }

  uint8_t *rotated = static_cast<uint8_t *>(heap_caps_malloc(dataLength, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (rotated == nullptr) {
    rotated = static_cast<uint8_t *>(heap_caps_malloc(dataLength, MALLOC_CAP_8BIT));
  }
  if (rotated == nullptr) {
    ESP_LOGE(kTag, "camera rotate buffer allocation failed: %u bytes", static_cast<unsigned>(dataLength));
    releaseCameraFrame(capture);
    return false;
  }

  for (size_t y = 0; y < sourceHeight; ++y) {
    for (size_t x = 0; x < sourceWidth; ++x) {
      const size_t targetX = sourceHeight - 1 - y;
      const size_t targetY = x;
      const size_t sourceIndex = (y * sourceWidth + x) * bytesPerPixel;
      const size_t targetIndex = (targetY * sourceHeight + targetX) * bytesPerPixel;
      memcpy(rotated + targetIndex, sourceFrame.buf + sourceIndex, bytesPerPixel);
    }
  }

  uint8_t *previousOwnedBuffer = capture->ownedBuffer;
  capture->ownedBuffer = rotated;
  capture->logicalFrame.buf = rotated;
  capture->logicalFrame.len = dataLength;
  capture->logicalFrame.width = sourceHeight;
  capture->logicalFrame.height = sourceWidth;
  capture->logicalFrame.format = sourceFrame.format;
  if (previousOwnedBuffer != nullptr) {
    free(previousOwnedBuffer);
  }
  return true;
}

void releaseCameraFrame(CapturedCameraFrame *capture) {
  if (capture == nullptr) {
    return;
  }
  if (capture->ownedBuffer != nullptr) {
    free(capture->ownedBuffer);
  }
  if (capture->rawFrame != nullptr) {
    esp_camera_fb_return(capture->rawFrame);
  }
  unlockCamera(capture);
  capture->rawFrame = nullptr;
  capture->logicalFrame = {};
  capture->ownedBuffer = nullptr;
  capture->cameraLocked = false;
}

void configureCameraSensor(sensor_t *sensor) {
  if (sensor == nullptr) {
    return;
  }
  sensor->set_vflip(sensor, CAMERA_VFLIP ? 1 : 0);
  sensor->set_hmirror(sensor, CAMERA_HMIRROR ? 1 : 0);
  sensor->set_framesize(sensor, kCameraFrameSize);
  sensor->set_whitebal(sensor, 1);
  sensor->set_gain_ctrl(sensor, 1);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_brightness(sensor, 0);
  sensor->set_saturation(sensor, 1);
  sensor->set_contrast(sensor, 1);
  sensor->set_sharpness(sensor, 2);
  sensor->set_denoise(sensor, 1);
}

void configureCameraAutofocus(sensor_t *sensor) {
  if (sensor == nullptr) {
    return;
  }
  if (!esp_camera_af_is_supported(sensor)) {
    ESP_LOGW(kTag, "camera autofocus not supported by current sensor/driver config");
    return;
  }

  const esp_camera_af_config_t afConfig = {
      .mode = ESP_CAMERA_AF_MODE_AUTO,
      .step_size = 0,
      .range_min = 0,
      .range_max = 0,
      .timeout_ms = kCameraAfTimeoutMs,
  };
  esp_err_t err = esp_camera_af_init(sensor, &afConfig);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "camera autofocus init failed: %s", esp_err_to_name(err));
    return;
  }

  esp_camera_af_status_t status = {};
  err = esp_camera_af_wait(sensor, kCameraAfTimeoutMs, &status);
  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "camera autofocus ready: raw=%u focused=%c busy=%c",
             static_cast<unsigned>(status.raw),
             status.focused ? 'Y' : 'N',
             status.busy ? 'Y' : 'N');
  } else {
    ESP_LOGW(kTag, "camera autofocus wait failed: %s", esp_err_to_name(err));
  }
}

const char *stateLabel(TimerState state) {
  switch (state) {
  case TimerState::Idle:
    return "WAIT";
  case TimerState::Sitting:
    return "SIT";
  case TimerState::AwayGrace:
    return "AWAY";
  case TimerState::AwayWarning:
    return "RESET";
  case TimerState::Alerting:
    return "STAND";
  }
  return "UNKNOWN";
}

const char *displayStateLabel(TimerState state, bool isPresent) {
  switch (state) {
  case TimerState::Idle:
    return isPresent ? "SEATED" : "EMPTY";
  case TimerState::Sitting:
    return "SEATED";
  case TimerState::AwayGrace:
  case TimerState::AwayWarning:
    return "AWAY";
  case TimerState::Alerting:
    return "ALERT";
  }
  return "UNKNOWN";
}

int scaledTextWidth(const char *text, int scale) {
  if (text == nullptr || scale <= 0) {
    return 0;
  }
  return static_cast<int>(strlen(text)) * 6 * scale;
}

int centeredTextX(const char *text, int scale) {
  return std::max(0, (OLED_WIDTH - scaledTextWidth(text, scale)) / 2);
}

struct OledDiagnosticProfile {
  const char *label;
  uint8_t comScanDirection;
  uint8_t comPinsConfig;
};

void flushDisplayForMs(uint32_t durationMs) {
  display.flush();
  vTaskDelay(pdMS_TO_TICKS(durationMs));
}

void clearDisplayBand(int yStart, int yEnd) {
  for (int y = yStart; y <= yEnd && y < OLED_HEIGHT; ++y) {
    if (y < 0) {
      continue;
    }
    for (int x = 0; x < OLED_WIDTH; ++x) {
      display.setPixel(x, y, false);
    }
  }
}

void drawProfileLabel(const char *label) {
  clearDisplayBand(48, OLED_HEIGHT - 1);
  display.textScaled(centeredTextX(label, 1), 56, 1, label);
}

void drawCheckerPattern() {
  display.clear();
  for (int y = 0; y < OLED_HEIGHT; ++y) {
    for (int x = 0; x < OLED_WIDTH; ++x) {
      if (((x / 4) + (y / 4)) % 2 == 0) {
        display.setPixel(x, y);
      }
    }
  }
}

void drawVerticalStripePattern() {
  display.clear();
  for (int x = 0; x < OLED_WIDTH; ++x) {
    if ((x / 2) % 2 != 0) {
      continue;
    }
    for (int y = 0; y < OLED_HEIGHT; ++y) {
      display.setPixel(x, y);
    }
  }
}

void runOledDiagnosticsForProfile(const OledDiagnosticProfile &profile) {
  display.begin(profile.comScanDirection, profile.comPinsConfig);

  display.clear();
  drawProfileLabel(profile.label);
  flushDisplayForMs(700);

  display.fill(true);
  drawProfileLabel(profile.label);
  flushDisplayForMs(1200);

  drawVerticalStripePattern();
  drawProfileLabel(profile.label);
  flushDisplayForMs(1200);

  drawCheckerPattern();
  drawProfileLabel(profile.label);
  flushDisplayForMs(1200);

  display.clear();
  flushDisplayForMs(300);
}

void runOledDiagnostics() {
  if (!ENABLE_OLED_DIAGNOSTICS) {
    return;
  }

  const OledDiagnosticProfile profiles[] = {
      {"P1 A1 C8 DA12", 0xc8, 0x12},
      {"P2 A1 C0 DA12", 0xc0, 0x12},
      {"P3 A1 C8 DA02", 0xc8, 0x02},
      {"P4 A1 C0 DA02", 0xc0, 0x02},
      {"P5 A1 C8 DA32", 0xc8, 0x32},
      {"P6 A1 C0 DA32", 0xc0, 0x32},
  };

  for (const OledDiagnosticProfile &profile : profiles) {
    runOledDiagnosticsForProfile(profile);
  }

  display.begin();
  display.clear();
  display.flush();
}

void normalizeNvsInit() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

uint32_t minutesFromMs(uint32_t ms) {
  return ms / kMsPerMinute;
}

uint32_t msFromMinutes(uint32_t minutes) {
  return minutes * kMsPerMinute;
}

bool validTimerMinutes(uint32_t sitMinutes, uint32_t awayMinutes) {
  return sitMinutes >= kSitMinMinutes && sitMinutes <= kSitMaxMinutes &&
         awayMinutes >= kAwayMinMinutes && awayMinutes <= kAwayMaxMinutes;
}

void applyTimerMinutes(uint32_t sitMinutes, uint32_t awayMinutes) {
  timerSettings.sitTargetMs = msFromMinutes(sitMinutes);
  timerSettings.awayResetMs = msFromMinutes(awayMinutes);
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

bool cloudSettingsComplete() {
  return cloudSettings.ssid[0] != '\0' &&
         cloudSettings.serverUrl[0] != '\0' &&
         cloudSettings.deviceId[0] != '\0' &&
         cloudSettings.token[0] != '\0' &&
         validServerUrl(cloudSettings.serverUrl);
}

void setCloudError(const char *message) {
  safeCopy(cloudLastError, sizeof(cloudLastError), message == nullptr ? "" : message);
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
  applyTimerMinutes(sitMinutes, awayMinutes);
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
    applyTimerMinutes(sitMinutes, awayMinutes);
  }
  return err;
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

class PresenceDetector {
public:
  bool begin() {
    modelReady_ = seatModel.begin();
    if (cameraMutex == nullptr) {
      cameraMutex = xSemaphoreCreateMutex();
      if (cameraMutex == nullptr) {
        ESP_LOGE(kTag, "camera mutex allocation failed");
        return false;
      }
    }

    camera_config_t cameraConfig = {};
    cameraConfig.ledc_channel = LEDC_CHANNEL_0;
    cameraConfig.ledc_timer = LEDC_TIMER_0;
    cameraConfig.pin_d0 = PIN_CAM_D0;
    cameraConfig.pin_d1 = PIN_CAM_D1;
    cameraConfig.pin_d2 = PIN_CAM_D2;
    cameraConfig.pin_d3 = PIN_CAM_D3;
    cameraConfig.pin_d4 = PIN_CAM_D4;
    cameraConfig.pin_d5 = PIN_CAM_D5;
    cameraConfig.pin_d6 = PIN_CAM_D6;
    cameraConfig.pin_d7 = PIN_CAM_D7;
    cameraConfig.pin_xclk = PIN_CAM_XCLK;
    cameraConfig.pin_pclk = PIN_CAM_PCLK;
    cameraConfig.pin_vsync = PIN_CAM_VSYNC;
    cameraConfig.pin_href = PIN_CAM_HREF;
    cameraConfig.pin_sccb_sda = PIN_CAM_SIOD;
    cameraConfig.pin_sccb_scl = PIN_CAM_SIOC;
    cameraConfig.pin_pwdn = PIN_CAM_PWDN;
    cameraConfig.pin_reset = PIN_CAM_RESET;
    cameraConfig.xclk_freq_hz = 20000000;
    cameraConfig.frame_size = kCameraFrameSize;
    cameraConfig.pixel_format = PIXFORMAT_JPEG;
    cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
    cameraConfig.jpeg_quality = kCameraSensorJpegQuality;
    cameraConfig.fb_count = 3;

    const esp_err_t err = esp_camera_init(&cameraConfig);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "camera init failed: %s", esp_err_to_name(err));
      return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
      configureCameraSensor(sensor);
      configureCameraAutofocus(sensor);
    }
    ESP_LOGI(kTag,
             "camera ready: %ux%u JPEG preview, capture=%ux%u%s",
             static_cast<unsigned>(kCameraFrameWidth),
             static_cast<unsigned>(kCameraFrameHeight),
             static_cast<unsigned>(kCameraCaptureFrameWidth),
             static_cast<unsigned>(kCameraCaptureFrameHeight),
             CAMERA_ROTATE_CW_90 ? " rotated cw90" : "");
    return true;
  }

  bool update(uint32_t nowMs) {
    if (nowMs - lastSampleMs_ < CAMERA_SAMPLE_INTERVAL_MS) {
      return diagnostics_.present;
    }
    lastSampleMs_ = nowMs;

    const bool rawPresent = sampleCameraPresence();
    updateDebouncedPresence(rawPresent);
    diagnostics_.present = present_;
    diagnostics_.rawPresent = rawPresent;
    diagnostics_.onFrames = onFrames_;
    diagnostics_.offFrames = offFrames_;
    return present_;
  }

  void recalibrate() {
    present_ = false;
    calibrated_ = false;
    onFrames_ = 0;
    offFrames_ = 0;
    baseline_ = -1;
    calibrationSum_ = 0;
    calibrationFrames_ = 0;
    diagnostics_ = PresenceDiagnostics{};
  }

  PresenceDiagnostics diagnostics() const {
    PresenceDiagnostics value = diagnostics_;
    value.present = present_;
    value.calibrated = calibrated_;
    value.baseline = baseline_ < 0 ? 0 : static_cast<uint16_t>(baseline_);
    return value;
  }

  void exportNormalizedFeatures(const camera_fb_t *frame, int8_t *features, size_t featureCount) const {
    buildModelFeatures(frame, features, featureCount);
  }

private:
  bool present_ = false;
  bool calibrated_ = false;
  bool modelReady_ = false;
  uint32_t lastSampleMs_ = 0;
  uint8_t onFrames_ = 0;
  uint8_t offFrames_ = 0;
  int32_t baseline_ = -1;
  uint32_t calibrationSum_ = 0;
  uint8_t calibrationFrames_ = 0;
  PresenceDiagnostics diagnostics_ = {};

  void updateDebouncedPresence(bool rawPresent) {
    diagnostics_.rawPresent = rawPresent;
    if (rawPresent) {
      onFrames_ = std::min<uint8_t>(onFrames_ + 1, PRESENCE_ON_FRAMES);
      offFrames_ = 0;
      if (onFrames_ >= PRESENCE_ON_FRAMES) {
        present_ = true;
      }
      return;
    }

    offFrames_ = std::min<uint8_t>(offFrames_ + 1, PRESENCE_OFF_FRAMES);
    onFrames_ = 0;
    if (offFrames_ >= PRESENCE_OFF_FRAMES) {
      present_ = false;
    }
  }

  bool sampleCameraPresence() {
    CapturedCameraFrame capture = {};
    if (!captureCameraFrame(&capture)) {
      diagnostics_.fallbackReason = "camera_frame_failed";
      return present_;
    }
    const camera_fb_t *frame = &capture.logicalFrame;

    int8_t features[kFeatureCount] = {};
    buildModelFeatures(frame, features, kFeatureCount);

    const uint16_t roiScore = calculateRoiScore(frame);
    releaseCameraFrame(&capture);
    diagnostics_.score = roiScore;

    const SeatModelResult modelResult = seatModel.infer(features, kFeatureCount);
    diagnostics_.modelReady = modelResult.ready;
    diagnostics_.modelProbability = modelResult.occupiedProbability;
    diagnostics_.inferenceMs = modelResult.inferenceMs;
    diagnostics_.fallbackReason = modelResult.fallbackReason;
    modelReady_ = modelResult.ready;

    if (modelReady_) {
      diagnostics_.diff = 0;
      return modelResult.occupiedProbability >= MODEL_OCCUPIED_THRESHOLD;
    }

    return sampleRoiFallback(roiScore);
  }

  bool sampleRoiFallback(uint16_t roiScore) {
    if (!calibrated_) {
      calibrationSum_ += roiScore;
      calibrationFrames_++;
      diagnostics_.diff = 0;
      if (calibrationFrames_ >= PRESENCE_CALIBRATION_FRAMES) {
        baseline_ = calibrationSum_ / calibrationFrames_;
        calibrated_ = true;
      }
      return false;
    }

    const uint16_t diff = abs(static_cast<int32_t>(roiScore) - baseline_);
    diagnostics_.diff = diff;
    return diff >= ROI_DIFF_THRESHOLD;
  }

  uint16_t calculateRoiScore(const camera_fb_t *frame) const {
    const size_t width = frame->width > 0 ? frame->width : 320;
    const size_t height = frame->height > 0 ? frame->height : 240;
    const size_t x0 = width * ROI_X_PERCENT / 100;
    const size_t y0 = height * ROI_Y_PERCENT / 100;
    const size_t x1 = x0 + width * ROI_W_PERCENT / 100;
    const size_t y1 = y0 + height * ROI_H_PERCENT / 100;

    uint32_t sum = 0;
    uint32_t count = 0;
    for (size_t y = y0; y < y1; y += 3) {
      for (size_t x = x0; x < x1; x += 3) {
        sum += frameLumaAt(frame, x, y);
        count++;
      }
    }
    return count == 0 ? 0 : static_cast<uint16_t>(sum / count);
  }

  void buildModelFeatures(const camera_fb_t *frame, int8_t *features, size_t featureCount) const {
    if (featureCount != kFeatureCount) {
      return;
    }

    uint8_t cells[kFeatureCount] = {};
    const size_t width = frame->width > 0 ? frame->width : 320;
    const size_t height = frame->height > 0 ? frame->height : 240;
    const size_t x0 = width * ROI_X_PERCENT / 100;
    const size_t y0 = height * ROI_Y_PERCENT / 100;
    const size_t roiW = width * ROI_W_PERCENT / 100;
    const size_t roiH = height * ROI_H_PERCENT / 100;
    uint32_t totalMean = 0;

    for (size_t gy = 0; gy < 8; ++gy) {
      for (size_t gx = 0; gx < 8; ++gx) {
        const size_t sx0 = x0 + gx * roiW / 8;
        const size_t sy0 = y0 + gy * roiH / 8;
        const size_t sx1 = x0 + (gx + 1) * roiW / 8;
        const size_t sy1 = y0 + (gy + 1) * roiH / 8;
        uint32_t sum = 0;
        uint32_t count = 0;
        for (size_t y = sy0; y < sy1; y += 2) {
          for (size_t x = sx0; x < sx1; x += 2) {
            sum += frameLumaAt(frame, x, y);
            count++;
          }
        }
        const uint8_t mean = count == 0 ? 0 : static_cast<uint8_t>(sum / count);
        cells[gy * 8 + gx] = mean;
        totalMean += mean;
      }
    }

    const int globalMean = static_cast<int>(totalMean / kFeatureCount);
    for (size_t i = 0; i < kFeatureCount; ++i) {
      int centered = (static_cast<int>(cells[i]) - globalMean) * 2;
      centered = std::max(-128, std::min(127, centered));
      features[i] = static_cast<int8_t>(centered);
    }
  }
};

PresenceDetector presenceDetector;

void buzzerBegin() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_1;
  timer.freq_hz = BUZZER_FREQUENCY_HZ;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);

  ledc_channel_config_t channel = {};
  channel.gpio_num = PIN_BUZZER;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_1;
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = LEDC_TIMER_1;
  channel.duty = 0;
  ledc_channel_config(&channel);
}

void buzzerOn() {
  if (buzzerActive) {
    return;
  }
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, BUZZER_FREQUENCY_HZ);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, BUZZER_DUTY);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  buzzerActive = true;
}

void buzzerOff() {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  buzzerActive = false;
}

void startBuzzerSequence(uint32_t nowMs, uint8_t beeps, uint32_t onMs, uint32_t offMs) {
  if (beeps == 0) {
    return;
  }
  buzzerSequence.active = true;
  buzzerSequence.toneOn = true;
  buzzerSequence.remainingPhases = static_cast<uint8_t>(beeps * 2);
  buzzerSequence.nextToggleMs = nowMs + onMs;
  buzzerSequence.onMs = onMs;
  buzzerSequence.offMs = offMs;
  buzzerOn();
}

void showDisplayOverlay(const char *message, uint32_t nowMs, uint32_t durationMs) {
  safeCopy(displayOverlay.message, sizeof(displayOverlay.message), message);
  displayOverlay.untilMs = nowMs + durationMs;
  lastDisplayMs = 0;
}

void resetTimer() {
  timerContext = TimerContext{};
  buzzerOff();
}

uint32_t elapsedSittingMs(uint32_t nowMs) {
  switch (timerContext.state) {
  case TimerState::Idle:
    return 0;
  case TimerState::AwayGrace:
  case TimerState::AwayWarning:
    return timerContext.awayStartMs - timerContext.sitStartMs;
  case TimerState::Sitting:
  case TimerState::Alerting:
    return nowMs - timerContext.sitStartMs;
  }
  return 0;
}

uint32_t remainingSitMs(uint32_t nowMs) {
  const uint32_t elapsed = elapsedSittingMs(nowMs);
  return elapsed >= timerSettings.sitTargetMs ? 0 : timerSettings.sitTargetMs - elapsed;
}

void startSitting(uint32_t nowMs) {
  timerContext.state = TimerState::Sitting;
  timerContext.sitStartMs = nowMs;
  timerContext.awayStartMs = 0;
}

void resumeSittingAfterAway(uint32_t nowMs) {
  timerContext.sitStartMs += nowMs - timerContext.awayStartMs;
  timerContext.awayStartMs = 0;
  timerContext.state = TimerState::Sitting;
}

void updateTimer(bool isPresent, uint32_t nowMs) {
  switch (timerContext.state) {
  case TimerState::Idle:
    if (isPresent) {
      startSitting(nowMs);
    }
    break;
  case TimerState::Sitting:
    if (!isPresent) {
      timerContext.state = TimerState::AwayGrace;
      timerContext.awayStartMs = nowMs;
    } else if (elapsedSittingMs(nowMs) >= timerSettings.sitTargetMs) {
      timerContext.state = TimerState::Alerting;
    }
    break;
  case TimerState::AwayGrace: {
    if (isPresent) {
      resumeSittingAfterAway(nowMs);
      break;
    }
    const uint32_t awayMs = nowMs - timerContext.awayStartMs;
    if (awayMs >= timerSettings.awayResetMs) {
      resetTimer();
    } else if (awayMs >= AWAY_GRACE_MS) {
      timerContext.state = TimerState::AwayWarning;
    }
    break;
  }
  case TimerState::AwayWarning:
    if (isPresent) {
      resumeSittingAfterAway(nowMs);
    } else if (nowMs - timerContext.awayStartMs >= timerSettings.awayResetMs) {
      resetTimer();
    }
    break;
  case TimerState::Alerting:
    if (!isPresent) {
      resetTimer();
    }
    break;
  }
}

bool isPinDown(int pin) {
  return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
}

bool isButtonDown() {
  return isPinDown(PIN_BUTTON);
}

bool isCaptureButtonDown() {
  return isPinDown(PIN_CAPTURE_BUTTON);
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
           stateLabel(timerContext.state),
           diag.present ? "true" : "false",
           diag.rawPresent ? "true" : "false",
           diag.modelReady ? "true" : "false",
           static_cast<double>(diag.modelProbability),
           static_cast<unsigned long>(diag.inferenceMs),
           wifiStartedAsSta ? "sta" : "ap",
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

void handleCaptureButton(uint32_t nowMs) {
  if (!captureButtonPressedEvent) {
    return;
  }
  captureButtonPressedEvent = false;

  char sampleId[48] = {};
  if (captureAndStoreSample("button2", nowMs, sampleId, sizeof(sampleId))) {
    startBuzzerSequence(nowMs, 1, kCaptureBeepOnMs, kCaptureBeepOffMs);
    showDisplayOverlay("CAPTURED", nowMs, kTransientDisplayMs);
    ESP_LOGI(kTag, "sample saved: %s", sampleId);
    return;
  }

  startBuzzerSequence(nowMs, 2, kCaptureBeepOnMs, kCaptureBeepOffMs);
  showDisplayOverlay("SAVE ERR", nowMs, kTransientDisplayMs);
}

void updateBuzzer(uint32_t nowMs) {
  if (buzzerSequence.active) {
    if (static_cast<int32_t>(nowMs - buzzerSequence.nextToggleMs) >= 0) {
      if (buzzerSequence.remainingPhases > 0) {
        buzzerSequence.remainingPhases--;
      }
      if (buzzerSequence.remainingPhases == 0) {
        buzzerSequence.active = false;
        buzzerOff();
      } else {
        buzzerSequence.toneOn = !buzzerSequence.toneOn;
        if (buzzerSequence.toneOn) {
          buzzerOn();
          buzzerSequence.nextToggleMs = nowMs + buzzerSequence.onMs;
        } else {
          buzzerOff();
          buzzerSequence.nextToggleMs = nowMs + buzzerSequence.offMs;
        }
      }
    }
    return;
  }

  if (timerContext.state == TimerState::Alerting) {
    if ((nowMs / 500) % 2 == 0) {
      buzzerOn();
    } else {
      buzzerOff();
    }
    return;
  }

  buzzerOff();
}

void drawDisplay(bool isPresent, uint32_t nowMs) {
  if (nowMs - lastDisplayMs < 250) {
    return;
  }
  lastDisplayMs = nowMs;

  if (displayOverlay.untilMs != 0 && static_cast<int32_t>(displayOverlay.untilMs - nowMs) > 0) {
    display.clear();
    display.textScaled(centeredTextX(displayOverlay.message, 2), 24, 2, displayOverlay.message);
    display.flush();
    return;
  }
  if (displayOverlay.untilMs != 0) {
    displayOverlay.untilMs = 0;
    displayOverlay.message[0] = '\0';
  }

  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  const uint32_t remainingMs = remainingSitMs(nowMs);
  const uint32_t totalSeconds = (remainingMs + 999) / 1000;
  const uint32_t displayMinutes = std::min<uint32_t>(totalSeconds / 60, 99);
  const uint32_t displaySeconds = totalSeconds % 60;
  char timerText[8] = {};
  snprintf(timerText,
           sizeof(timerText),
           "%02lu:%02lu",
           static_cast<unsigned long>(displayMinutes),
           static_cast<unsigned long>(displaySeconds));
  const bool awayState = timerContext.state == TimerState::AwayGrace ||
                         timerContext.state == TimerState::AwayWarning;

  char displayState[12] = {};
  if (awayState) {
    const char *pausedText = (nowMs / 1000) % 2 == 0 ? "PAUSED" : "PAUSED .";
    snprintf(displayState, sizeof(displayState), "%s", pausedText);
  } else if (timerContext.state == TimerState::Alerting && (nowMs / 500) % 2 != 0) {
    displayState[0] = '\0';
  } else {
    snprintf(displayState, sizeof(displayState), "%s", displayStateLabel(timerContext.state, isPresent));
  }

  const uint32_t probabilityPercent = std::min<uint32_t>(
      100,
      static_cast<uint32_t>(diag.modelProbability * 100.0f + 0.5f));
  char probabilityText[16] = {};
  snprintf(probabilityText,
           sizeof(probabilityText),
           "PROB %02lu%%",
           static_cast<unsigned long>(probabilityPercent));

  display.clear();
  display.textScaled(centeredTextX(displayState, 1), 0, 1, displayState);
  display.textScaled(centeredTextX(timerText, kTimerDigitScale), 20, kTimerDigitScale, timerText);
  display.textScaled(centeredTextX(probabilityText, 1), 56, 1, probabilityText);
  display.flush();
}

void logStatus(bool isPresent, uint32_t nowMs) {
  if (nowMs - lastStatusLogMs < 5000) {
    return;
  }
  lastStatusLogMs = nowMs;

  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  ESP_LOGI(kTag,
           "tick state=%s pose=%s raw=%c on=%u off=%u model=%c prob=%u th=%u diff=%u base=%u btn=%c sit=%lu away=%lu",
           stateLabel(timerContext.state),
           isPresent ? "sit" : "away",
           diag.rawPresent ? 'Y' : 'N',
           diag.onFrames,
           diag.offFrames,
           diag.modelReady ? 'Y' : 'N',
           static_cast<unsigned>(diag.modelProbability * 100.0f),
           static_cast<unsigned>(MODEL_OCCUPIED_THRESHOLD * 100.0f),
           diag.diff,
           diag.baseline,
           isButtonDown() ? 'L' : 'H',
           static_cast<unsigned long>(minutesFromMs(timerSettings.sitTargetMs)),
           static_cast<unsigned long>(minutesFromMs(timerSettings.awayResetMs)));
}

[[maybe_unused]] esp_err_t sendIndex(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Camera</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:760px;margin:20px auto;padding:0 14px}.preview{width:min(100%,360px);aspect-ratio:3/4;background:#222;overflow:hidden;display:flex;align-items:center;justify-content:center;margin:auto}"
      "#frame{width:133.333%;height:75%;display:block;transform:rotate(90deg)}"
      "button,a,input{font-size:18px}button,a{padding:10px 14px;margin:6px 6px 6px 0;display:inline-block}"
      "a{color:#8cc8ff}.settings{margin:16px 0;padding:12px;border:1px solid #333;background:#181818}"
      "label{display:block;margin:10px 0 4px}input{box-sizing:border-box;width:100%;padding:10px;background:#222;color:#eee;border:1px solid #555}"
      "#msg{min-height:24px;color:#9fdb9f}</style></head><body><main>"
      "<h2>Bell Robot Camera</h2><div class='preview'><img id='frame' src='/stream'></div>"
      "<p><button onclick='refreshFrame()'>Refresh</button><a href='/status'>Status</a><a href='/reset'>Reset</a><a href='/samples'>Samples</a></p>"
      "<form class='settings' onsubmit='saveSettings(event)'>"
      "<label for='sit'>倒计时（分钟）</label><input id='sit' name='sit_minutes' type='number' min='1' max='180' step='1' required>"
      "<label for='away'>离场容忍（分钟）</label><input id='away' name='away_minutes' type='number' min='1' max='5' step='1' required>"
      "<button type='submit'>保存设置</button><span id='msg'></span></form>"
      "<p><a href='/label?class=absent'>Save absent sample</a><a href='/label?class=seated'>Save seated sample</a></p>"
      "<script>function refreshFrame(){document.getElementById('frame').src='/stream?ts='+Date.now()}"
      "async function loadSettings(){let r=await fetch('/settings');let s=await r.json();document.getElementById('sit').value=s.sit_minutes;document.getElementById('away').value=s.away_minutes}"
      "async function saveSettings(e){e.preventDefault();let sit=document.getElementById('sit'),away=document.getElementById('away'),msg=document.getElementById('msg');msg.textContent='Saving...';"
      "let b='sit_minutes='+encodeURIComponent(sit.value)+'&away_minutes='+encodeURIComponent(away.value);"
      "let r=await fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});"
      "msg.textContent=r.ok?'Saved':'Save failed'}"
      "loadSettings()</script></main></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendIndexCloud(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Camera</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:760px;margin:20px auto;padding:0 14px}.preview{width:min(100%,360px);aspect-ratio:3/4;background:#222;overflow:hidden;display:flex;align-items:center;justify-content:center;margin:auto}"
      "#frame{width:133.333%;height:75%;display:block;transform:rotate(90deg)}"
      "button,a,input{font-size:18px}button,a{padding:10px 14px;margin:6px 6px 6px 0;display:inline-block}"
      "a{color:#8cc8ff}.settings{margin:16px 0;padding:12px;border:1px solid #333;background:#181818}"
      "label{display:block;margin:10px 0 4px}input{box-sizing:border-box;width:100%;padding:10px;background:#222;color:#eee;border:1px solid #555}"
      ".hint{color:#aaa;font-size:14px}#msg,#cloudMsg{min-height:24px;color:#9fdb9f}</style></head><body><main>"
      "<h2>Bell Robot Camera</h2><div class='preview'><img id='frame' src='/stream'></div>"
      "<p><button onclick='refreshFrame()'>Refresh</button><a href='/status'>Status</a><a href='/reset'>Reset</a><a href='/samples'>Samples</a></p>"
      "<form class='settings' onsubmit='saveSettings(event)'>"
      "<label for='sit'>Timer minutes</label><input id='sit' name='sit_minutes' type='number' min='1' max='180' step='1' required>"
      "<label for='away'>Away tolerance minutes</label><input id='away' name='away_minutes' type='number' min='1' max='5' step='1' required>"
      "<button type='submit'>Save timer</button><span id='msg'></span></form>"
      "<form class='settings' onsubmit='saveCloud(event)'>"
      "<h3>Cloud remote access</h3><p class='hint'>Only Wi-Fi is required. Device ID and token are generated automatically.</p>"
      "<label for='ssid'>2.4G Wi-Fi SSID</label><input id='ssid' name='ssid' maxlength='32' required>"
      "<label for='pass'>Wi-Fi password</label><input id='pass' name='password' type='password' maxlength='64'>"
      "<label for='server'>Server URL</label><input id='server' name='server_url' placeholder='https://your-domain.example' maxlength='159' required>"
      "<p class='hint'>Device ID: <span id='device'>-</span></p>"
      "<button type='submit'>Save cloud</button><button type='button' onclick='forgetCloud()'>Forget cloud</button><span id='cloudMsg'></span></form>"
      "<p><a href='/label?class=absent'>Save absent sample</a><a href='/label?class=seated'>Save seated sample</a></p>"
      "<script>function refreshFrame(){document.getElementById('frame').src='/stream?ts='+Date.now()}"
      "async function loadSettings(){let r=await fetch('/settings');let s=await r.json();document.getElementById('sit').value=s.sit_minutes;document.getElementById('away').value=s.away_minutes}"
      "async function loadCloud(){let r=await fetch('/cloud');let s=await r.json();document.getElementById('ssid').value=s.ssid||'';document.getElementById('server').value=s.server_url||'';document.getElementById('device').textContent=s.device_id||'-'}"
      "async function saveSettings(e){e.preventDefault();let sit=document.getElementById('sit'),away=document.getElementById('away'),msg=document.getElementById('msg');msg.textContent='Saving...';"
      "let b='sit_minutes='+encodeURIComponent(sit.value)+'&away_minutes='+encodeURIComponent(away.value);"
      "let r=await fetch('/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});"
      "msg.textContent=r.ok?'Saved':'Save failed'}"
      "async function saveCloud(e){e.preventDefault();let ids=['ssid','pass','server'],p=new URLSearchParams(),msg=document.getElementById('cloudMsg');ids.forEach(id=>p.append(document.getElementById(id).name,document.getElementById(id).value));msg.textContent='Saving...';let r=await fetch('/cloud',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});let t=await r.text();msg.textContent=r.ok?'Saved, rebooting...':('Save failed: '+t)}"
      "async function forgetCloud(){let msg=document.getElementById('cloudMsg');msg.textContent='Clearing...';let r=await fetch('/cloud/forget',{method:'POST'});let t=await r.text();msg.textContent=r.ok?'Cleared, rebooting...':('Clear failed: '+t)}"
      "loadSettings();loadCloud()</script></main></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendSamplesPage(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Samples</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:980px;margin:20px auto;padding:0 14px}button,a{font-size:16px;padding:10px 14px;margin:6px 6px 6px 0;display:inline-block}"
      "a{color:#8cc8ff}.toolbar{margin-bottom:12px}.hint{color:#aaa}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px}"
      ".card{border:1px solid #333;background:#181818;padding:12px}.card img{width:100%;height:auto;background:#222;display:block}"
      ".meta{font-size:13px;line-height:1.5;color:#cfcfcf;word-break:break-word}.status{min-height:24px;color:#9fdb9f}</style></head><body><main>"
      "<h2>Bell Robot Samples</h2><p class='hint'>Second button and the debug capture endpoint both save JPEG + metadata into device storage.</p>"
      "<div class='toolbar'><a href='/'>Back</a><button onclick='captureSample()'>Capture Now</button><button onclick='clearSamples()'>Clear All</button><span id='status' class='status'></span></div>"
      "<div id='summary' class='hint'></div><div id='grid' class='grid'></div>"
      "<script>"
      "async function loadSamples(){const status=document.getElementById('status');const summary=document.getElementById('summary');const grid=document.getElementById('grid');status.textContent='Loading...';"
      "const r=await fetch('/samples/list');if(!r.ok){status.textContent='Load failed';return;}const data=await r.json();summary.textContent='Stored samples: '+data.count;grid.innerHTML='';"
      "if(!data.items||data.items.length===0){grid.innerHTML='<div class=\"hint\">No samples yet.</div>';status.textContent='';return;}"
      "for(const item of data.items){const card=document.createElement('div');card.className='card';"
      "card.innerHTML='<img alt=\"sample\" src=\"'+item.preview_url+'\">'+"
      "'<p><strong>'+item.id+'</strong></p>'+"
      "'<div class=\"meta\">state: '+item.state+'<br>present: '+item.present+'<br>model_prob: '+item.model_prob+'<br>wifi_mode: '+item.wifi_mode+'<br>boot_ms: '+item.boot_ms+'<br>jpeg_bytes: '+item.jpeg_bytes+'</div>'+"
      "'<p><a href=\"'+item.download_url+'\" download>Download JPG</a><a href=\"'+item.meta_url+'\" target=\"_blank\">Metadata</a></p>';"
      "grid.appendChild(card);}status.textContent='';}"
      "async function captureSample(){const status=document.getElementById('status');status.textContent='Capturing...';const r=await fetch('/samples/capture',{method:'POST'});"
      "if(!r.ok){status.textContent='Capture failed';return;}const data=await r.json();status.textContent='Saved '+(data.id||'sample');loadSamples();}"
      "async function clearSamples(){const status=document.getElementById('status');if(!confirm('Clear all stored samples?')){return;}status.textContent='Clearing...';"
      "const r=await fetch('/samples/clear',{method:'POST'});status.textContent=r.ok?'Cleared':'Clear failed';if(r.ok){loadSamples();}}"
      "loadSamples();</script></main></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

bool captureFrameAsJpeg(CapturedCameraFrame *capture,
                        uint8_t **outJpgBuffer,
                        size_t *outJpgLength,
                        bool *outConverted,
                        uint8_t jpegQuality,
                        framesize_t frameSize) {
  if (capture == nullptr || outJpgBuffer == nullptr || outJpgLength == nullptr || outConverted == nullptr) {
    return false;
  }

  if (!captureCameraFrame(capture, frameSize)) {
    return false;
  }
  camera_fb_t *frame = &capture->logicalFrame;

  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  bool converted = false;
  if (frame->format == PIXFORMAT_JPEG) {
    jpgBuffer = frame->buf;
    jpgLength = frame->len;
  } else {
    converted = frame2jpg(frame, jpegQuality, &jpgBuffer, &jpgLength);
    if (!converted) {
      releaseCameraFrame(capture);
      return false;
    }
  }

  *outJpgBuffer = jpgBuffer;
  *outJpgLength = jpgLength;
  *outConverted = converted;
  return true;
}

void releaseCapturedJpeg(CapturedCameraFrame *capture, uint8_t *jpgBuffer, bool converted) {
  if (converted && jpgBuffer != nullptr) {
    free(jpgBuffer);
  }
  releaseCameraFrame(capture);
}

esp_err_t sendCapture(httpd_req_t *req) {
  CapturedCameraFrame capture = {};
  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  bool converted = false;
  if (!captureFrameAsJpeg(&capture, &jpgBuffer, &jpgLength, &converted)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera capture failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t err = httpd_resp_send(req, reinterpret_cast<const char *>(jpgBuffer), jpgLength);
  releaseCapturedJpeg(&capture, jpgBuffer, converted);
  return err;
}

esp_err_t sendStream(httpd_req_t *req) {
  static constexpr char boundary[] = "bellrobot";
  char frameHeader[96] = {};

  ESP_LOGI(kTag, "camera stream connected");
  httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=bellrobot");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Connection", "close");

  uint32_t logFrameCount = 0;
  uint32_t logStartMs = millis32();
  while (true) {
    CapturedCameraFrame capture = {};
    if (!lockCamera(&capture)) {
      ESP_LOGW(kTag, "camera stream lock timeout");
      return ESP_FAIL;
    }
    if (!setCameraFrameSize(kCameraFrameSize)) {
      releaseCameraFrame(&capture);
      return ESP_FAIL;
    }

    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      ESP_LOGW(kTag, "camera stream capture failed");
      releaseCameraFrame(&capture);
      return ESP_FAIL;
    }
    capture.rawFrame = frame;

    if (frame->format != PIXFORMAT_JPEG) {
      ESP_LOGW(kTag, "camera stream expected JPEG frame, got format=%d", static_cast<int>(frame->format));
      releaseCameraFrame(&capture);
      return ESP_FAIL;
    }
    const size_t frameLength = frame->len;
    const uint16_t frameWidth = frame->width;
    const uint16_t frameHeight = frame->height;

    const int headerLength = snprintf(frameHeader,
                                      sizeof(frameHeader),
                                      "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                      boundary,
                                      static_cast<unsigned>(frameLength));
    esp_err_t err = headerLength > 0 && static_cast<size_t>(headerLength) < sizeof(frameHeader)
                        ? httpd_resp_send_chunk(req, frameHeader, headerLength)
                        : ESP_FAIL;
    if (err == ESP_OK) {
      err = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(frame->buf), frameLength);
    }

    releaseCameraFrame(&capture);
    if (err != ESP_OK) {
      ESP_LOGI(kTag, "camera stream disconnected: %s", esp_err_to_name(err));
      return ESP_OK;
    }

    logFrameCount++;
    if (logFrameCount >= kCameraStreamLogFrames) {
      const uint32_t nowMs = millis32();
      const uint32_t elapsedMs = nowMs - logStartMs;
      const float fps = elapsedMs == 0 ? 0.0f : (static_cast<float>(logFrameCount) * 1000.0f) / elapsedMs;
      ESP_LOGI(kTag,
               "camera stream fps=%.1f jpeg=%u bytes frame=%ux%u",
               static_cast<double>(fps),
               static_cast<unsigned>(frameLength),
               static_cast<unsigned>(frameWidth),
               static_cast<unsigned>(frameHeight));
      logFrameCount = 0;
      logStartMs = nowMs;
    }

    vTaskDelay(pdMS_TO_TICKS(kCameraStreamFrameDelayMs));
  }
}

esp_err_t sendSettings(httpd_req_t *req) {
  char payload[96] = {};
  snprintf(payload,
           sizeof(payload),
           "{\"sit_minutes\":%lu,\"away_minutes\":%lu}",
           static_cast<unsigned long>(minutesFromMs(timerSettings.sitTargetMs)),
           static_cast<unsigned long>(minutesFromMs(timerSettings.awayResetMs)));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
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
           stateLabel(timerContext.state),
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
           static_cast<unsigned long>(minutesFromMs(timerSettings.sitTargetMs)),
           static_cast<unsigned long>(minutesFromMs(timerSettings.awayResetMs)),
           CAMERA_ROTATE_CW_90 ? "cw90" : "none",
           ENABLE_CLOUD_REMOTE ? "true" : "false",
           cloudSettings.deviceId,
           wifiStartedAsSta ? "sta" : "ap",
           (wifiEventGroup != nullptr &&
            (xEventGroupGetBits(wifiEventGroup) & kWifiConnectedBit)) ? "true" : "false",
           cloudSettingsComplete() ? "true" : "false",
           static_cast<unsigned long>(cloudLastPollMs),
           static_cast<unsigned long>(cloudLastSuccessMs),
           cloudLastError);
}

bool parseUnsignedStrict(const char *value, uint32_t *out) {
  if (value == nullptr || *value == '\0' || out == nullptr) {
    return false;
  }
  char *end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || *end != '\0' || parsed > UINT32_MAX) {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

void formUrlDecodeInPlace(char *value) {
  if (value == nullptr) {
    return;
  }
  char *read = value;
  char *write = value;
  while (*read != '\0') {
    if (*read == '+') {
      *write++ = ' ';
      read++;
      continue;
    }
    if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
      const int hi = hexValue(read[1]);
      const int lo = hexValue(read[2]);
      if (hi >= 0 && lo >= 0) {
        *write++ = static_cast<char>((hi << 4) | lo);
        read += 3;
        continue;
      }
    }
    *write++ = *read++;
  }
  *write = '\0';
}

void trimInPlace(char *value) {
  if (value == nullptr) {
    return;
  }
  char *start = value;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
    start++;
  }
  char *end = start + strlen(start);
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
    end--;
  }
  const size_t length = static_cast<size_t>(end - start);
  memmove(value, start, length);
  value[length] = '\0';
}

bool readFormValue(const char *body, const char *key, char *out, size_t outSize) {
  if (body == nullptr || key == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  const size_t keyLength = strlen(key);
  const char *cursor = body;
  while (*cursor != '\0') {
    const char *pairEnd = strchr(cursor, '&');
    const size_t pairLength = pairEnd == nullptr ? strlen(cursor) : static_cast<size_t>(pairEnd - cursor);
    const char *equals = static_cast<const char *>(memchr(cursor, '=', pairLength));
    if (equals != nullptr && static_cast<size_t>(equals - cursor) == keyLength &&
        strncmp(cursor, key, keyLength) == 0) {
      const char *valueStart = equals + 1;
      const size_t encodedLength = pairLength - keyLength - 1;
      const size_t copyLength = std::min(encodedLength, outSize - 1);
      memcpy(out, valueStart, copyLength);
      out[copyLength] = '\0';
      formUrlDecodeInPlace(out);
      trimInPlace(out);
      return true;
    }
    if (pairEnd == nullptr) {
      break;
    }
    cursor = pairEnd + 1;
  }
  return false;
}

esp_err_t readPostBody(httpd_req_t *req, char *buffer, size_t bufferSize) {
  if (req->content_len == 0 || req->content_len >= bufferSize) {
    return ESP_FAIL;
  }

  size_t received = 0;
  while (received < req->content_len) {
    const int read = httpd_req_recv(req,
                                    buffer + received,
                                    req->content_len - received);
    if (read <= 0) {
      return ESP_FAIL;
    }
    received += read;
  }
  buffer[received] = '\0';
  return ESP_OK;
}

esp_err_t handleSettingsPost(httpd_req_t *req) {
  char body[96] = {};
  if (readPostBody(req, body, sizeof(body)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid settings body");
    return ESP_FAIL;
  }

  char sitValue[16] = {};
  char awayValue[16] = {};
  if (httpd_query_key_value(body, "sit_minutes", sitValue, sizeof(sitValue)) != ESP_OK ||
      httpd_query_key_value(body, "away_minutes", awayValue, sizeof(awayValue)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing timer settings");
    return ESP_FAIL;
  }

  uint32_t sitMinutes = 0;
  uint32_t awayMinutes = 0;
  if (!parseUnsignedStrict(sitValue, &sitMinutes) ||
      !parseUnsignedStrict(awayValue, &awayMinutes) ||
      !validTimerMinutes(sitMinutes, awayMinutes)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "settings out of range");
    return ESP_FAIL;
  }

  const esp_err_t err = saveTimerSettings(sitMinutes, awayMinutes);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "save settings failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save settings failed");
    return ESP_FAIL;
  }
  return sendSettings(req);
}

esp_err_t sendCloudSettings(httpd_req_t *req) {
  char payload[384] = {};
  snprintf(payload,
           sizeof(payload),
           "{\"ssid\":\"%s\",\"server_url\":\"%s\",\"device_id\":\"%s\","
           "\"token_set\":%s,\"configured\":%s,\"wifi_mode\":\"%s\"}",
           cloudSettings.ssid,
           cloudSettings.serverUrl,
           cloudSettings.deviceId,
           cloudSettings.token[0] == '\0' ? "false" : "true",
           cloudSettingsComplete() ? "true" : "false",
           wifiStartedAsSta ? "sta" : "ap");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleCloudPost(httpd_req_t *req) {
  char body[640] = {};
  if (readPostBody(req, body, sizeof(body)) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid cloud body");
    return ESP_FAIL;
  }

  CloudSettings next = cloudSettings;
  char value[180] = {};
  if (readFormValue(body, "ssid", value, sizeof(value))) {
    safeCopy(next.ssid, sizeof(next.ssid), value);
  }
  if (readFormValue(body, "password", value, sizeof(value))) {
    safeCopy(next.password, sizeof(next.password), value);
  }
  if (readFormValue(body, "server_url", value, sizeof(value))) {
    safeCopy(next.serverUrl, sizeof(next.serverUrl), value);
  }

  const esp_err_t err = saveCloudSettings(next);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "save cloud settings failed: %s", esp_err_to_name(err));
    if (next.ssid[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
    } else if (next.serverUrl[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing server url");
    } else if (!validServerUrl(next.serverUrl)) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "server url must start with http:// or https://");
    } else if (next.deviceId[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing device id");
    } else if (next.token[0] == '\0') {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing device token");
    } else {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid cloud settings");
    }
    return ESP_FAIL;
  }
  rebootAtMs = millis32() + 2000;
  return sendCloudSettings(req);
}

esp_err_t handleCloudForget(httpd_req_t *req) {
  const esp_err_t err = forgetCloudSettings();
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "forget cloud failed");
    return ESP_FAIL;
  }
  rebootAtMs = millis32() + 2000;
  return httpd_resp_send(req, "forget ok", HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendStatus(httpd_req_t *req) {
  char payload[1024] = {};
  buildStatusPayload(payload, sizeof(payload));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleReset(httpd_req_t *req) {
  resetTimer();
  presenceDetector.recalibrate();
  return httpd_resp_send(req, "reset ok", HTTPD_RESP_USE_STRLEN);
}

const char *queryClass(httpd_req_t *req, char *buffer, size_t bufferSize) {
  if (httpd_req_get_url_query_str(req, buffer, bufferSize) != ESP_OK) {
    return nullptr;
  }
  static char classValue[16] = {};
  if (httpd_query_key_value(buffer, "class", classValue, sizeof(classValue)) != ESP_OK) {
    return nullptr;
  }
  if (strcmp(classValue, "empty") == 0) {
    strcpy(classValue, "absent");
  } else if (strcmp(classValue, "occupied") == 0) {
    strcpy(classValue, "seated");
  }
  if (strcmp(classValue, "absent") != 0 && strcmp(classValue, "seated") != 0) {
    return nullptr;
  }
  return classValue;
}

esp_err_t handleLabel(httpd_req_t *req) {
  char query[64] = {};
  const char *label = queryClass(req, query, sizeof(query));
  if (label == nullptr) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "use /label?class=absent or /label?class=seated");
    return ESP_FAIL;
  }

  CapturedCameraFrame capture = {};
  if (!captureCameraFrame(&capture)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera capture failed");
    return ESP_FAIL;
  }
  const camera_fb_t *frame = &capture.logicalFrame;

  int8_t features[kFeatureCount] = {};
  presenceDetector.exportNormalizedFeatures(frame, features, kFeatureCount);

  char filename[80] = {};
  snprintf(filename, sizeof(filename), "attachment; filename=\"%s_%lu.pgm\"",
           label,
           static_cast<unsigned long>(++sampleCounter));

  char header[64] = {};
  const int headerLength = snprintf(header, sizeof(header), "P5\n8 8\n255\n");
  uint8_t pixels[kFeatureCount] = {};
  for (size_t i = 0; i < kFeatureCount; ++i) {
    pixels[i] = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(features[i]) + 128)));
  }

  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Content-Disposition", filename);
  httpd_resp_send_chunk(req, header, headerLength);
  httpd_resp_send_chunk(req, reinterpret_cast<const char *>(pixels), sizeof(pixels));
  httpd_resp_send_chunk(req, nullptr, 0);
  releaseCameraFrame(&capture);
  return ESP_OK;
}

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  config.max_uri_handlers = 20;
  ESP_ERROR_CHECK(httpd_start(&httpServer, &config));

  httpd_uri_t index = {};
  index.uri = "/";
  index.method = HTTP_GET;
  index.handler = ENABLE_CLOUD_REMOTE ? sendIndexCloud : sendIndex;

  httpd_uri_t capture = {};
  capture.uri = "/capture";
  capture.method = HTTP_GET;
  capture.handler = sendCapture;

  httpd_uri_t stream = {};
  stream.uri = "/stream";
  stream.method = HTTP_GET;
  stream.handler = sendStream;

  httpd_uri_t status = {};
  status.uri = "/status";
  status.method = HTTP_GET;
  status.handler = sendStatus;

  httpd_uri_t reset = {};
  reset.uri = "/reset";
  reset.method = HTTP_GET;
  reset.handler = handleReset;

  httpd_uri_t settingsGet = {};
  settingsGet.uri = "/settings";
  settingsGet.method = HTTP_GET;
  settingsGet.handler = sendSettings;

  httpd_uri_t settingsPost = {};
  settingsPost.uri = "/settings";
  settingsPost.method = HTTP_POST;
  settingsPost.handler = handleSettingsPost;

  httpd_uri_t label = {};
  label.uri = "/label";
  label.method = HTTP_GET;
  label.handler = handleLabel;

  httpd_uri_t samplesPage = {};
  samplesPage.uri = "/samples";
  samplesPage.method = HTTP_GET;
  samplesPage.handler = sendSamplesPage;

  httpd_uri_t samplesList = {};
  samplesList.uri = "/samples/list";
  samplesList.method = HTTP_GET;
  samplesList.handler = sendSamplesList;

  httpd_uri_t sampleFile = {};
  sampleFile.uri = "/samples/file";
  sampleFile.method = HTTP_GET;
  sampleFile.handler = sendSampleFile;

  httpd_uri_t sampleMeta = {};
  sampleMeta.uri = "/samples/meta";
  sampleMeta.method = HTTP_GET;
  sampleMeta.handler = sendSampleMetadata;

  httpd_uri_t samplesClear = {};
  samplesClear.uri = "/samples/clear";
  samplesClear.method = HTTP_POST;
  samplesClear.handler = handleSamplesClear;

  httpd_uri_t samplesCapture = {};
  samplesCapture.uri = "/samples/capture";
  samplesCapture.method = HTTP_POST;
  samplesCapture.handler = handleSampleCapture;

  httpd_uri_t cloudGet = {};
  cloudGet.uri = "/cloud";
  cloudGet.method = HTTP_GET;
  cloudGet.handler = sendCloudSettings;

  httpd_uri_t cloudPost = {};
  cloudPost.uri = "/cloud";
  cloudPost.method = HTTP_POST;
  cloudPost.handler = handleCloudPost;

  httpd_uri_t cloudForget = {};
  cloudForget.uri = "/cloud/forget";
  cloudForget.method = HTTP_POST;
  cloudForget.handler = handleCloudForget;

  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &index));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &capture));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &stream));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &status));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &reset));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &settingsGet));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &settingsPost));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &label));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesPage));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesList));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &sampleFile));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &sampleMeta));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesClear));
  ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &samplesCapture));
  if (ENABLE_CLOUD_REMOTE) {
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &cloudGet));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &cloudPost));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpServer, &cloudForget));
  }
}

void wifiEventHandler(void *arg, esp_event_base_t eventBase, int32_t eventId, void *eventData) {
  (void)arg;
  if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
    if (wifiEventGroup != nullptr) {
      xEventGroupClearBits(wifiEventGroup, kWifiConnectedBit);
    }
    if (wifiStartedAsSta) {
      esp_wifi_connect();
    }
  } else if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
    if (wifiEventGroup != nullptr) {
      xEventGroupSetBits(wifiEventGroup, kWifiConnectedBit);
    }
    const ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(eventData);
    ESP_LOGI(kTag, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));
  }
}

void initWifiDriver() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifiEventGroup = xEventGroupCreate();
  staNetif = esp_netif_create_default_wifi_sta();
  apNetif = esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                       ESP_EVENT_ANY_ID,
                                                       &wifiEventHandler,
                                                       nullptr,
                                                       nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                       IP_EVENT_STA_GOT_IP,
                                                       &wifiEventHandler,
                                                       nullptr,
                                                       nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void configureApIp() {
  if (apNetif == nullptr) {
    return;
  }
  esp_netif_ip_info_t ip = {};
  IP4_ADDR(&ip.ip, CAMERA_PREVIEW_AP_IP_0, CAMERA_PREVIEW_AP_IP_1, CAMERA_PREVIEW_AP_IP_2, CAMERA_PREVIEW_AP_IP_3);
  IP4_ADDR(&ip.gw, CAMERA_PREVIEW_AP_IP_0, CAMERA_PREVIEW_AP_IP_1, CAMERA_PREVIEW_AP_IP_2, CAMERA_PREVIEW_AP_IP_3);
  IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
  ESP_ERROR_CHECK(esp_netif_dhcps_stop(apNetif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(apNetif, &ip));
  ESP_ERROR_CHECK(esp_netif_dhcps_start(apNetif));
}

void startWifiApOnly() {
  configureApIp();

  wifi_config_t wifiConfig = {};
  strncpy(reinterpret_cast<char *>(wifiConfig.ap.ssid), CAMERA_PREVIEW_AP_SSID, sizeof(wifiConfig.ap.ssid));
  strncpy(reinterpret_cast<char *>(wifiConfig.ap.password), CAMERA_PREVIEW_AP_PASSWORD, sizeof(wifiConfig.ap.password));
  wifiConfig.ap.ssid_len = strlen(CAMERA_PREVIEW_AP_SSID);
  wifiConfig.ap.channel = 1;
  wifiConfig.ap.max_connection = 4;
  wifiConfig.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  if (strlen(CAMERA_PREVIEW_AP_PASSWORD) == 0) {
    wifiConfig.ap.authmode = WIFI_AUTH_OPEN;
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifiConfig));
  ESP_ERROR_CHECK(esp_wifi_start());
  wifiStartedAsAp = true;
  wifiStartedAsSta = false;
  ESP_LOGI(kTag, "AP started: %s / http://192.168.4.1/", CAMERA_PREVIEW_AP_SSID);
}

bool startWifiStaOnly() {
  wifi_config_t wifiConfig = {};
  safeCopy(reinterpret_cast<char *>(wifiConfig.sta.ssid), sizeof(wifiConfig.sta.ssid), cloudSettings.ssid);
  safeCopy(reinterpret_cast<char *>(wifiConfig.sta.password), sizeof(wifiConfig.sta.password), cloudSettings.password);
  wifiConfig.sta.threshold.authmode = WIFI_AUTH_OPEN;
  wifiConfig.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

  if (wifiEventGroup != nullptr) {
    xEventGroupClearBits(wifiEventGroup, kWifiConnectedBit);
  }

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));
  wifiStartedAsSta = true;
  wifiStartedAsAp = false;
  ESP_ERROR_CHECK(esp_wifi_start());

  const EventBits_t bits = xEventGroupWaitBits(wifiEventGroup,
                                               kWifiConnectedBit,
                                               pdFALSE,
                                               pdTRUE,
                                               pdMS_TO_TICKS(kStaConnectTimeoutMs));
  if ((bits & kWifiConnectedBit) != 0) {
    setCloudError("wifi_connected");
    return true;
  }

  ESP_LOGW(kTag, "STA connect timeout, fallback to AP");
  setCloudError("wifi_timeout");
  esp_wifi_stop();
  wifiStartedAsSta = false;
  return false;
}

void startWifi() {
  initWifiDriver();
  if (!ENABLE_CLOUD_REMOTE) {
    setCloudError("disabled");
    startWifiApOnly();
    return;
  }
  if (cloudSettingsComplete() && startWifiStaOnly()) {
    ESP_LOGI(kTag, "STA mode active, cloud server: %s", cloudSettings.serverUrl);
    return;
  }
  startWifiApOnly();
}

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

const char *querySampleId(httpd_req_t *req, char *buffer, size_t bufferSize) {
  if (httpd_req_get_url_query_str(req, buffer, bufferSize) != ESP_OK) {
    return nullptr;
  }
  static char idValue[64] = {};
  if (httpd_query_key_value(buffer, "id", idValue, sizeof(idValue)) != ESP_OK) {
    return nullptr;
  }
  for (char *cursor = idValue; *cursor != '\0'; ++cursor) {
    const char c = *cursor;
    const bool valid = (c >= 'a' && c <= 'z') ||
                       (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') ||
                       c == '_' ||
                       c == '-';
    if (!valid) {
      return nullptr;
    }
  }
  return idValue;
}

esp_err_t sendSamplesList(httpd_req_t *req) {
  std::vector<std::string> ids;
  const esp_err_t listErr = sample_store::listIds(&ids);
  if (listErr != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sample list failed");
    return ESP_FAIL;
  }

  std::string payload;
  payload.reserve(16384);
  payload += "{\"count\":";
  payload += std::to_string(ids.size());
  payload += ",\"items\":[";

  bool first = true;
  for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
    std::string metadata;
    if (sample_store::loadMetadata(it->c_str(), &metadata) != ESP_OK) {
      continue;
    }

    char state[16] = {};
    char wifiMode[8] = {};
    bool present = false;
    uint32_t bootMs = 0;
    uint32_t jpegBytes = 0;
    float modelProb = 0.0f;
    jsonFindString(metadata.c_str(), "state", state, sizeof(state));
    jsonFindString(metadata.c_str(), "wifi_mode", wifiMode, sizeof(wifiMode));
    jsonFindBool(metadata.c_str(), "present", &present);
    jsonFindUint(metadata.c_str(), "boot_ms", &bootMs);
    jsonFindUint(metadata.c_str(), "jpeg_bytes", &jpegBytes);
    jsonFindFloat(metadata.c_str(), "model_prob", &modelProb);

    char item[512] = {};
    snprintf(item,
             sizeof(item),
             "%s{\"id\":\"%s\",\"state\":\"%s\",\"present\":%s,"
             "\"model_prob\":%.3f,\"wifi_mode\":\"%s\",\"boot_ms\":%lu,"
             "\"jpeg_bytes\":%lu,\"preview_url\":\"/samples/file?id=%s\","
             "\"download_url\":\"/samples/file?id=%s\",\"meta_url\":\"/samples/meta?id=%s\"}",
             first ? "" : ",",
             it->c_str(),
             state[0] == '\0' ? "-" : state,
             present ? "true" : "false",
             static_cast<double>(modelProb),
             wifiMode[0] == '\0' ? "-" : wifiMode,
             static_cast<unsigned long>(bootMs),
             static_cast<unsigned long>(jpegBytes),
             it->c_str(),
             it->c_str(),
             it->c_str());
    payload += item;
    first = false;
  }
  payload += "]}";

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, payload.c_str(), payload.size());
}

esp_err_t sendSampleFile(httpd_req_t *req) {
  char query[96] = {};
  const char *id = querySampleId(req, query, sizeof(query));
  if (id == nullptr) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
    return ESP_FAIL;
  }

  std::vector<uint8_t> jpegData;
  const esp_err_t err = sample_store::loadJpeg(id, &jpegData);
  if (err == ESP_ERR_NOT_FOUND) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "sample not found");
    return ESP_FAIL;
  }
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sample read failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req,
                         reinterpret_cast<const char *>(jpegData.data()),
                         jpegData.size());
}

esp_err_t sendSampleMetadata(httpd_req_t *req) {
  char query[96] = {};
  const char *id = querySampleId(req, query, sizeof(query));
  if (id == nullptr) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing id");
    return ESP_FAIL;
  }

  std::string metadata;
  const esp_err_t err = sample_store::loadMetadata(id, &metadata);
  if (err == ESP_ERR_NOT_FOUND) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "sample not found");
    return ESP_FAIL;
  }
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "metadata read failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, metadata.c_str(), metadata.size());
}

esp_err_t handleSamplesClear(httpd_req_t *req) {
  const esp_err_t err = sample_store::clear();
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sample clear failed");
    return ESP_FAIL;
  }
  return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handleSampleCapture(httpd_req_t *req) {
  const uint32_t nowMs = millis32();
  char sampleId[48] = {};
  if (!captureAndStoreSample("button2", nowMs, sampleId, sizeof(sampleId))) {
    startBuzzerSequence(nowMs, 2, kCaptureBeepOnMs, kCaptureBeepOffMs);
    showDisplayOverlay("SAVE ERR", nowMs, kTransientDisplayMs);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
    return ESP_FAIL;
  }

  startBuzzerSequence(nowMs, 1, kCaptureBeepOnMs, kCaptureBeepOffMs);
  showDisplayOverlay("CAPTURED", nowMs, kTransientDisplayMs);

  char payload[96] = {};
  snprintf(payload, sizeof(payload), "{\"ok\":true,\"id\":\"%s\"}", sampleId);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
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
    if (!cloudSettingsComplete() || !wifiStartedAsSta || wifiEventGroup == nullptr ||
        (xEventGroupGetBits(wifiEventGroup) & kWifiConnectedBit) == 0) {
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
      cloudFirstFailureMs = 0;
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
        wifiStartedAsSta = false;
        cloudFirstFailureMs = 0;
        startWifiApOnly();
      }
    }
  }
}

void setupButton() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << PIN_BUTTON) | (1ULL << PIN_CAPTURE_BUTTON);
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&io);
  lastButtonLevel = !isButtonDown();
  lastCaptureButtonLevel = !isCaptureButtonDown();
}
} // namespace

extern "C" void app_main(void) {
  normalizeNvsInit();
  loadTimerSettings();
  if (ENABLE_CLOUD_REMOTE) {
    loadCloudSettings();
  } else {
    setCloudError("disabled");
  }
  buzzerBegin();
  setupButton();
  ESP_ERROR_CHECK(sample_store::init());
  display.begin();
  runOledDiagnostics();
  display.text(0, 0, "BELL");
  display.text(0, 1, "START");
  display.flush();

  const bool cameraOk = presenceDetector.begin();
  if (!cameraOk) {
    display.clear();
    display.text(0, 0, "Camera failed");
    display.text(0, 1, "Check pins");
    display.flush();
  }

  startWifi();
  startWebServer();
  if (ENABLE_CLOUD_REMOTE) {
    xTaskCreate(cloudPollTask, "cloud_poll", 8192, nullptr, 5, nullptr);
  }

  ESP_LOGI(kTag, "ready. local URL: http://192.168.4.1/ when AP is active");
  while (true) {
    const uint32_t nowMs = millis32();
    if (rebootAtMs != 0 && static_cast<int32_t>(nowMs - rebootAtMs) >= 0) {
      esp_restart();
    }
    const bool isPresent = presenceDetector.update(nowMs);
    updateButton(nowMs);
    updateCaptureButton(nowMs);
    handleButton();
    handleCaptureButton(nowMs);
    updateTimer(isPresent, nowMs);
    updateBuzzer(nowMs);
    drawDisplay(isPresent, nowMs);
    logStatus(isPresent, nowMs);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
