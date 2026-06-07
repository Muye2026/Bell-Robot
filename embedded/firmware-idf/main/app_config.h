#pragma once

#include <stddef.h>
#include <stdint.h>

// Core timing policy.
static constexpr uint32_t DEFAULT_SIT_TARGET_MS = 45UL * 60UL * 1000UL;
static constexpr uint32_t AWAY_GRACE_MS = 10UL * 1000UL;
static constexpr uint32_t DEFAULT_AWAY_RESET_MS = 1UL * 60UL * 1000UL;

// Display: SPI SSD1306 128x64 module with pins SCK/MOSI/RES/DC/CS.
// Render a 128x64 logical canvas directly into the SSD1306 page buffer.
static constexpr int OLED_PHYSICAL_WIDTH = 128;
static constexpr int OLED_PHYSICAL_HEIGHT = 64;
static constexpr int OLED_WIDTH = OLED_PHYSICAL_WIDTH;
static constexpr int OLED_HEIGHT = OLED_PHYSICAL_HEIGHT;
static constexpr bool ENABLE_OLED_DIAGNOSTICS = false;
static constexpr int PIN_OLED_MOSI = 47;
static constexpr int PIN_OLED_CLK = 48;
static constexpr int PIN_OLED_DC = 41;
static constexpr int PIN_OLED_CS = 42;
static constexpr int PIN_OLED_RESET = 40;

// Output and input.
static constexpr int PIN_BUZZER = 21;
static constexpr int PIN_BUTTON = 1;
static constexpr int PIN_CAPTURE_BUTTON = 2;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
static constexpr uint32_t CAPTURE_BUTTON_DEBOUNCE_MS = 40;
static constexpr uint32_t BUZZER_FREQUENCY_HZ = 2400;
static constexpr uint32_t BUZZER_DUTY = 128;

// Startup feedback.
static constexpr bool STARTUP_FEEDBACK_ENABLED = true;
static constexpr bool STARTUP_AUDIO_ENABLED = true;
static constexpr uint32_t STARTUP_DISPLAY_FRAME_MS = 120;
static constexpr bool CAMERA_AUTOFOCUS_BLOCKING_STARTUP = false;

// 倒计时到时（Alerting 状态）的提示音旋律。
// 0 = 超级马里奥「过关」通关音，1 = 最终幻想「胜利」号角，2 = 升级/答对小铃声。
// 乐谱定义在 buzzer_music.h，可在那里新增旋律后用新的编号引用。
static constexpr int BUZZER_ALERT_MELODY = 0;
// 旋律播完一遍后、在仍未起身时，下一次重播之前的静音间隔。
static constexpr uint32_t BUZZER_ALERT_REPEAT_GAP_MS = 1200;
static constexpr size_t SAMPLE_STORE_MAX_COUNT = 64;

// Camera preview AP.
static constexpr char CAMERA_PREVIEW_AP_SSID[] = "Bell-Robot";
static constexpr char CAMERA_PREVIEW_AP_PASSWORD[] = "12345678";
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_0 = 192;
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_1 = 168;
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_2 = 4;
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_3 = 1;

// Development network mode.
// false: AP-only for local development.
// true: STA cloud remote with AP fallback.
static constexpr bool ENABLE_CLOUD_REMOTE = false;

// Current camera module: DC5640 AF 120-degree lens.
// Pin mapping currently reuses the Freenove OV5640-class interface and must be
// re-verified against the actual module if capture orientation or wiring looks off.
static constexpr bool CAMERA_VFLIP = true;
static constexpr bool CAMERA_HMIRROR = false;
static constexpr bool CAMERA_ROTATE_CW_90 = true;

// Presence detector tuning.
static constexpr uint32_t CAMERA_SAMPLE_INTERVAL_MS = 500;
static constexpr uint8_t PRESENCE_ON_FRAMES = 5;
static constexpr uint8_t PRESENCE_OFF_FRAMES = 5;
static constexpr uint8_t PRESENCE_CALIBRATION_FRAMES = 8;
static constexpr uint16_t ROI_DIFF_THRESHOLD = 35;
static constexpr float MODEL_OCCUPIED_THRESHOLD = 0.50f;

// ROI in percentage of frame dimensions.
// The default camera placement is on a monitor or desktop, so bias the region
// upward toward head/shoulders/torso instead of the lower chair area.
static constexpr uint8_t ROI_X_PERCENT = 18;
static constexpr uint8_t ROI_Y_PERCENT = 10;
static constexpr uint8_t ROI_W_PERCENT = 64;
static constexpr uint8_t ROI_H_PERCENT = 72;

// Camera pins verified against the Freenove ESP32-S3 WROOM pinout.
static constexpr int PIN_CAM_PWDN = -1;
static constexpr int PIN_CAM_RESET = -1;
static constexpr int PIN_CAM_XCLK = 15;
static constexpr int PIN_CAM_SIOD = 4;
static constexpr int PIN_CAM_SIOC = 5;
static constexpr int PIN_CAM_D0 = 11; // CAM_Y2
static constexpr int PIN_CAM_D1 = 9;  // CAM_Y3
static constexpr int PIN_CAM_D2 = 8;  // CAM_Y4
static constexpr int PIN_CAM_D3 = 10; // CAM_Y5
static constexpr int PIN_CAM_D4 = 12; // CAM_Y6
static constexpr int PIN_CAM_D5 = 18; // CAM_Y7
static constexpr int PIN_CAM_D6 = 17; // CAM_Y8
static constexpr int PIN_CAM_D7 = 16; // CAM_Y9
static constexpr int PIN_CAM_VSYNC = 6;
static constexpr int PIN_CAM_HREF = 7;
static constexpr int PIN_CAM_PCLK = 13;
