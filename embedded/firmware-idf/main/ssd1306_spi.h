#pragma once

#include <stdint.h>

#include "app_config.h"

class Ssd1306Spi {
public:
  bool begin(uint8_t comScanDirection = 0xc8, uint8_t comPinsConfig = 0x12);
  void clear();
  void fill(bool on);
  void setPixel(int x, int y, bool on = true);
  void text(int col, int row, const char *value);
  void textf(int col, int row, const char *format, ...);
  void textScaled(int x, int y, int scale, const char *value);
  void textScaledf(int x, int y, int scale, const char *format, ...);
  void flush();

private:
  uint8_t buffer_[OLED_WIDTH * OLED_HEIGHT / 8] = {};

  void command(uint8_t value);
  void data(const uint8_t *values, int length);
  void reset();
  void drawChar(int x, int y, char c, int scale = 1);
  bool getPixel(int x, int y) const;
};
