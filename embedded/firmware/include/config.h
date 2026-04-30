#pragma once

#include <Arduino.h>

// Core timing policy.
static constexpr uint32_t SIT_TARGET_MS = 45UL * 60UL * 1000UL;
static constexpr uint32_t AWAY_GRACE_MS = 5UL * 1000UL;
static constexpr uint32_t AWAY_RESET_MS = 10UL * 1000UL;

// Display: SPI SSD1306 128x64 module with pins SCK/MOSI/RES/DC/CS.
#ifndef OLED_DRIVER_SH1106
#define OLED_DRIVER_SH1106 0
#endif

static constexpr int OLED_WIDTH = 128;
static constexpr int OLED_HEIGHT = 64;
static constexpr int PIN_OLED_MOSI = 3;
static constexpr int PIN_OLED_CLK = 14;
static constexpr int PIN_OLED_DC = 41;
static constexpr int PIN_OLED_CS = 42;
static constexpr int PIN_OLED_RESET = 40;
static constexpr uint32_t OLED_SELF_TEST_MS = 5000;

// Output and input.
static constexpr int PIN_BUZZER = 21;
static constexpr int PIN_BUTTON = 1;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
static constexpr uint16_t BUZZER_FREQUENCY_HZ = 2400;
static constexpr uint8_t BUZZER_LEDC_CHANNEL = 1;
static constexpr uint8_t BUZZER_LEDC_RESOLUTION_BITS = 8;
static constexpr uint32_t BUZZER_DUTY = 128;

// Camera switch. Set to 0 to validate timer/display logic without camera wiring.
#ifndef CAMERA_ENABLED
#define CAMERA_ENABLED 1
#endif

// Camera preview switch. When enabled, the board creates a Wi-Fi AP and serves
// JPEG frames at http://192.168.4.1/. Presence detection still runs.
#ifndef CAMERA_PREVIEW_AP_ENABLED
#define CAMERA_PREVIEW_AP_ENABLED 1
#endif

static constexpr char CAMERA_PREVIEW_AP_SSID[] = "Bill-Camera";
static constexpr char CAMERA_PREVIEW_AP_PASSWORD[] = "12345678";
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_0 = 192;
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_1 = 168;
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_2 = 4;
static constexpr uint8_t CAMERA_PREVIEW_AP_IP_3 = 1;

// Current camera module: OV5640CSP, silk screen "VVS OV5640CSP 8225N VC".
static constexpr bool CAMERA_VFLIP = true;
static constexpr bool CAMERA_HMIRROR = false;

// Presence detector tuning.
static constexpr uint32_t CAMERA_SAMPLE_INTERVAL_MS = 500;
static constexpr uint8_t PRESENCE_ON_FRAMES = 6;
static constexpr uint8_t PRESENCE_OFF_FRAMES = 5;
static constexpr uint8_t PRESENCE_CALIBRATION_FRAMES = 8;
static constexpr uint16_t ROI_DIFF_THRESHOLD = 35;

// ROI in percentage of frame dimensions.
static constexpr uint8_t ROI_X_PERCENT = 20;
static constexpr uint8_t ROI_Y_PERCENT = 25;
static constexpr uint8_t ROI_W_PERCENT = 60;
static constexpr uint8_t ROI_H_PERCENT = 55;

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
