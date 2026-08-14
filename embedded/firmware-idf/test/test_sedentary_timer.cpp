// SedentaryTimer 主机端单元测试。
//
// 只依赖 sedentary_timer.cpp + app_config.h（纯 constexpr 配置），
// 不链接任何 ESP-IDF 代码。用 tools/test-host.sh 构建并运行。

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sedentary_timer.h"

using bell_robot::SedentaryTimer;
using bell_robot::TimerState;
using bell_robot::displayStateLabel;
using bell_robot::minutesFromMs;
using bell_robot::msFromMinutes;
using bell_robot::stateLabel;
using bell_robot::validTimerMinutes;

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

void expectState(const SedentaryTimer &timer, TimerState expected,
                 const char *what) {
  ++gChecks;
  if (timer.state() != expected) {
    ++gFailures;
    printf("FAIL %s: state=%s expected=%s\n", what,
           stateLabel(timer.state()), stateLabel(expected));
  }
}
} // namespace

// 基础：无人时保持 Idle，有人开始计时。
static void testIdleToSitting() {
  SedentaryTimer t;
  t.configure(msFromMinutes(45), msFromMinutes(1));
  expectState(t, TimerState::Idle, "initial state");
  CHECK(t.elapsedSittingMs(0) == 0);
  CHECK(t.remainingSitMs(0) == msFromMinutes(45));

  t.update(false, 1000);
  expectState(t, TimerState::Idle, "still empty stays idle");
  CHECK(t.elapsedSittingMs(1000) == 0);

  t.update(true, 2000);
  expectState(t, TimerState::Sitting, "present starts sitting");
  CHECK(t.sitStartMs() == 2000);
  CHECK(t.elapsedSittingMs(3000) == 1000);
  CHECK(t.remainingSitMs(3000) == msFromMinutes(45) - 1000);
}

// 坐满目标 → Alerting；起身 → 复位 Idle。
static void testSitToAlert() {
  SedentaryTimer t;
  t.configure(1000, 60000);
  t.update(true, 0);
  t.update(true, 500);
  expectState(t, TimerState::Sitting, "not yet full");
  t.update(true, 1000);
  expectState(t, TimerState::Alerting, "target reached alerts");
  CHECK(t.remainingSitMs(1000) == 0);
  t.update(true, 1500);
  expectState(t, TimerState::Alerting, "alert persists while seated");
  t.update(false, 2000);
  expectState(t, TimerState::Idle, "standing up clears alert and resets");
  CHECK(t.elapsedSittingMs(2000) == 0);
}

// 暂离宽容期：AWAY_GRACE_MS 内回来 → 倒计时按暂停续算。
static void testAwayGracePauseAndResume() {
  SedentaryTimer t;
  t.configure(msFromMinutes(45), msFromMinutes(1));
  t.update(true, 0);
  t.update(true, 30000);
  CHECK(t.elapsedSittingMs(30000) == 30000);

  t.update(false, 35000); // 离开
  expectState(t, TimerState::AwayGrace, "absence enters grace");
  CHECK(t.elapsedSittingMs(40000) == 35000); // 冻结在离开时刻 35000

  t.update(true, 42000); // 宽容期内回来（离场 7s）
  expectState(t, TimerState::Sitting, "return within grace resumes");
  CHECK(t.elapsedSittingMs(50000) == 43000); // 35000 + 回来后的 8s，离场 7s 不计入
}

// 超过宽容期进入警告，仍不回来超过容忍 → 整轮重置。
static void testAwayWarningThenReset() {
  SedentaryTimer t;
  t.configure(msFromMinutes(45), msFromMinutes(1));
  t.update(true, 0);
  t.update(true, 10000);
  t.update(false, 11000);
  expectState(t, TimerState::AwayGrace, "grace first");

  t.update(false, 21000); // 离开 10s，超过 AWAY_GRACE_MS
  expectState(t, TimerState::AwayWarning, "beyond grace warns");

  t.update(false, 71000); // 离开 60s，超过容忍
  expectState(t, TimerState::Idle, "beyond tolerance resets");
  CHECK(t.elapsedSittingMs(71000) == 0);
}

// 警告期内回来 → 续算。
static void testResumeFromWarning() {
  SedentaryTimer t;
  t.configure(msFromMinutes(45), msFromMinutes(1));
  t.update(true, 0);
  t.update(true, 20000);
  t.update(false, 21000);
  t.update(false, 32000); // AwayWarning
  expectState(t, TimerState::AwayWarning, "warning after grace");
  t.update(true, 40000); // 回来（离开 19s，仍在 60s 容忍内）
  expectState(t, TimerState::Sitting, "return from warning resumes");
  CHECK(t.elapsedSittingMs(50000) == 31000); // 冻结的 21s + 回来后的 10s
}

// configure 与校验边界。
static void testConfigureAndValidation() {
  SedentaryTimer t;
  t.configure(msFromMinutes(30), msFromMinutes(3));
  CHECK(t.sitTargetMs() == msFromMinutes(30));
  CHECK(t.awayResetMs() == msFromMinutes(3));

  CHECK(validTimerMinutes(1, 1));
  CHECK(validTimerMinutes(180, 5));
  CHECK(!validTimerMinutes(0, 1));
  CHECK(!validTimerMinutes(181, 1));
  CHECK(!validTimerMinutes(45, 0));
  CHECK(!validTimerMinutes(45, 6));

  CHECK(minutesFromMs(msFromMinutes(45)) == 45);
  CHECK(minutesFromMs(msFromMinutes(45) + 59999) == 45);
  CHECK(msFromMinutes(1) == 60000);
}

// reset 清空整轮状态。
static void testReset() {
  SedentaryTimer t;
  t.configure(1000, 60000);
  t.update(true, 0);
  t.update(true, 900);
  t.update(false, 1000); // AwayGrace
  t.reset();
  expectState(t, TimerState::Idle, "reset goes idle");
  CHECK(t.sitStartMs() == 0);
  CHECK(t.awayStartMs() == 0);
  CHECK(t.elapsedSittingMs(2000) == 0);

  t.update(true, 3000);
  expectState(t, TimerState::Sitting, "restart after reset works");
}

// 标签映射（日志/显示别改坏了）。
static void testLabels() {
  CHECK(strcmp(stateLabel(TimerState::Idle), "WAIT") == 0);
  CHECK(strcmp(stateLabel(TimerState::Sitting), "SIT") == 0);
  CHECK(strcmp(stateLabel(TimerState::AwayGrace), "AWAY") == 0);
  CHECK(strcmp(stateLabel(TimerState::AwayWarning), "RESET") == 0);
  CHECK(strcmp(stateLabel(TimerState::Alerting), "STAND") == 0);

  CHECK(strcmp(displayStateLabel(TimerState::Idle, true), "SEATED") == 0);
  CHECK(strcmp(displayStateLabel(TimerState::Idle, false), "EMPTY") == 0);
  CHECK(strcmp(displayStateLabel(TimerState::Sitting, true), "SEATED") == 0);
  CHECK(strcmp(displayStateLabel(TimerState::AwayWarning, true), "AWAY") == 0);
  CHECK(strcmp(displayStateLabel(TimerState::Alerting, true), "ALERT") == 0);
}

int main() {
  testIdleToSitting();
  testSitToAlert();
  testAwayGracePauseAndResume();
  testAwayWarningThenReset();
  testResumeFromWarning();
  testConfigureAndValidation();
  testReset();
  testLabels();

  if (gFailures == 0) {
    printf("PASS: %d checks, 0 failures\n", gChecks);
    return 0;
  }
  printf("FAILED: %d of %d checks failed\n", gFailures, gChecks);
  return 1;
}
