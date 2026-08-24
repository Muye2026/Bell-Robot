#pragma once

#include <stdint.h>

#include "sedentary_timer.h"

// 界面布局：把「画布多大」和「这一帧显示什么」都变成纯逻辑。
//
// 换成 LED 点阵后画布从 128x64 掉到 32x10 上下，像素预算差一到两个数量级，
// 原来的三行布局（状态 / 大倒计时 / PROB）放不下。所以布局不是写死的坐标表，
// 而是由 makeDisplayLayout() 按尺寸推导：
//
//   Rich    128x64 OLED  状态行 + 大号倒计时 + PROB 行（与旧固件逐像素一致）
//   Compact 32x10 / 32x16  倒计时 + 底部进度条（10 行是这套内容的下界）
//   Minimal 32x8  点阵   只有倒计时，八行刚好被 5x7 字形占满
//
// 这一层不依赖 ESP-IDF，可以在主机端编译并单测，测试入口见
// firmware-idf/test/test_display_layout.cpp。

namespace bell_robot {

class DisplayBackend;

enum class DisplayProfile {
  Rich,
  Compact,
  Minimal,
};

struct DisplayLayout {
  DisplayProfile profile = DisplayProfile::Minimal;
  int width = 0;
  int height = 0;

  // 倒计时是唯一在所有画布上都保留的元素。
  int timerScale = 1;
  int timerY = 0;

  bool showState = false;
  int stateY = 0;

  bool showProb = false;
  int probY = 0;

  // 进度条：小画布上用来代替被砍掉的状态文字。
  bool showBar = false;
  int barX = 0;
  int barY = 0;
  int barW = 0;
  int barH = 0;
};

struct DisplayFrame {
  char state[12] = {};
  char timer[8] = {};
  char prob[16] = {};
  uint8_t barPercent = 0;
};

// 按画布尺寸推导布局。
DisplayLayout makeDisplayLayout(int width, int height);

// 按当前计时状态生成这一帧的内容。nowMs 只用于闪烁相位。
DisplayFrame makeDisplayFrame(const DisplayLayout &layout,
                              TimerState state,
                              bool isPresent,
                              uint32_t remainingMs,
                              uint32_t sitTargetMs,
                              float modelProbability,
                              uint32_t nowMs);

// 把一帧画到后端上（含 clear/flush）。
void renderDisplayFrame(DisplayBackend &display,
                        const DisplayLayout &layout,
                        const DisplayFrame &frame);

// 启动画面，同样按画布尺寸自适应。
void renderStartupFrame(DisplayBackend &display,
                        const DisplayLayout &layout,
                        const char *stageLabel,
                        uint8_t progressPercent,
                        uint32_t nowMs);

// 瞬时提示（CAPTURED / SAVE ERR / CAM FAIL）。小画布上会自动降级字号。
void renderOverlay(DisplayBackend &display,
                   const DisplayLayout &layout,
                   const char *message);

} // namespace bell_robot
