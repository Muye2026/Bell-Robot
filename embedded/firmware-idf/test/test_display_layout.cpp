// 显示布局 + 后端文字渲染的主机端单元测试。
//
// 只依赖 display_layout.cpp + display_backend.cpp + sedentary_timer.cpp，
// 不链接任何 ESP-IDF 代码。用 tools/test-host.sh 构建并运行。
//
// 重点覆盖两件事：
//   1. 128x64 OLED 的布局在重构后逐像素不变（换后端不能改现有硬件的观感）。
//   2. 32x8 / 32x16 点阵上倒计时确实放得下、不越界、不被截断——这是
//      industrial-design/tech1-cnc-r1 里两个点阵档位的前提假设。

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "display_backend.h"
#include "display_layout.h"
#include "sedentary_timer.h"

using bell_robot::DisplayBackend;
using bell_robot::DisplayFrame;
using bell_robot::DisplayLayout;
using bell_robot::DisplayProfile;
using bell_robot::TimerState;
using bell_robot::centeredTextX;
using bell_robot::kGlyphHeight;
using bell_robot::makeDisplayFrame;
using bell_robot::makeDisplayLayout;
using bell_robot::renderDisplayFrame;
using bell_robot::renderOverlay;
using bell_robot::renderStartupFrame;
using bell_robot::textWidthPx;

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++gChecks;                                                            \
    if (!(cond)) {                                                        \
      ++gFailures;                                                        \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                     \
  } while (0)

#define CHECK_EQ(actual, expected)                                        \
  do {                                                                    \
    ++gChecks;                                                            \
    const long a__ = static_cast<long>(actual);                           \
    const long e__ = static_cast<long>(expected);                         \
    if (a__ != e__) {                                                     \
      ++gFailures;                                                        \
      printf("FAIL %s:%d: %s = %ld, expected %ld\n",                      \
             __FILE__, __LINE__, #actual, a__, e__);                      \
    }                                                                     \
  } while (0)

// 假后端：把像素画进字符网格，并记录越界写入。
class FakeDisplay : public DisplayBackend {
public:
  static constexpr int kMaxW = 256;
  static constexpr int kMaxH = 128;

  FakeDisplay(int w, int h) : width_(w), height_(h) { clear(); }

  bool begin() override { return true; }
  int width() const override { return width_; }
  int height() const override { return height_; }

  void clear() override {
    memset(pixels_, 0, sizeof(pixels_));
    flushes_ = 0;
  }

  void fill(bool on) override {
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        pixels_[y][x] = on ? 1 : 0;
      }
    }
  }

  void setPixel(int x, int y, bool on = true) override {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) {
      ++outOfBounds_;
      return;
    }
    pixels_[y][x] = on ? 1 : 0;
  }

  void flush() override { ++flushes_; }

  bool at(int x, int y) const { return pixels_[y][x] != 0; }
  int outOfBounds() const { return outOfBounds_; }
  int flushes() const { return flushes_; }

  int litCount() const {
    int n = 0;
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        n += pixels_[y][x];
      }
    }
    return n;
  }

  int rightmostLitColumn() const {
    for (int x = width_ - 1; x >= 0; --x) {
      for (int y = 0; y < height_; ++y) {
        if (pixels_[y][x]) {
          return x;
        }
      }
    }
    return -1;
  }

  int topmostLitRow() const {
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        if (pixels_[y][x]) {
          return y;
        }
      }
    }
    return -1;
  }

  int bottommostLitRow() const {
    for (int y = height_ - 1; y >= 0; --y) {
      for (int x = 0; x < width_; ++x) {
        if (pixels_[y][x]) {
          return y;
        }
      }
    }
    return -1;
  }

  bool rowEmpty(int y) const {
    for (int x = 0; x < width_; ++x) {
      if (pixels_[y][x]) {
        return false;
      }
    }
    return true;
  }

  void dump(const char *label) const {
    printf("--- %s (%dx%d) ---\n", label, width_, height_);
    for (int y = 0; y < height_; ++y) {
      for (int x = 0; x < width_; ++x) {
        putchar(pixels_[y][x] ? '#' : '.');
      }
      putchar('\n');
    }
  }

private:
  int width_;
  int height_;
  uint8_t pixels_[kMaxH][kMaxW] = {};
  int outOfBounds_ = 0;
  int flushes_ = 0;
};

DisplayFrame sittingFrame(const DisplayLayout &layout, uint32_t remainingMs, uint32_t targetMs) {
  return makeDisplayFrame(layout, TimerState::Sitting, true, remainingMs, targetMs, 0.9f, 0);
}

// ---------------------------------------------------------------- 文字度量

void testTextMetrics() {
  CHECK_EQ(textWidthPx("45:00", 1), 30);
  CHECK_EQ(textWidthPx("45:00", 4), 120);
  CHECK_EQ(textWidthPx("", 1), 0);
  CHECK_EQ(textWidthPx(nullptr, 1), 0);
  CHECK_EQ(textWidthPx("45:00", 0), 0);

  // 32 宽的点阵上 MM:SS 正好剩 2 像素余量，这是 matrix-a/b 选 32 列的原因。
  CHECK(textWidthPx("45:00", 1) <= 32);
  CHECK(textWidthPx("45:00", 2) > 32);

  CHECK_EQ(centeredTextX("45:00", 1, 32), 1);
  CHECK_EQ(centeredTextX("45:00", 4, 128), 4);
  // 放不下时不返回负数，否则字形会被画到画布左边外面。
  CHECK_EQ(centeredTextX("PROB 100%", 4, 32), 0);
}

// ------------------------------------------------------------------ 布局

void testRichLayoutMatchesLegacyOled() {
  // 重构前 display_ui.cpp 里写死的值：stateY=0、timerY=20、scale=4、probY=56。
  // 这些必须由尺寸推导出来而不是巧合，否则现有 OLED 观感会变。
  const DisplayLayout layout = makeDisplayLayout(128, 64);
  CHECK(layout.profile == DisplayProfile::Rich);
  CHECK_EQ(layout.timerScale, 4);
  CHECK_EQ(layout.timerY, 20);
  CHECK(layout.showState);
  CHECK_EQ(layout.stateY, 0);
  CHECK(layout.showProb);
  CHECK_EQ(layout.probY, 56);
  CHECK(!layout.showBar);
}

void testMinimalLayout32x8() {
  const DisplayLayout layout = makeDisplayLayout(32, 8);
  CHECK(layout.profile == DisplayProfile::Minimal);
  CHECK_EQ(layout.timerScale, 1);
  CHECK_EQ(layout.timerY, 0);
  // 八行被 5x7 字形占满，没有位置放状态文字或进度条。
  CHECK(!layout.showState);
  CHECK(!layout.showProb);
  CHECK(!layout.showBar);
}

void testCompactLayout32x16() {
  const DisplayLayout layout = makeDisplayLayout(32, 16);
  CHECK(layout.profile == DisplayProfile::Compact);
  CHECK_EQ(layout.timerScale, 1);
  CHECK_EQ(layout.timerY, 3);
  CHECK(!layout.showState);
  CHECK(!layout.showProb);
  CHECK(layout.showBar);
  CHECK_EQ(layout.barX, 1);
  CHECK_EQ(layout.barW, 30);
  CHECK_EQ(layout.barH, 2);
  CHECK_EQ(layout.barY, 11);
  // 进度条不能压到倒计时字形上。
  CHECK(layout.barY >= layout.timerY + kGlyphHeight);
}

void testCompactLayout32x10() {
  // tech1-cnc-r2 的实际点阵：60x20 正面减掉摄像头，1.4mm 间距下解出 32x10。
  // 10 行正好等于「倒计时 7 + 间隔 1 + 进度条 2」，是 Compact 的下界。
  const DisplayLayout layout = makeDisplayLayout(32, 10);
  CHECK(layout.profile == DisplayProfile::Compact);
  CHECK_EQ(layout.timerScale, 1);
  CHECK_EQ(layout.timerY, 0);
  CHECK(layout.showBar);
  CHECK_EQ(layout.barY, 8);
  CHECK_EQ(layout.barH, 2);
  CHECK(layout.barY + layout.barH <= 10);
  CHECK(layout.barY >= layout.timerY + kGlyphHeight);

  // 少一行就退回 Minimal，不能出现进度条压在字形上的中间状态。
  CHECK(makeDisplayLayout(32, 9).profile == DisplayProfile::Minimal);
}

void testLayoutNeverOverflows() {
  const int sizes[][2] = {{128, 64}, {32, 8}, {32, 10}, {32, 16}, {24, 8}, {64, 32}, {32, 24}, {16, 8}};
  for (const auto &size : sizes) {
    const DisplayLayout layout = makeDisplayLayout(size[0], size[1]);
    CHECK(layout.timerScale >= 1);
    CHECK(layout.timerY >= 0);
    CHECK(layout.timerY + kGlyphHeight * layout.timerScale <= size[1]);
    if (layout.showProb) {
      CHECK(layout.probY + kGlyphHeight <= size[1]);
    }
    if (layout.showBar) {
      CHECK(layout.barY + layout.barH <= size[1]);
      CHECK(layout.barX + layout.barW <= size[0]);
    }
  }
}

// ------------------------------------------------------------------ 帧内容

void testFrameTimerText() {
  const DisplayLayout layout = makeDisplayLayout(128, 64);

  DisplayFrame frame = sittingFrame(layout, 45UL * 60UL * 1000UL, 45UL * 60UL * 1000UL);
  CHECK(strcmp(frame.timer, "45:00") == 0);

  frame = sittingFrame(layout, 0, 45UL * 60UL * 1000UL);
  CHECK(strcmp(frame.timer, "00:00") == 0);

  frame = sittingFrame(layout, 61UL * 1000UL, 45UL * 60UL * 1000UL);
  CHECK(strcmp(frame.timer, "01:01") == 0);

  // 分钟数封顶 99，否则 MM:SS 会变成六个字符把布局撑破。
  frame = sittingFrame(layout, 200UL * 60UL * 1000UL, 200UL * 60UL * 1000UL);
  CHECK(strcmp(frame.timer, "99:00") == 0);
  CHECK_EQ(strlen(frame.timer), 5);
}

void testFrameStateBlink() {
  const DisplayLayout layout = makeDisplayLayout(128, 64);

  // 暂离：PAUSED 后面的点每秒闪一次。
  DisplayFrame a = makeDisplayFrame(layout, TimerState::AwayGrace, false, 1000, 1000, 0.1f, 0);
  DisplayFrame b = makeDisplayFrame(layout, TimerState::AwayGrace, false, 1000, 1000, 0.1f, 1000);
  CHECK(strcmp(a.state, "PAUSED") == 0);
  CHECK(strcmp(b.state, "PAUSED .") == 0);

  // 到时告警：状态文字每 500ms 闪一次。
  DisplayFrame on = makeDisplayFrame(layout, TimerState::Alerting, true, 0, 1000, 0.9f, 0);
  DisplayFrame off = makeDisplayFrame(layout, TimerState::Alerting, true, 0, 1000, 0.9f, 500);
  CHECK(on.state[0] != '\0');
  CHECK(off.state[0] == '\0');
}

void testFrameProbAndBar() {
  const DisplayLayout rich = makeDisplayLayout(128, 64);
  DisplayFrame frame = sittingFrame(rich, 0, 1000);
  CHECK(strcmp(frame.prob, "PROB 90%") == 0);

  // 概率超出 0-1 时不能写出 "PROB 120%" 这种把布局撑破的字符串。
  frame = makeDisplayFrame(rich, TimerState::Sitting, true, 0, 1000, 1.8f, 0);
  CHECK(strcmp(frame.prob, "PROB 100%") == 0);
  frame = makeDisplayFrame(rich, TimerState::Sitting, true, 0, 1000, -0.5f, 0);
  CHECK(strcmp(frame.prob, "PROB 00%") == 0);

  const DisplayLayout compact = makeDisplayLayout(32, 16);
  CHECK_EQ(sittingFrame(compact, 1000, 1000).barPercent, 0);
  CHECK_EQ(sittingFrame(compact, 500, 1000).barPercent, 50);
  CHECK_EQ(sittingFrame(compact, 0, 1000).barPercent, 100);
  // 剩余时间大于目标（刚配置完设置）时不能算出负数。
  CHECK_EQ(sittingFrame(compact, 5000, 1000).barPercent, 0);
  // 目标为 0 时不能除零。
  CHECK_EQ(sittingFrame(compact, 0, 0).barPercent, 0);
}

// ------------------------------------------------------------------ 渲染

void testRender32x8FitsCountdown() {
  const DisplayLayout layout = makeDisplayLayout(32, 8);
  FakeDisplay display(32, 8);
  renderDisplayFrame(display, layout, sittingFrame(layout, 45UL * 60UL * 1000UL, 45UL * 60UL * 1000UL));

  CHECK_EQ(display.outOfBounds(), 0);
  CHECK_EQ(display.flushes(), 1);
  // 五个字形都画上了，没有因为宽度不够被 textScaled 截断。
  // 起点 x=1，最后一个字形占 25-29 列。
  CHECK_EQ(display.rightmostLitColumn(), 29);
  CHECK_EQ(display.bottommostLitRow(), 6);
  CHECK(display.rowEmpty(7));

  // "45:00" 在这套 5x7 字库下点亮的像素数是固定的，用它兜住字形渲染回归。
  // 同一个数字也被 industrial-design/tech1-cnc-r1 的点亮渲染引用
  // （validation JSON 里的 lit_dots_in_45_00_frame），两边共用同一份字形数据。
  CHECK_EQ(display.litCount(), 77);
}

void testRender32x16ShowsBar() {
  const DisplayLayout layout = makeDisplayLayout(32, 16);
  FakeDisplay display(32, 16);
  renderDisplayFrame(display, layout, sittingFrame(layout, 500, 1000));

  CHECK_EQ(display.outOfBounds(), 0);
  // 进度条 50%：从 x=1 起 15 像素实心，加上末端 2 像素刻度。
  CHECK(display.at(1, 11));
  CHECK(display.at(15, 11));
  CHECK(!display.at(16, 11));
  CHECK(display.at(30, 11));
  CHECK(display.at(30, 12));
  CHECK(!display.at(31, 11));
}

void testRender128x64HasAllThreeRows() {
  const DisplayLayout layout = makeDisplayLayout(128, 64);
  FakeDisplay display(128, 64);
  renderDisplayFrame(display, layout, sittingFrame(layout, 45UL * 60UL * 1000UL, 45UL * 60UL * 1000UL));

  CHECK_EQ(display.outOfBounds(), 0);
  CHECK(!display.rowEmpty(0));   // 状态行
  CHECK(!display.rowEmpty(20));  // 大号倒计时
  CHECK(!display.rowEmpty(56));  // PROB 行
}

void testStartupAndOverlayNeverOverflow() {
  const int sizes[][2] = {{128, 64}, {32, 16}, {32, 8}};
  const char *stages[] = {"BOOT", "CAMERA", "WIFI", "SERVER", "READY"};
  const char *overlays[] = {"CAPTURED", "SAVE ERR", "CAM FAIL", "OK", ""};

  for (const auto &size : sizes) {
    const DisplayLayout layout = makeDisplayLayout(size[0], size[1]);
    for (const char *stage : stages) {
      for (uint8_t progress = 0; progress <= 100; progress = static_cast<uint8_t>(progress + 25)) {
        FakeDisplay display(size[0], size[1]);
        renderStartupFrame(display, layout, stage, progress, 12345);
        CHECK_EQ(display.outOfBounds(), 0);
      }
    }
    for (const char *overlay : overlays) {
      FakeDisplay display(size[0], size[1]);
      renderOverlay(display, layout, overlay);
      CHECK_EQ(display.outOfBounds(), 0);
    }
  }
}

void testOverlayWrapsInsteadOfTruncating() {
  // 32x16 放不下一行 "CAM FAIL"（48 像素），应该在空格处拆成两行而不是截断。
  const DisplayLayout compact = makeDisplayLayout(32, 16);
  FakeDisplay wrapped(32, 16);
  renderOverlay(wrapped, compact, "CAM FAIL");
  CHECK_EQ(wrapped.outOfBounds(), 0);
  CHECK(!wrapped.rowEmpty(1));   // 第一行 CAM
  CHECK(!wrapped.rowEmpty(9));   // 第二行 FAIL
  // FAIL 是 24 像素宽，居中后从 x=4 起，最后一个字形占到 x=26。
  CHECK_EQ(wrapped.rightmostLitColumn(), 26);

  // 128x64 上放得下整行，用 scale=2：字形跨 14 行。
  const DisplayLayout rich = makeDisplayLayout(128, 64);
  FakeDisplay big(128, 64);
  renderOverlay(big, rich, "CAM FAIL");
  CHECK_EQ(big.outOfBounds(), 0);
  CHECK_EQ(big.bottommostLitRow() - big.topmostLitRow() + 1, 14);
}

} // namespace

int main() {
  testTextMetrics();
  testRichLayoutMatchesLegacyOled();
  testMinimalLayout32x8();
  testCompactLayout32x16();
  testCompactLayout32x10();
  testLayoutNeverOverflows();
  testFrameTimerText();
  testFrameStateBlink();
  testFrameProbAndBar();
  testRender32x8FitsCountdown();
  testRender32x16ShowsBar();
  testRender128x64HasAllThreeRows();
  testStartupAndOverlayNeverOverflow();
  testOverlayWrapsInsteadOfTruncating();

  if (gFailures == 0) {
    printf("display_layout: %d checks passed\n", gChecks);

    // 把 32x8 的实际帧打出来，和 industrial-design/tech1-cnc-r1 的
    // display_lit 渲染肉眼对照。
    const DisplayLayout layout = makeDisplayLayout(32, 8);
    FakeDisplay display(32, 8);
    renderDisplayFrame(display, layout, sittingFrame(layout, 45UL * 60UL * 1000UL, 45UL * 60UL * 1000UL));
    display.dump("matrix-a 32x8 countdown");

    const DisplayLayout compact = makeDisplayLayout(32, 16);
    FakeDisplay compactDisplay(32, 16);
    renderDisplayFrame(compactDisplay, compact, sittingFrame(compact, 18UL * 60UL * 1000UL, 45UL * 60UL * 1000UL));
    compactDisplay.dump("matrix-b 32x16 countdown + bar");
    return 0;
  }
  printf("display_layout: %d/%d checks FAILED\n", gFailures, gChecks);
  return 1;
}
