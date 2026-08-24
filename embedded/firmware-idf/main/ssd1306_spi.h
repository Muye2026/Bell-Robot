#pragma once

#include <stdint.h>

#include "app_config.h"
#include "display_backend.h"

// SPI SSD1306 128x64 后端。
//
// 字库和文字渲染在 DisplayBackend 基类里，这里只负责像素缓冲和 SPI 时序。

namespace bell_robot {

class Ssd1306Spi : public DisplayBackend {
public:
  bool begin() override;
  int width() const override { return OLED_WIDTH; }
  int height() const override { return OLED_HEIGHT; }

  void clear() override;
  void fill(bool on) override;
  void setPixel(int x, int y, bool on = true) override;
  void flush() override;

  // SSD1306 专用：自检时逐个尝试 COM 扫描方向 / COM 引脚配置组合，
  // 用来确定手上这块屏的正确初始化参数。不属于通用后端接口。
  bool beginWithProfile(uint8_t comScanDirection, uint8_t comPinsConfig);

private:
  uint8_t buffer_[OLED_WIDTH * OLED_HEIGHT / 8] = {};

  void command(uint8_t value);
  void data(const uint8_t *values, int length);
  void reset();
};

} // namespace bell_robot
