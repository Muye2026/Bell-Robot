#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "esp_camera.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"
#include "seat_model.h"
#include "ssd1306_spi.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

namespace {
constexpr char kTag[] = "bell_robot";
constexpr uint32_t kFeatureCount = 64;
constexpr int kTimerDigitScale = 4;
constexpr int kAlertScale = 2;

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

Ssd1306Spi display;
SeatModel seatModel;
TimerContext timerContext;
httpd_handle_t httpServer = nullptr;

uint32_t lastDisplayMs = 0;
uint32_t lastStatusLogMs = 0;
uint32_t lastButtonChangeMs = 0;
bool lastButtonLevel = true;
bool buttonPressedEvent = false;
bool buttonResetConsumed = false;
bool buzzerActive = false;
uint32_t sampleCounter = 0;

uint32_t millis32() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
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

void formatTime(uint32_t ms, char *buffer, size_t bufferSize) {
  const uint32_t totalSeconds = (ms + 999) / 1000;
  snprintf(buffer, bufferSize, "%02lu:%02lu",
           static_cast<unsigned long>(totalSeconds / 60),
           static_cast<unsigned long>(totalSeconds % 60));
}

int scaledTextWidth(const char *text, int scale) {
  if (text == nullptr || scale <= 0) {
    return 0;
  }
  return static_cast<int>(strlen(text)) * 6 * scale;
}

void normalizeNvsInit() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

class PresenceDetector {
public:
  bool begin() {
    modelReady_ = seatModel.begin();

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
    cameraConfig.frame_size = FRAMESIZE_QVGA;
    cameraConfig.pixel_format = PIXFORMAT_GRAYSCALE;
    cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
    cameraConfig.jpeg_quality = 12;
    cameraConfig.fb_count = 2;

    const esp_err_t err = esp_camera_init(&cameraConfig);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "camera init failed: %s", esp_err_to_name(err));
      return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
      sensor->set_vflip(sensor, CAMERA_VFLIP ? 1 : 0);
      sensor->set_hmirror(sensor, CAMERA_HMIRROR ? 1 : 0);
      sensor->set_framesize(sensor, FRAMESIZE_QVGA);
    }
    ESP_LOGI(kTag, "camera ready: %dx%d grayscale", 320, 240);
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
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      diagnostics_.fallbackReason = "camera_frame_failed";
      return present_;
    }

    int8_t features[kFeatureCount] = {};
    buildModelFeatures(frame, features, kFeatureCount);

    const uint16_t roiScore = calculateRoiScore(frame);
    esp_camera_fb_return(frame);
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
        const size_t index = y * width + x;
        if (index < frame->len) {
          sum += frame->buf[index];
          count++;
        }
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
            const size_t index = y * width + x;
            if (index < frame->len) {
              sum += frame->buf[index];
              count++;
            }
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

void resetTimer() {
  timerContext = TimerContext{};
  buzzerOff();
}

uint32_t elapsedSittingMs(uint32_t nowMs) {
  return timerContext.state == TimerState::Idle ? 0 : nowMs - timerContext.sitStartMs;
}

uint32_t remainingSitMs(uint32_t nowMs) {
  const uint32_t elapsed = elapsedSittingMs(nowMs);
  return elapsed >= SIT_TARGET_MS ? 0 : SIT_TARGET_MS - elapsed;
}

void startSitting(uint32_t nowMs) {
  timerContext.state = TimerState::Sitting;
  timerContext.sitStartMs = nowMs;
  timerContext.awayStartMs = 0;
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
    } else if (elapsedSittingMs(nowMs) >= SIT_TARGET_MS) {
      timerContext.state = TimerState::Alerting;
    }
    break;
  case TimerState::AwayGrace: {
    if (isPresent) {
      timerContext.state = TimerState::Sitting;
      timerContext.awayStartMs = 0;
      break;
    }
    const uint32_t awayMs = nowMs - timerContext.awayStartMs;
    if (awayMs >= AWAY_RESET_MS) {
      resetTimer();
    } else if (awayMs >= AWAY_GRACE_MS) {
      timerContext.state = TimerState::AwayWarning;
    }
    break;
  }
  case TimerState::AwayWarning:
    if (isPresent) {
      timerContext.state = TimerState::Sitting;
      timerContext.awayStartMs = 0;
    } else if (nowMs - timerContext.awayStartMs >= AWAY_RESET_MS) {
      resetTimer();
    }
    break;
  case TimerState::Alerting:
    break;
  }
}

bool isButtonDown() {
  return gpio_get_level(static_cast<gpio_num_t>(PIN_BUTTON)) == 0;
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

void handleButton() {
  if (!buttonPressedEvent) {
    return;
  }
  buttonPressedEvent = false;
  resetTimer();
  presenceDetector.recalibrate();
}

void updateAlertOutput(uint32_t nowMs) {
  if (timerContext.state != TimerState::Alerting) {
    buzzerOff();
    return;
  }
  if ((nowMs / 500) % 2 == 0) {
    buzzerOn();
  } else {
    buzzerOff();
  }
}

void drawDisplay(bool isPresent, uint32_t nowMs) {
  if (nowMs - lastDisplayMs < 250) {
    return;
  }
  lastDisplayMs = nowMs;

  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  const uint32_t remainingMs = remainingSitMs(nowMs);
  const uint32_t totalSeconds = (remainingMs + 999) / 1000;
  const uint32_t displayMinutes = std::min<uint32_t>(totalSeconds / 60, 99);
  const uint32_t displaySeconds = totalSeconds % 60;
  char minutesText[4] = {};
  char secondsText[4] = {};
  snprintf(minutesText, sizeof(minutesText), "%02lu", static_cast<unsigned long>(displayMinutes));
  snprintf(secondsText, sizeof(secondsText), "%02lu", static_cast<unsigned long>(displaySeconds));
  const int minutesX = std::max(0, (OLED_WIDTH - scaledTextWidth(minutesText, kTimerDigitScale)) / 2);
  const int secondsX = std::max(0, (OLED_WIDTH - scaledTextWidth(secondsText, kTimerDigitScale)) / 2);
  const char *displayState = displayStateLabel(timerContext.state, isPresent);
  const char *detectText = isPresent ? "sit" : "away";

  display.clear();
  display.textf(0, 0, "%s", displayState);
  display.textf(0, 1, "%s %02u%%",
                detectText,
                static_cast<unsigned>(diag.modelProbability * 100.0f));
  display.textf(0, 2, "R%c ON%u/%u",
                diag.rawPresent ? 'Y' : 'N',
                diag.onFrames,
                PRESENCE_ON_FRAMES);
  if (timerContext.state == TimerState::Alerting) {
    const char *alertText = "STAND";
    const int alertX = std::max(0, (OLED_WIDTH - scaledTextWidth(alertText, kAlertScale)) / 2);
    display.textScaled(alertX, 48, kAlertScale, alertText);
  } else {
    display.textScaled(minutesX, 28, kTimerDigitScale, minutesText);
    display.textScaled(secondsX, 70, kTimerDigitScale, secondsText);
  }
  if (timerContext.state == TimerState::AwayGrace || timerContext.state == TimerState::AwayWarning) {
    char away[8] = {};
    const uint32_t awayMs = nowMs - timerContext.awayStartMs;
    formatTime(AWAY_RESET_MS - std::min(awayMs, AWAY_RESET_MS), away, sizeof(away));
    display.textf(0, 14, "RST %s", away);
  } else if (timerContext.state == TimerState::Idle && !diag.modelReady) {
    display.textf(0, 14, "FB D:%u", diag.diff);
  } else if (timerContext.state == TimerState::Sitting) {
    display.textf(0, 14, "PROB:%02u%%", static_cast<unsigned>(diag.modelProbability * 100.0f));
  } else if (timerContext.state == TimerState::Alerting) {
    display.textf(0, 14, "PRESS BTN");
  }
  display.flush();
}

void logStatus(bool isPresent, uint32_t nowMs) {
  if (nowMs - lastStatusLogMs < 5000) {
    return;
  }
  lastStatusLogMs = nowMs;

  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  ESP_LOGI(kTag,
           "tick state=%s pose=%s raw=%c on=%u off=%u model=%c prob=%u th=%u diff=%u base=%u btn=%c",
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
           isButtonDown() ? 'L' : 'H');
}

esp_err_t sendIndex(httpd_req_t *req) {
  static constexpr char html[] =
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bell Robot Camera</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:760px;margin:24px auto;padding:0 16px}img{width:100%;height:auto;background:#222}"
      "button,a{font-size:18px;padding:10px 14px;margin:6px;display:inline-block}</style></head><body><main>"
      "<h2>Bell Robot Camera</h2><img id='frame' src='/capture?ts=0'>"
      "<p><button onclick='refreshFrame()'>Refresh</button><a href='/status'>Status</a><a href='/reset'>Reset</a></p>"
      "<p><a href='/label?class=absent'>Save absent sample</a><a href='/label?class=seated'>Save seated sample</a></p>"
      "<script>function refreshFrame(){document.getElementById('frame').src='/capture?ts='+Date.now()}"
      "setInterval(refreshFrame,1000)</script></main></body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t sendCapture(httpd_req_t *req) {
  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera capture failed");
    return ESP_FAIL;
  }

  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  bool converted = false;

  if (frame->format == PIXFORMAT_JPEG) {
    jpgBuffer = frame->buf;
    jpgLength = frame->len;
  } else {
    converted = frame2jpg(frame, 80, &jpgBuffer, &jpgLength);
    if (!converted) {
      esp_camera_fb_return(frame);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "jpeg conversion failed");
      return ESP_FAIL;
    }
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t err = httpd_resp_send(req, reinterpret_cast<const char *>(jpgBuffer), jpgLength);
  if (converted) {
    free(jpgBuffer);
  }
  esp_camera_fb_return(frame);
  return err;
}

esp_err_t sendStatus(httpd_req_t *req) {
  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  char payload[640] = {};
  snprintf(payload,
           sizeof(payload),
           "{\"state\":\"%s\",\"present\":%s,\"calibrated\":%s,\"score\":%u,"
           "\"baseline\":%u,\"diff\":%u,\"button\":%s,\"raw_present\":%s,"
           "\"on_frames\":%u,\"off_frames\":%u,\"on_required\":%u,"
           "\"model_ready\":%s,\"model_prob\":%.3f,\"model_threshold\":%.2f,"
           "\"model_version\":\"%s\",\"inference_ms\":%lu,"
           "\"fallback_reason\":\"%s\"}",
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
           diag.fallbackReason == nullptr ? "" : diag.fallbackReason);
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

  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera capture failed");
    return ESP_FAIL;
  }

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
  esp_camera_fb_return(frame);
  return ESP_OK;
}

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 8192;
  ESP_ERROR_CHECK(httpd_start(&httpServer, &config));

  httpd_uri_t index = {};
  index.uri = "/";
  index.method = HTTP_GET;
  index.handler = sendIndex;

  httpd_uri_t capture = {};
  capture.uri = "/capture";
  capture.method = HTTP_GET;
  capture.handler = sendCapture;

  httpd_uri_t status = {};
  status.uri = "/status";
  status.method = HTTP_GET;
  status.handler = sendStatus;

  httpd_uri_t reset = {};
  reset.uri = "/reset";
  reset.method = HTTP_GET;
  reset.handler = handleReset;

  httpd_uri_t label = {};
  label.uri = "/label";
  label.method = HTTP_GET;
  label.handler = handleLabel;

  httpd_register_uri_handler(httpServer, &index);
  httpd_register_uri_handler(httpServer, &capture);
  httpd_register_uri_handler(httpServer, &status);
  httpd_register_uri_handler(httpServer, &reset);
  httpd_register_uri_handler(httpServer, &label);
}

void startWifiAp() {
  normalizeNvsInit();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t *apNetif = esp_netif_create_default_wifi_ap();

  esp_netif_ip_info_t ip = {};
  IP4_ADDR(&ip.ip, CAMERA_PREVIEW_AP_IP_0, CAMERA_PREVIEW_AP_IP_1, CAMERA_PREVIEW_AP_IP_2, CAMERA_PREVIEW_AP_IP_3);
  IP4_ADDR(&ip.gw, CAMERA_PREVIEW_AP_IP_0, CAMERA_PREVIEW_AP_IP_1, CAMERA_PREVIEW_AP_IP_2, CAMERA_PREVIEW_AP_IP_3);
  IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
  ESP_ERROR_CHECK(esp_netif_dhcps_stop(apNetif));
  ESP_ERROR_CHECK(esp_netif_set_ip_info(apNetif, &ip));
  ESP_ERROR_CHECK(esp_netif_dhcps_start(apNetif));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(kTag, "AP started: %s / http://192.168.4.1/", CAMERA_PREVIEW_AP_SSID);
}

void setupButton() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << PIN_BUTTON;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&io);
  lastButtonLevel = !isButtonDown();
}
} // namespace

extern "C" void app_main(void) {
  buzzerBegin();
  setupButton();
  display.begin();
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

  startWifiAp();
  startWebServer();

  ESP_LOGI(kTag, "ready. AP URL: http://192.168.4.1/");
  while (true) {
    const uint32_t nowMs = millis32();
    const bool isPresent = presenceDetector.update(nowMs);
    updateButton(nowMs);
    handleButton();
    updateTimer(isPresent, nowMs);
    updateAlertOutput(nowMs);
    drawDisplay(isPresent, nowMs);
    logStatus(isPresent, nowMs);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
