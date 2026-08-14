#pragma once

#include <stdint.h>

// OLED 显示模块：启动画面、倒计时主界面、瞬时提示 overlay 与 OLED 自检。
//
// 内部持有 Ssd1306Spi 实例与所有显示状态（lastDisplayMs / overlay /
// startupFeedback），对外只暴露函数式接口，主循环不需要接触底层对象。

namespace bell_robot {

enum class StartupStage {
  Boot,
  Camera,
  Wifi,
  Server,
  Ready,
};

// OLED 初始化（上电后、startStartupFeedback 之前调用）。
void displayInit();

// 启动反馈：BOOT/CAMERA/WIFI/SERVER 进度条 + 可选开机旋律（非阻塞）。
void startStartupFeedback(uint32_t bootStartMs);
void setStartupStage(StartupStage stage, uint32_t nowMs);
void stopStartupFeedback(uint32_t nowMs);
void logStartupMilestone(const char *name, uint32_t nowMs);

// ENABLE_OLED_DIAGNOSTICS 时在启动前先跑一遍 OLED 自检画面。
void runStartupDiagnosticsOverride(uint32_t bootStartMs);

// 瞬时提示（CAPTURED / SAVE ERR / CAM FAIL），覆盖主界面一段时间。
void showDisplayOverlay(const char *message, uint32_t nowMs, uint32_t durationMs);

// 主循环每周期调用：绘制倒计时主界面。
void drawDisplay(bool isPresent, uint32_t nowMs);

void runOledDiagnostics();

} // namespace bell_robot
