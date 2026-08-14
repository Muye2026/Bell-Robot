#include "display_ui.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_state.h"
#include "buzzer_player.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "presence_detector.h"
#include "ssd1306_spi.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
constexpr int kTimerDigitScale = 4;
constexpr uint32_t kTransientDisplayMs = 1000;

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

Ssd1306Spi display;
DisplayOverlay displayOverlay;
StartupFeedback startupFeedback;
TaskHandle_t startupFeedbackTaskHandle = nullptr;
uint32_t lastDisplayMs = 0;
bool firstCountdownFrameLogged = false;

int scaledTextWidth(const char *text, int scale) {
  if (text == nullptr || scale <= 0) {
    return 0;
  }
  return static_cast<int>(strlen(text)) * 6 * scale;
}

int centeredTextX(const char *text, int scale) {
  return std::max(0, (OLED_WIDTH - scaledTextWidth(text, scale)) / 2);
}

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

  const char *stage = startupStageLabel(startupFeedback.stage);
  uint8_t progress = startupStageProgress(startupFeedback.stage);
  if (startupFeedback.stage != StartupStage::Ready) {
    progress = std::min<uint8_t>(99, progress + static_cast<uint8_t>((nowMs / 220) % 8));
  }

  display.clear();
  display.textScaled(centeredTextX("BELL", 2), 0, 2, "BELL");
  display.textScaled(centeredTextX(stage, 1), 24, 1, stage);

  constexpr int barX = 13;
  constexpr int barY = 42;
  constexpr int barW = 102;
  constexpr int barH = 9;
  for (int x = barX; x < barX + barW; ++x) {
    display.setPixel(x, barY);
    display.setPixel(x, barY + barH - 1);
  }
  for (int y = barY; y < barY + barH; ++y) {
    display.setPixel(barX, y);
    display.setPixel(barX + barW - 1, y);
  }
  const int fillW = ((barW - 4) * progress) / 100;
  for (int x = 0; x < fillW; ++x) {
    for (int y = 0; y < barH - 4; ++y) {
      display.setPixel(barX + 2 + x, barY + 2 + y);
    }
  }

  const int scanX = barX + 2 + static_cast<int>((nowMs / 80) % (barW - 4));
  for (int y = 55; y < 61; ++y) {
    display.setPixel(scanX, y);
  }

  const char *dots = ".";
  switch ((nowMs / 350) % 4) {
  case 0:
    dots = "";
    break;
  case 1:
    dots = ".";
    break;
  case 2:
    dots = "..";
    break;
  default:
    dots = "...";
    break;
  }
  char bootText[16] = {};
  snprintf(bootText, sizeof(bootText), "BOOTING%s", dots);
  if (startupFeedback.stage == StartupStage::Ready) {
    snprintf(bootText, sizeof(bootText), "START");
  }
  display.textScaled(centeredTextX(bootText, 1), 56, 1, bootText);
  display.flush();
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

struct OledDiagnosticProfile {
  const char *label;
  uint8_t comScanDirection;
  uint8_t comPinsConfig;
};

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
} // namespace

void displayInit() {
  display.begin();
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
  logStartupMilestone("oled_first_frame", nowMs);

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
  const uint32_t remainingMs = sedentaryTimer.remainingSitMs(nowMs);
  const uint32_t totalSeconds = (remainingMs + 999) / 1000;
  const uint32_t displayMinutes = std::min<uint32_t>(totalSeconds / 60, 99);
  const uint32_t displaySeconds = totalSeconds % 60;
  char timerText[8] = {};
  snprintf(timerText,
           sizeof(timerText),
           "%02lu:%02lu",
           static_cast<unsigned long>(displayMinutes),
           static_cast<unsigned long>(displaySeconds));
  const bool awayState = sedentaryTimer.state() == TimerState::AwayGrace ||
                         sedentaryTimer.state() == TimerState::AwayWarning;

  char displayState[12] = {};
  if (awayState) {
    const char *pausedText = (nowMs / 1000) % 2 == 0 ? "PAUSED" : "PAUSED .";
    snprintf(displayState, sizeof(displayState), "%s", pausedText);
  } else if (sedentaryTimer.state() == TimerState::Alerting && (nowMs / 500) % 2 != 0) {
    displayState[0] = '\0';
  } else {
    snprintf(displayState, sizeof(displayState), "%s", displayStateLabel(sedentaryTimer.state(), isPresent));
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
  if (!firstCountdownFrameLogged) {
    firstCountdownFrameLogged = true;
    logStartupMilestone("countdown_first_frame", nowMs);
  }
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

} // namespace bell_robot
