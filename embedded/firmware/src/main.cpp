#include <Arduino.h>
#include <U8g2lib.h>

#include "config.h"

#if CAMERA_ENABLED
#include "esp_camera.h"
#include "img_converters.h"
#endif

#if CAMERA_ENABLED && CAMERA_PREVIEW_AP_ENABLED
#include <WebServer.h>
#include <WiFi.h>
#endif

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
  bool alertSilenced = false;
};

class PresenceDetector {
public:
  bool begin() {
#if CAMERA_ENABLED
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
    cameraConfig.frame_size = CAMERA_PREVIEW_AP_ENABLED ? FRAMESIZE_QVGA : FRAMESIZE_QQVGA;
    cameraConfig.pixel_format = PIXFORMAT_GRAYSCALE;
    cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
    cameraConfig.jpeg_quality = 12;
    cameraConfig.fb_count = 2;

    if (esp_camera_init(&cameraConfig) != ESP_OK) {
      return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
      sensor->set_vflip(sensor, CAMERA_VFLIP ? 1 : 0);
      sensor->set_hmirror(sensor, CAMERA_HMIRROR ? 1 : 0);
      sensor->set_framesize(sensor, CAMERA_PREVIEW_AP_ENABLED ? FRAMESIZE_QVGA : FRAMESIZE_QQVGA);
    }
#endif
    return true;
  }

  bool update(uint32_t nowMs) {
    if (nowMs - lastSampleMs_ < CAMERA_SAMPLE_INTERVAL_MS) {
      return present_;
    }
    lastSampleMs_ = nowMs;

#if CAMERA_ENABLED
    const bool rawPresent = sampleCameraPresence();
#else
    const bool rawPresent = sampleSerialPresence();
#endif
    updateDebouncedPresence(rawPresent);
    return present_;
  }

  bool present() const {
    return present_;
  }

  bool calibrated() const {
    return calibrated_;
  }

  uint16_t lastScore() const {
    return lastScore_;
  }

  uint16_t lastDiff() const {
    return lastDiff_;
  }

  uint16_t baseline() const {
    return baseline_ < 0 ? 0 : static_cast<uint16_t>(baseline_);
  }

  void recalibrate() {
    present_ = false;
    calibrated_ = false;
    onFrames_ = 0;
    offFrames_ = 0;
    baseline_ = -1;
    calibrationSum_ = 0;
    calibrationFrames_ = 0;
    lastDiff_ = 0;
  }

private:
  bool present_ = false;
  bool calibrated_ = false;
  uint32_t lastSampleMs_ = 0;
  uint8_t onFrames_ = 0;
  uint8_t offFrames_ = 0;
  int32_t baseline_ = -1;
  uint32_t calibrationSum_ = 0;
  uint8_t calibrationFrames_ = 0;
  uint16_t lastScore_ = 0;
  uint16_t lastDiff_ = 0;

  void updateDebouncedPresence(bool rawPresent) {
    if (rawPresent) {
      onFrames_ = min<uint8_t>(onFrames_ + 1, PRESENCE_ON_FRAMES);
      offFrames_ = 0;
      if (onFrames_ >= PRESENCE_ON_FRAMES) {
        present_ = true;
      }
      return;
    }

    offFrames_ = min<uint8_t>(offFrames_ + 1, PRESENCE_OFF_FRAMES);
    onFrames_ = 0;
    if (offFrames_ >= PRESENCE_OFF_FRAMES) {
      present_ = false;
    }
  }

#if CAMERA_ENABLED
  bool sampleCameraPresence() {
    camera_fb_t *frame = esp_camera_fb_get();
    if (frame == nullptr) {
      return present_;
    }

    const uint16_t roiScore = calculateRoiScore(frame);
    esp_camera_fb_return(frame);
    lastScore_ = roiScore;

    if (!calibrated_) {
      calibrationSum_ += roiScore;
      calibrationFrames_++;
      lastDiff_ = 0;
      if (calibrationFrames_ >= PRESENCE_CALIBRATION_FRAMES) {
        baseline_ = calibrationSum_ / calibrationFrames_;
        calibrated_ = true;
      }
      return false;
    }

    const uint16_t diff = abs(static_cast<int32_t>(roiScore) - baseline_);
    lastDiff_ = diff;
    return diff >= ROI_DIFF_THRESHOLD;
  }

  uint16_t calculateRoiScore(const camera_fb_t *frame) const {
    const size_t width = frame->width > 0 ? frame->width : 160;
    const size_t height = frame->height > 0 ? frame->height : 120;
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

    if (count == 0) {
      return 0;
    }
    return static_cast<uint16_t>(sum / count);
  }
#else
  bool sampleSerialPresence() {
    while (Serial.available() > 0) {
      const char input = static_cast<char>(Serial.read());
      if (input == '1') {
        return true;
      }
      if (input == '0') {
        return false;
      }
    }
    return present_;
  }
#endif
};

#if OLED_DRIVER_SH1106
U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI display(
    U8G2_R0,
    PIN_OLED_CLK,
    PIN_OLED_MOSI,
    PIN_OLED_CS,
    PIN_OLED_DC,
    PIN_OLED_RESET);
#else
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI display(
    U8G2_R0,
    PIN_OLED_CLK,
    PIN_OLED_MOSI,
    PIN_OLED_CS,
    PIN_OLED_DC,
    PIN_OLED_RESET);
#endif
PresenceDetector presenceDetector;
TimerContext timerContext;

#if CAMERA_ENABLED && CAMERA_PREVIEW_AP_ENABLED
WebServer cameraServer(80);
#endif

uint32_t lastDisplayMs = 0;
uint32_t lastButtonChangeMs = 0;
uint32_t oledSelfTestUntilMs = 0;
bool lastButtonLevel = HIGH;
bool buttonPressedEvent = false;
bool buttonResetConsumed = false;
bool buzzerActive = false;

void buzzerBegin() {
  ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_FREQUENCY_HZ, BUZZER_LEDC_RESOLUTION_BITS);
  ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CHANNEL);
  ledcWrite(BUZZER_LEDC_CHANNEL, 0);
}

void buzzerOn() {
  if (buzzerActive) {
    return;
  }
  ledcWriteTone(BUZZER_LEDC_CHANNEL, BUZZER_FREQUENCY_HZ);
  ledcWrite(BUZZER_LEDC_CHANNEL, BUZZER_DUTY);
  buzzerActive = true;
}

void buzzerOff() {
  if (!buzzerActive) {
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    return;
  }
  ledcWrite(BUZZER_LEDC_CHANNEL, 0);
  buzzerActive = false;
}

void oledHardwareReset() {
  pinMode(PIN_OLED_CS, OUTPUT);
  pinMode(PIN_OLED_CLK, OUTPUT);
  pinMode(PIN_OLED_MOSI, OUTPUT);
  pinMode(PIN_OLED_DC, OUTPUT);
  pinMode(PIN_OLED_RESET, OUTPUT);

  digitalWrite(PIN_OLED_CS, HIGH);
  digitalWrite(PIN_OLED_CLK, LOW);
  digitalWrite(PIN_OLED_MOSI, LOW);
  digitalWrite(PIN_OLED_DC, LOW);
  digitalWrite(PIN_OLED_RESET, HIGH);
  delay(20);
  digitalWrite(PIN_OLED_RESET, LOW);
  delay(80);
  digitalWrite(PIN_OLED_RESET, HIGH);
  delay(150);
}

const char *stateLabel(TimerState state);
void resetTimer();
bool isButtonDown();

#if CAMERA_ENABLED && CAMERA_PREVIEW_AP_ENABLED
void handleCameraIndex() {
  cameraServer.send(
      200,
      "text/html",
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Bill Camera</title><style>body{margin:0;background:#111;color:#eee;font-family:sans-serif}"
      "main{max-width:720px;margin:24px auto;padding:0 16px}img{width:100%;height:auto;background:#222}"
      "button{font-size:18px;padding:10px 14px;margin:12px 0}</style></head><body><main>"
      "<h2>Bill Camera Preview</h2><img id='frame' src='/capture?ts=0'>"
      "<p><button onclick='refreshFrame()'>刷新画面</button></p>"
      "<script>function refreshFrame(){document.getElementById('frame').src='/capture?ts='+Date.now()}"
      "setInterval(refreshFrame,1000)</script></main></body></html>");
}

void handleCameraCapture() {
  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr) {
    cameraServer.send(503, "text/plain", "camera capture failed");
    return;
  }

  cameraServer.sendHeader("Cache-Control", "no-store");

  if (frame->format == PIXFORMAT_JPEG) {
    cameraServer.send_P(200, "image/jpeg", reinterpret_cast<const char *>(frame->buf), frame->len);
    esp_camera_fb_return(frame);
    return;
  }

  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  if (!frame2jpg(frame, 80, &jpgBuffer, &jpgLength)) {
    esp_camera_fb_return(frame);
    cameraServer.send(500, "text/plain", "jpeg conversion failed");
    return;
  }

  cameraServer.send_P(200, "image/jpeg", reinterpret_cast<const char *>(jpgBuffer), jpgLength);
  free(jpgBuffer);
  esp_camera_fb_return(frame);
}

void handleStatus() {
  char payload[192] = {};
  snprintf(payload,
           sizeof(payload),
           "{\"state\":\"%s\",\"present\":%s,\"calibrated\":%s,\"score\":%u,\"baseline\":%u,\"diff\":%u,\"button\":%s}",
           stateLabel(timerContext.state),
           presenceDetector.present() ? "true" : "false",
           presenceDetector.calibrated() ? "true" : "false",
           presenceDetector.lastScore(),
           presenceDetector.baseline(),
           presenceDetector.lastDiff(),
           isButtonDown() ? "true" : "false");
  cameraServer.send(200, "application/json", payload);
}

void handleReset() {
  resetTimer();
  presenceDetector.recalibrate();
  cameraServer.send(200, "text/plain", "reset ok");
}

void beginCameraPreviewServer() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);

  const IPAddress apIp(
      CAMERA_PREVIEW_AP_IP_0,
      CAMERA_PREVIEW_AP_IP_1,
      CAMERA_PREVIEW_AP_IP_2,
      CAMERA_PREVIEW_AP_IP_3);
  const IPAddress gateway = apIp;
  const IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, gateway, subnet);
  WiFi.softAP(CAMERA_PREVIEW_AP_SSID, CAMERA_PREVIEW_AP_PASSWORD, 1, false, 4);
  delay(300);

  cameraServer.on("/", HTTP_GET, handleCameraIndex);
  cameraServer.on("/capture", HTTP_GET, handleCameraCapture);
  cameraServer.on("/status", HTTP_GET, handleStatus);
  cameraServer.on("/reset", HTTP_GET, handleReset);
  cameraServer.begin();

  Serial.printf("Camera preview AP: %s / %s\r\n", CAMERA_PREVIEW_AP_SSID, CAMERA_PREVIEW_AP_PASSWORD);
  Serial.printf("Camera preview IP: %s\r\n", WiFi.softAPIP().toString().c_str());
  Serial.println("Camera preview URL: http://192.168.4.1/");
}
#endif

const char *stateLabel(TimerState state) {
  switch (state) {
  case TimerState::Idle:
    return "WAIT";
  case TimerState::Sitting:
    return "SIT";
  case TimerState::AwayGrace:
    return "AWAY";
  case TimerState::AwayWarning:
    return "RESET SOON";
  case TimerState::Alerting:
    return "STAND";
  }
  return "UNKNOWN";
}

void drawOledSelfTest(uint32_t nowMs) {
  const uint32_t phaseMs = nowMs % 5000;
  display.clearBuffer();
  display.setDrawColor(1);

  if (phaseMs < 700) {
    display.drawBox(0, 0, OLED_WIDTH, OLED_HEIGHT);
    display.sendBuffer();
    return;
  }

  if (phaseMs < 1400) {
    display.sendBuffer();
    return;
  }

  display.drawFrame(0, 0, OLED_WIDTH, OLED_HEIGHT);
  display.drawLine(0, 0, OLED_WIDTH - 1, OLED_HEIGHT - 1);
  display.drawLine(OLED_WIDTH - 1, 0, 0, OLED_HEIGHT - 1);
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(6, 12, OLED_DRIVER_SH1106 ? "OLED SH1106 TEST" : "OLED SSD1306 TEST");
  display.drawStr(6, 26, "SCL14 SDA3");
  display.drawStr(6, 40, "DC41 CS42 R40");
  display.setCursor(6, 56);
  display.print((oledSelfTestUntilMs - nowMs + 999) / 1000);
  display.print("s");
  display.sendBuffer();
}

uint32_t elapsedSittingMs(uint32_t nowMs) {
  if (timerContext.state == TimerState::Idle) {
    return 0;
  }
  return nowMs - timerContext.sitStartMs;
}

uint32_t remainingSitMs(uint32_t nowMs) {
  const uint32_t elapsed = elapsedSittingMs(nowMs);
  if (elapsed >= SIT_TARGET_MS) {
    return 0;
  }
  return SIT_TARGET_MS - elapsed;
}

void formatTime(uint32_t ms, char *buffer, size_t bufferSize) {
  const uint32_t totalSeconds = (ms + 999) / 1000;
  const uint32_t minutes = totalSeconds / 60;
  const uint32_t seconds = totalSeconds % 60;
  snprintf(buffer, bufferSize, "%02lu:%02lu", minutes, seconds);
}

void resetTimer() {
  timerContext = TimerContext{};
  buzzerOff();
}

void startSitting(uint32_t nowMs) {
  timerContext.state = TimerState::Sitting;
  timerContext.sitStartMs = nowMs;
  timerContext.awayStartMs = 0;
  timerContext.alertSilenced = false;
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

  case TimerState::AwayWarning: {
    if (isPresent) {
      timerContext.state = TimerState::Sitting;
      timerContext.awayStartMs = 0;
      break;
    }
    if (nowMs - timerContext.awayStartMs >= AWAY_RESET_MS) {
      resetTimer();
    }
    break;
  }

  case TimerState::Alerting:
    break;
  }
}

void updateButton(uint32_t nowMs) {
  const bool currentLevel = digitalRead(PIN_BUTTON);
  if (currentLevel != lastButtonLevel) {
    lastButtonChangeMs = nowMs;
    lastButtonLevel = currentLevel;
    return;
  }

  if (currentLevel == HIGH) {
    buttonResetConsumed = false;
    return;
  }

  if (!buttonResetConsumed && nowMs - lastButtonChangeMs > BUTTON_DEBOUNCE_MS) {
    buttonResetConsumed = true;
    buttonPressedEvent = true;
  }
}

bool isButtonDown() {
  return digitalRead(PIN_BUTTON) == LOW;
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
  if (timerContext.state != TimerState::Alerting || timerContext.alertSilenced) {
    buzzerOff();
    return;
  }

  const bool beepOn = (nowMs / 500) % 2 == 0;
  if (beepOn) {
    buzzerOn();
  } else {
    buzzerOff();
  }
}

void drawDisplay(bool isPresent, uint32_t nowMs) {
  if (nowMs < oledSelfTestUntilMs) {
    drawOledSelfTest(nowMs);
    return;
  }

  if (nowMs - lastDisplayMs < 250) {
    return;
  }
  lastDisplayMs = nowMs;

  char remaining[8] = {};
  formatTime(remainingSitMs(nowMs), remaining, sizeof(remaining));

  display.clearBuffer();
  display.setDrawColor(1);
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 10);
  display.print("State:");
  display.print(stateLabel(timerContext.state));

  display.setCursor(0, 22);
  display.print("Seat:");
  display.print(isPresent ? "occupied" : "empty");
  display.print(" Btn:");
  display.print(isButtonDown() ? "L" : "H");

  display.setCursor(0, 34);
  display.print(presenceDetector.calibrated() ? "D:" : "CAL ");
  display.print(presenceDetector.lastDiff());
  display.print(" B:");
  display.print(presenceDetector.baseline());

  display.setFont(u8g2_font_logisoso20_tf);
  display.setCursor(0, 63);

  if (timerContext.state == TimerState::Idle) {
    display.print("45:00");
  } else if (timerContext.state == TimerState::Alerting) {
    display.print("STAND!");
  } else {
    display.print(remaining);
  }

  if (timerContext.state == TimerState::AwayGrace || timerContext.state == TimerState::AwayWarning) {
    char away[8] = {};
    const uint32_t awayMs = nowMs - timerContext.awayStartMs;
    formatTime(AWAY_RESET_MS - min(awayMs, AWAY_RESET_MS), away, sizeof(away));
    display.setFont(u8g2_font_6x10_tf);
    display.setCursor(78, 56);
    display.print("R ");
    display.print(away);
  }

  display.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  buzzerBegin();
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Serial.printf("SPI OLED pins. MOSI=%d CLK=%d DC=%d CS=%d RST=%d\r\n",
                PIN_OLED_MOSI,
                PIN_OLED_CLK,
                PIN_OLED_DC,
                PIN_OLED_CS,
                PIN_OLED_RESET);
  oledHardwareReset();
  display.begin();
  display.setPowerSave(0);
  display.setContrast(180);
  Serial.println(OLED_DRIVER_SH1106 ? "SH1106 init ok" : "SSD1306 init ok");
  oledSelfTestUntilMs = millis() + OLED_SELF_TEST_MS;

  display.clearBuffer();
  display.setDrawColor(1);
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 10);
  display.println("Starting...");
  display.sendBuffer();

  if (!presenceDetector.begin()) {
    Serial.println("Camera init failed. Check config.h pins or set CAMERA_ENABLED=0.");
  }

#if CAMERA_ENABLED && CAMERA_PREVIEW_AP_ENABLED
  beginCameraPreviewServer();
#endif

  Serial.println("Ready. If CAMERA_ENABLED=0, send 1 for occupied, 0 for empty.");
}

void loop() {
  const uint32_t nowMs = millis();

#if CAMERA_ENABLED && CAMERA_PREVIEW_AP_ENABLED
  cameraServer.handleClient();
#endif

  const bool isPresent = presenceDetector.update(nowMs);

  updateButton(nowMs);
  handleButton();
  updateTimer(isPresent, nowMs);
  updateAlertOutput(nowMs);
  drawDisplay(isPresent, nowMs);
}
