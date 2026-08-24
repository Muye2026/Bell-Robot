#pragma once

#include <stdint.h>

#include "app_config.h"
#include "display_backend.h"

// IS31FL3733 LED 点阵后端（tech1-cnc-r2 外壳）。
//
// 选这颗而不是 MAX7219 模组，是因为 MAX7219 模组的 LED 间距是写死的，
// 对不上 CNC 外壳上的孔阵；IS31FL3733 配自绘 PCB 可以自定间距，
// 并且每点 8 位 PWM，方便用亮度表达状态（小画布上没有位置放状态文字）。
//
// 单芯片 12 CS x 16 SW = 192 点，按 MATRIX_WIDTH x MATRIX_HEIGHT 需要几片就级联几片，
// 地址在 MATRIX_I2C_ADDRESSES 里配置。
//
// 尚未在实机上验证：本驱动按数据手册写成，硬件到手后需要先跑通再接入主流程。

namespace bell_robot {

// 单片 IS31FL3733 的点数上限。
constexpr int kIs31fl3733DotsPerChip = 192;
constexpr int kIs31fl3733SwCount = 16;
constexpr int kIs31fl3733CsCount = 12;

constexpr int kMatrixChipCount =
    (MATRIX_WIDTH * MATRIX_HEIGHT + kIs31fl3733DotsPerChip - 1) / kIs31fl3733DotsPerChip;

// 显示坐标到「第几片芯片的哪个 SW/CS 交点」的映射。
//
// 当前是按行优先线性编号再切片的占位实现。真实映射由 LED PCB 的走线决定，
// 打样前必须回到这里对齐——单独抽成函数就是为了到时候只改这一处，
// 而不是散落在 flush() 里。
struct MatrixTarget {
  int chip;
  int sw;
  int cs;
};

MatrixTarget matrixTarget(int x, int y);

class LedMatrixIs31fl3733 : public DisplayBackend {
public:
  bool begin() override;
  int width() const override { return MATRIX_WIDTH; }
  int height() const override { return MATRIX_HEIGHT; }

  void clear() override;
  void fill(bool on) override;
  void setPixel(int x, int y, bool on = true) override;
  void flush() override;
  void setBrightness(uint8_t level) override;

private:
  bool writeRegister(int chip, uint8_t reg, uint8_t value);
  bool writeBlock(int chip, uint8_t reg, const uint8_t *values, int length);
  bool selectPage(int chip, uint8_t page);
  bool initChip(int chip);

  uint8_t pwm_[kMatrixChipCount][kIs31fl3733DotsPerChip] = {};
  bool dirty_[kMatrixChipCount] = {};
  uint8_t onLevel_ = MATRIX_DEFAULT_PWM;
  bool ready_ = false;
};

} // namespace bell_robot
