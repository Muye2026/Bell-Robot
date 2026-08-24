#include "display_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_state.h"
#include "buzzer_player.h"
#include "display_backend.h"
#include "display_layout.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "presence_detector.h"

#if DISPLAY_BACKEND == DISPLAY_BACKEND_LED_MATRIX
#include "led_matrix_is31fl3733.h"
#else
#include "ssd1306_spi.h"
#endif

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
constexpr uint32_t kFrameIntervalMs = 250;

struct DisplayOverlay {
  char message[16] = {};
  uint32_t untilMs = 0;
};

struct StartupFeedback {
  bool active = false;
  StartupStage stage = StartupStage::Boot;
  uint32_t bootStartMs = 0;
  uint32_t lastFrameMs = 0;
  const BuzzerMelody *melody = nullptr;
  size_t melodyIndex = 0;
  uint32_t melodySegmentEndMs = 0;
};

#if DISPLAY_BACKEND == DISPLAY_BACKEND_LED_MATRIX
LedMatrixIs31fl3733 backend;
#else
Ssd1306Spi backend;
#endif

DisplayBackend &display = backend;
DisplayLayout layout;

DisplayOverlay displayOverlay;
StartupFeedback startupFeedback;
TaskHandle_t startupFeedbackTaskHandle = nullptr;
uint32_t lastDisplayMs = 0;
bool firstCountdownFrameLogged = false;

const BuzzerMelody &startupFeedbackMelody() {
  return kStartupMelodyVariants[0];
}

const char *startupStageLabel(StartupStage stage) {
  switch (stage) {
  case StartupStage::Boot:
    return "BOOT";
  case StartupStage::Camera:
    return "CAMERA";
  case StartupStage::Wifi:
    return "WIFI";
  case StartupStage::Server:
    return "SERVER";
  case StartupStage::Ready:
    return "READY";
  }
  return "BOOT";
}

uint8_t startupStageProgress(StartupStage stage) {
  switch (stage) {
  case StartupStage::Boot:
    return 15;
  case StartupStage::Camera:
    return 42;
  case StartupStage::Wifi:
    return 68;
  case StartupStage::Server:
    return 84;
  case StartupStage::Ready:
    return 100;
  }
  return 0;
}

void drawStartupFrame(uint32_t nowMs, bool force) {
  if (!STARTUP_FEEDBACK_ENABLED) {
    return;
  }
  if (!force && nowMs - startupFeedback.lastFrameMs < STARTUP_DISPLAY_FRAME_MS) {
    return;
  }
  startupFeedback.lastFrameMs = nowMs;

  uint8_t progress = startupStageProgress(startupFeedback.stage);
  if (startupFeedback.stage != StartupStage::Ready) {
    // 阶段之间抖动几个百分点，让进度条在等待时看起来还在动。
    const uint8_t jitter = static_cast<uint8_t>((nowMs / 220) % 8);
    progress = static_cast<uint8_t>(progress + jitter);
    if (progress > 99) {
      progress = 99;
    }
  }

  renderStartupFrame(display, layout, startupStageLabel(startupFeedback.stage), progress, nowMs);
}

void updateStartupMelody(uint32_t nowMs) {
  if (!STARTUP_AUDIO_ENABLED || startupFeedback.melody == nullptr ||
      startupFeedback.melody->notes == nullptr || startupFeedback.melody->count == 0) {
    return;
  }
  if (startupFeedback.melodySegmentEndMs != 0 &&
      static_cast<int32_t>(nowMs - startupFeedback.melodySegmentEndMs) < 0) {
    return;
  }
  if (startupFeedback.melodyIndex >= startupFeedback.melody->count) {
    startupFeedback.melody = nullptr;
    startupFeedback.melodySegmentEndMs = 0;
    buzzerPlayer.off();
    return;
  }

  const BuzzerNote &note = startupFeedback.melody->notes[startupFeedback.melodyIndex++];
  buzzerPlayer.tone(note.freqHz);
  startupFeedback.melodySegmentEndMs = nowMs + note.durationMs;
}

void startupFeedbackTask(void *arg) {
  (void)arg;
  while (startupFeedback.active) {
    const uint32_t nowMs = millis32();
    updateStartupMelody(nowMs);
    drawStartupFrame(nowMs, false);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  buzzerPlayer.off();
  startupFeedbackTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

#if DISPLAY_BACKEND == DISPLAY_BACKEND_SSD1306
void flushDisplayForMs(uint32_t durationMs) {
  display.flush();
  vTaskDelay(pdMS_TO_TICKS(durationMs));
}

void drawProfileLabel(const char *label) {
  display.fillRect(0, 48, display.width(), display.height() - 48, false);
  display.textScaled(display.centeredX(label, 1), 56, 1, label);
}

void drawCheckerPattern() {
  display.clear();
  for (int y = 0; y < display.height(); ++y) {
    for (int x = 0; x < display.width(); ++x) {
      if (((x / 4) + (y / 4)) % 2 == 0) {
        display.setPixel(x, y);
      }
    }
  }
}

void drawVerticalStripePattern() {
  display.clear();
  for (int x = 0; x < display.width(); ++x) {
    if ((x / 2) % 2 != 0) {
      continue;
    }
    for (int y = 0; y < display.height(); ++y) {
      display.setPixel(x, y);
    }
  }
}

struct OledDiagnosticProfile {
  const char *label;
  uint8_t comScanDirection;
  uint8_t comPinsConfig;
};

void runOledDiagnosticsForProfile(const OledDiagnosticProfile &profile) {
  backend.beginWithProfile(profile.comScanDirection, profile.comPinsConfig);

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
#endif // DISPLAY_BACKEND_SSD1306

} // namespace

void displayInit() {
  display.begin();
  layout = makeDisplayLayout(display.width(), display.height());
  ESP_LOGI(kTag,
           "display %dx%d profile=%d timer_scale=%d",
           layout.width,
           layout.height,
           static_cast<int>(layout.profile),
           layout.timerScale);
}

void startStartupFeedback(uint32_t bootStartMs) {
  firstCountdownFrameLogged = false;
  startupFeedback = StartupFeedback{};
  startupFeedback.bootStartMs = bootStartMs;
  logStartupMilestone("boot_start", bootStartMs);

  if (!STARTUP_FEEDBACK_ENABLED) {
    return;
  }

  startupFeedback.active = true;
  if (STARTUP_AUDIO_ENABLED) {
    startupFeedback.melody = &startupFeedbackMelody();
  }

  const uint32_t nowMs = millis32();
  drawStartupFrame(nowMs, true);
  logStartupMilestone("display_first_frame", nowMs);

  const BaseType_t created =
      xTaskCreate(startupFeedbackTask, "startup_ui", 4096, nullptr, 3, &startupFeedbackTaskHandle);
  if (created != pdPASS) {
    startupFeedbackTaskHandle = nullptr;
    ESP_LOGW(kTag, "startup feedback task create failed");
  }
}

void setStartupStage(StartupStage stage, uint32_t nowMs) {
  if (startupFeedback.stage == stage) {
    return;
  }
  startupFeedback.stage = stage;
  logStartupMilestone(startupStageLabel(stage), nowMs);
}

void stopStartupFeedback(uint32_t nowMs) {
  setStartupStage(StartupStage::Ready, nowMs);
  startupFeedback.active = false;
  vTaskDelay(pdMS_TO_TICKS(30));
  drawStartupFrame(millis32(), true);
  buzzerPlayer.off();
  lastDisplayMs = 0;
}

void logStartupMilestone(const char *name, uint32_t nowMs) {
  const uint32_t elapsed =
      startupFeedback.bootStartMs == 0 ? 0 : nowMs - startupFeedback.bootStartMs;
  ESP_LOGI(kTag,
           "startup milestone: %s at %lu ms",
           name,
           static_cast<unsigned long>(elapsed));
}

void runStartupDiagnosticsOverride(uint32_t bootStartMs) {
  startupFeedback.active = false;
  runOledDiagnostics();
  startStartupFeedback(bootStartMs);
}

void showDisplayOverlay(const char *message, uint32_t nowMs, uint32_t durationMs) {
  safeCopy(displayOverlay.message, sizeof(displayOverlay.message), message);
  displayOverlay.untilMs = nowMs + durationMs;
  lastDisplayMs = 0;
}

void drawDisplay(bool isPresent, uint32_t nowMs) {
  if (nowMs - lastDisplayMs < kFrameIntervalMs) {
    return;
  }
  lastDisplayMs = nowMs;

  if (displayOverlay.untilMs != 0 && static_cast<int32_t>(displayOverlay.untilMs - nowMs) > 0) {
    renderOverlay(display, layout, displayOverlay.message);
    return;
  }
  if (displayOverlay.untilMs != 0) {
    displayOverlay.untilMs = 0;
    displayOverlay.message[0] = '\0';
  }

  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  const DisplayFrame frame = makeDisplayFrame(layout,
                                              sedentaryTimer.state(),
                                              isPresent,
                                              sedentaryTimer.remainingSitMs(nowMs),
                                              sedentaryTimer.sitTargetMs(),
                                              diag.modelProbability,
                                              nowMs);
  renderDisplayFrame(display, layout, frame);

  if (!firstCountdownFrameLogged) {
    firstCountdownFrameLogged = true;
    logStartupMilestone("countdown_first_frame", nowMs);
  }
}

void runOledDiagnostics() {
#if DISPLAY_BACKEND == DISPLAY_BACKEND_SSD1306
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

  backend.begin();
  display.clear();
  display.flush();
#endif
}

} // namespace bell_robot
