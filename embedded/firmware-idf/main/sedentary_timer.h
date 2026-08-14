#pragma once

#include <stdint.h>

// 久坐计时状态机（纯逻辑模块，不依赖 FreeRTOS / ESP-IDF）。
//
// 状态转移：
//   Idle        --有人--> Sitting
//   Sitting     --无人--> AwayGrace  --超过 AWAY_GRACE_MS 仍无人--> AwayWarning
//   AwayGrace/AwayWarning --回来--> Sitting（倒计时按暂停续算）
//   AwayGrace/AwayWarning --超过 awayResetMs 仍无人--> Idle（重置）
//   Sitting     --坐满 sitTargetMs--> Alerting
//   Alerting    --起身--> Idle（自动消警并结束本轮）
//
// 该模块与硬件无关，可在 macOS / Linux 主机上直接编译并跑单元测试，
// 测试入口见 firmware-idf/test/test_sedentary_timer.cpp。

namespace bell_robot {

enum class TimerState {
  Idle,
  Sitting,
  AwayGrace,
  AwayWarning,
  Alerting,
};

struct TimerSettings {
  uint32_t sitTargetMs = 0;
  uint32_t awayResetMs = 0;
};

constexpr uint32_t kMsPerMinute = 60UL * 1000UL;
constexpr uint32_t kSitMinMinutes = 1;
constexpr uint32_t kSitMaxMinutes = 180;
constexpr uint32_t kAwayMinMinutes = 1;
constexpr uint32_t kAwayMaxMinutes = 5;

class SedentaryTimer {
public:
  SedentaryTimer();

  // 应用新的计时设置（毫秒）。坐满目标和离场容忍的最小/最大值由
  // validTimerMinutes() 按分钟数校验。
  void configure(uint32_t sitTargetMs, uint32_t awayResetMs);

  // 每个主循环周期调用一次。
  void update(bool isPresent, uint32_t nowMs);

  // 重置整轮计时，回到 Idle。
  void reset();

  // 直接进入 Sitting（供测试与特殊流程使用）。
  void startSitting(uint32_t nowMs);

  TimerState state() const { return context_.state; }
  uint32_t sitTargetMs() const { return settings_.sitTargetMs; }
  uint32_t awayResetMs() const { return settings_.awayResetMs; }
  uint32_t sitStartMs() const { return context_.sitStartMs; }
  uint32_t awayStartMs() const { return context_.awayStartMs; }

  // 本轮已累计的久坐时长（暂离期间按离开时刻冻结）。
  uint32_t elapsedSittingMs(uint32_t nowMs) const;

  // 距离坐满目标还剩多少毫秒。
  uint32_t remainingSitMs(uint32_t nowMs) const;

private:
  struct TimerContext {
    TimerState state = TimerState::Idle;
    uint32_t sitStartMs = 0;
    uint32_t awayStartMs = 0;
  };

  void resumeSittingAfterAway(uint32_t nowMs);

  TimerContext context_;
  TimerSettings settings_;
};

uint32_t minutesFromMs(uint32_t ms);
uint32_t msFromMinutes(uint32_t minutes);
bool validTimerMinutes(uint32_t sitMinutes, uint32_t awayMinutes);

// 状态标签：日志用。
const char *stateLabel(TimerState state);

// 状态标签：OLED / 网页显示用。
const char *displayStateLabel(TimerState state, bool isPresent);

} // namespace bell_robot
