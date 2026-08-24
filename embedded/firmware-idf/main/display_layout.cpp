#include "display_layout.h"

#include <stdio.h>
#include <string.h>

#include "display_backend.h"

namespace bell_robot {

namespace {

// 倒计时永远是 MM:SS，五个字符。所有尺寸推导都以它为基准。
constexpr int kTimerChars = 5;
constexpr int kTextLineHeight = kGlyphHeight + 1; // 一行 scale=1 文字占的高度
constexpr int kMaxScale = 8;

int clampInt(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  return value > high ? high : value;
}

// 能放下 MM:SS 的最大字号。128x64 上得到 4，与旧固件的 kTimerDigitScale 相同；
// 32 宽的点阵上得到 1（scale=2 需要 60 像素宽）。
int largestTimerScale(int width, int height) {
  int best = 1;
  for (int scale = 1; scale <= kMaxScale; ++scale) {
    const int w = kTimerChars * kGlyphAdvance * scale;
    const int h = kGlyphHeight * scale;
    if (w <= width && h <= height) {
      best = scale;
    }
  }
  return best;
}

} // namespace

DisplayLayout makeDisplayLayout(int width, int height) {
  DisplayLayout layout;
  layout.width = width;
  layout.height = height;
  layout.timerScale = largestTimerScale(width, height);

  const int timerH = kGlyphHeight * layout.timerScale;

  // Rich 需要倒计时上下各一行文字，外加行间留白。
  if (height >= timerH + 2 * kTextLineHeight + 6) {
    layout.profile = DisplayProfile::Rich;
    layout.showState = true;
    layout.stateY = 0;
    layout.showProb = true;
    layout.probY = height - kGlyphHeight - 1;
    // 128x64 下解出 20，与旧固件写死的 timerY 一致，换后端不改变 OLED 观感。
    layout.timerY = (height - timerH) / 2 + 2;
    return layout;
  }

  // Compact 放不下两行文字，但放得下一条 2 像素进度条。
  // 阈值由内容高度算出来，不是拍脑袋定的：倒计时 + 1 像素间隔 + 2 像素进度条。
  // 这个数字被 industrial-design/tech1-cnc-r2 的建模脚本引用来判断点阵行数够不够，
  // 两边必须是同一条规则，否则同一块画布上评审图和设备会画出不同的东西。
  constexpr int kBarHeight = 2;
  const int contentH = timerH + 1 + kBarHeight;
  if (height >= contentH) {
    layout.profile = DisplayProfile::Compact;
    layout.timerY = (height - contentH) / 2;
    layout.showBar = true;
    layout.barH = kBarHeight;
    layout.barY = layout.timerY + timerH + 1;
    layout.barX = 1;
    layout.barW = width - 2;
    return layout;
  }

  layout.profile = DisplayProfile::Minimal;
  layout.timerY = (height - timerH) / 2;
  if (layout.timerY < 0) {
    layout.timerY = 0;
  }
  return layout;
}

DisplayFrame makeDisplayFrame(const DisplayLayout &layout,
                              TimerState state,
                              bool isPresent,
                              uint32_t remainingMs,
                              uint32_t sitTargetMs,
                              float modelProbability,
                              uint32_t nowMs) {
  DisplayFrame frame;

  const uint32_t totalSeconds = (remainingMs + 999) / 1000;
  uint32_t minutes = totalSeconds / 60;
  if (minutes > 99) {
    minutes = 99;
  }
  snprintf(frame.timer,
           sizeof(frame.timer),
           "%02lu:%02lu",
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(totalSeconds % 60));

  const bool awayState =
      state == TimerState::AwayGrace || state == TimerState::AwayWarning;
  if (awayState) {
    // 暂离时在 PAUSED 后面加一个闪动的点，表示倒计时冻结而不是卡死。
    snprintf(frame.state,
             sizeof(frame.state),
             "%s",
             (nowMs / 1000) % 2 == 0 ? "PAUSED" : "PAUSED .");
  } else if (state == TimerState::Alerting && (nowMs / 500) % 2 != 0) {
    frame.state[0] = '\0';
  } else {
    snprintf(frame.state, sizeof(frame.state), "%s", displayStateLabel(state, isPresent));
  }

  int percent = static_cast<int>(modelProbability * 100.0f + 0.5f);
  percent = clampInt(percent, 0, 100);
  snprintf(frame.prob, sizeof(frame.prob), "PROB %02d%%", percent);

  // 进度条按「已坐时长 / 目标」填充，和倒计时是同一个量的另一种表达。
  if (sitTargetMs == 0) {
    frame.barPercent = 0;
  } else {
    const uint32_t clampedRemaining = remainingMs > sitTargetMs ? sitTargetMs : remainingMs;
    const uint32_t elapsed = sitTargetMs - clampedRemaining;
    frame.barPercent =
        static_cast<uint8_t>(clampInt(static_cast<int>((elapsed * 100ULL) / sitTargetMs), 0, 100));
  }

  (void)layout;
  return frame;
}

void renderDisplayFrame(DisplayBackend &display,
                        const DisplayLayout &layout,
                        const DisplayFrame &frame) {
  display.clear();

  if (layout.showState) {
    display.textScaled(display.centeredX(frame.state, 1), layout.stateY, 1, frame.state);
  }

  display.textScaled(display.centeredX(frame.timer, layout.timerScale),
                     layout.timerY,
                     layout.timerScale,
                     frame.timer);

  if (layout.showProb) {
    display.textScaled(display.centeredX(frame.prob, 1), layout.probY, 1, frame.prob);
  }

  if (layout.showBar && layout.barW > 0) {
    const int fill = (layout.barW * frame.barPercent) / 100;
    display.fillRect(layout.barX, layout.barY, fill, layout.barH, true);
    // 轨道末端保留两像素刻度，否则进度条走满之前看不出总长度。
    display.fillRect(layout.barX + layout.barW - 2, layout.barY, 2, layout.barH, true);
  }

  display.flush();
}

void renderStartupFrame(DisplayBackend &display,
                        const DisplayLayout &layout,
                        const char *stageLabel,
                        uint8_t progressPercent,
                        uint32_t nowMs) {
  const int width = layout.width;
  const int height = layout.height;
  display.clear();

  if (layout.profile == DisplayProfile::Rich) {
    display.textScaled(display.centeredX("BELL", 2), 0, 2, "BELL");
    display.textScaled(display.centeredX(stageLabel, 1), 24, 1, stageLabel);

    const int barX = 13;
    const int barY = 42;
    const int barW = width - 2 * barX;
    const int barH = 9;
    display.drawRectOutline(barX, barY, barW, barH, true);
    display.fillRect(barX + 2, barY + 2, ((barW - 4) * progressPercent) / 100, barH - 4, true);

    const int scanX = barX + 2 + static_cast<int>((nowMs / 80) % static_cast<uint32_t>(barW - 4));
    display.fillRect(scanX, height - 9, 1, 6, true);

    const char *dots = "";
    switch ((nowMs / 350) % 4) {
    case 1:
      dots = ".";
      break;
    case 2:
      dots = "..";
      break;
    case 3:
      dots = "...";
      break;
    default:
      break;
    }
    char bootText[16] = {};
    if (progressPercent >= 100) {
      snprintf(bootText, sizeof(bootText), "START");
    } else {
      snprintf(bootText, sizeof(bootText), "BOOTING%s", dots);
    }
    display.textScaled(display.centeredX(bootText, 1), height - 8, 1, bootText);
    display.flush();
    return;
  }

  // 小画布：阶段名只在放得下时画（"BOOT"/"WIFI" 放得下，"CAMERA" 在 32 宽下放不下），
  // 进度条是唯一保证可见的启动反馈。
  int barY = (height - 2) / 2;
  if (layout.profile == DisplayProfile::Compact && textWidthPx(stageLabel, 1) <= width) {
    display.textScaled(display.centeredX(stageLabel, 1), 0, 1, stageLabel);
    barY = height - 4;
  }

  const int barX = 1;
  const int barW = width - 2;
  display.fillRect(barX, barY, (barW * progressPercent) / 100, 2, true);
  display.fillRect(barX + barW - 2, barY, 2, 2, true);
  display.flush();
}

void renderOverlay(DisplayBackend &display,
                   const DisplayLayout &layout,
                   const char *message) {
  display.clear();
  if (message == nullptr || message[0] == '\0') {
    display.flush();
    return;
  }

  const int width = layout.width;
  const int height = layout.height;

  // 优先整行显示，能大就大。
  for (int scale = 2; scale >= 1; --scale) {
    if (textWidthPx(message, scale) <= width && kGlyphHeight * scale <= height) {
      const int y = (height - kGlyphHeight * scale) / 2;
      display.textScaled(centeredTextX(message, scale, width), y, scale, message);
      display.flush();
      return;
    }
  }

  // 一行放不下就在空格处拆两行。"CAM FAIL" 拆成 CAM / FAIL 仍然可读，
  // 从中间截断成 "CAM F" 就不可读了。
  const int twoLineHeight = 2 * kGlyphHeight + 1;
  const char *space = strchr(message, ' ');
  if (space != nullptr && twoLineHeight <= height) {
    char head[16] = {};
    const size_t headLen = static_cast<size_t>(space - message);
    if (headLen > 0 && headLen < sizeof(head)) {
      memcpy(head, message, headLen);
      const char *tail = space + 1;
      if (textWidthPx(head, 1) <= width && textWidthPx(tail, 1) <= width) {
        const int top = (height - twoLineHeight) / 2;
        display.textScaled(centeredTextX(head, 1, width), top, 1, head);
        display.textScaled(centeredTextX(tail, 1, width), top + kGlyphHeight + 1, 1, tail);
        display.flush();
        return;
      }
    }
  }

  // 兜底截断。Minimal 画布（32x8）只有 5 个字符位，所以调用方在小画布上
  // 应当直接传短字符串，而不是指望这里能救回来。
  char clipped[16] = {};
  int maxChars = width / kGlyphAdvance;
  if (maxChars < 1) {
    maxChars = 1;
  }
  if (maxChars > static_cast<int>(sizeof(clipped)) - 1) {
    maxChars = static_cast<int>(sizeof(clipped)) - 1;
  }
  strncpy(clipped, message, static_cast<size_t>(maxChars));
  const int y = (height - kGlyphHeight) / 2;
  display.textScaled(centeredTextX(clipped, 1, width), y < 0 ? 0 : y, 1, clipped);
  display.flush();
}

} // namespace bell_robot
