#pragma once

#include <stdint.h>

// 显示后端抽象。
//
// UI 代码只通过这个接口画图，不需要知道底下是 SPI OLED 还是 I2C 点阵驱动。
// 画布尺寸由后端自己报告（width()/height()），界面布局按尺寸自适应，
// 规则见 display_layout.h。
//
// 文字渲染是基类的共享实现，不在各后端里重复：字库和 textScaled() 都建立在
// setPixel() 之上，所以任何实现了像素接口的后端自动获得同一套字体和度量。
// 这一层不依赖 ESP-IDF，可以在主机端编译和单测。

namespace bell_robot {

// 5x7 字库：字形 5 像素宽，字符步进 6 像素（含 1 像素间距）。
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6;

// 纯函数文字度量，主机端可测。
int textWidthPx(const char *text, int scale);
int centeredTextX(const char *text, int scale, int canvasWidth);

class DisplayBackend {
public:
  virtual ~DisplayBackend() = default;

  virtual bool begin() = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;

  virtual void clear() = 0;
  virtual void fill(bool on) = 0;
  virtual void setPixel(int x, int y, bool on = true) = 0;
  virtual void flush() = 0;

  // 亮度 0-255。OLED 后端忽略；点阵后端映射到驱动芯片的全局电流。
  virtual void setBrightness(uint8_t level) { (void)level; }

  // 以下都基于 setPixel()，所有后端通用。
  void drawChar(int x, int y, char c, int scale = 1);
  void textScaled(int x, int y, int scale, const char *value);
  int centeredX(const char *text, int scale) const;

  void fillRect(int x, int y, int w, int h, bool on = true);
  void drawRectOutline(int x, int y, int w, int h, bool on = true);
};

} // namespace bell_robot
