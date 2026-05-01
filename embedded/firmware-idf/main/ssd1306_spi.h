#pragma once

#include <stdint.h>

#include "app_config.h"

class Ssd1306Spi {
public:
  bool begin();
  void clear();
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
  void setPixel(int x, int y);
  void setPhysicalPixel(uint8_t *physicalBuffer, int x, int y) const;
};
