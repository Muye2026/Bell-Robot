#pragma once

#include <stddef.h>
#include <stdint.h>

#include "buzzer_music.h"

// 蜂鸣器驱动与非阻塞提示音播放器。
//
// - 无源蜂鸣器由 LEDC PWM 驱动，tone() 每次重设方波频率（0 = 休止）。
// - startSequence() 播放“滴-滴-滴”式短提示（采样成功/失败反馈）。
// - update() 每个主循环周期调用一次：在 Alerting 状态下循环播放
//   BUZZER_ALERT_MELODY 选定的到时旋律，每遍之间静音
//   BUZZER_ALERT_REPEAT_GAP_MS；非 Alerting 时停止旋律。
// - stopAll() 供 resetTimer 使用（停止一切并静音）。

namespace bell_robot {

class BuzzerPlayer {
public:
  void begin();

  // 播放指定频率方波；freqHz 为 0 表示休止（静音）。
  void tone(uint32_t freqHz);
  void off();
  bool isOn() const { return active_; }

  // 短提示序列：beeps 声，每声 onMs，间隔 offMs。
  void startSequence(uint32_t nowMs, uint8_t beeps, uint32_t onMs, uint32_t offMs);

  // 主循环驱动。alerting 为 true 时循环播放到时旋律。
  void update(uint32_t nowMs, bool alerting);

  // 停止序列与旋律并静音。
  void stopAll();

private:
  struct SequenceState {
    bool active = false;
    bool toneOn = false;
    uint8_t remainingPhases = 0;
    uint32_t nextToggleMs = 0;
    uint32_t onMs = 0;
    uint32_t offMs = 0;
  };

  struct AlertMelodyState {
    const BuzzerNote *notes = nullptr;
    size_t count = 0;
    size_t index = 0;
    uint32_t segmentEndMs = 0;
    bool active = false;
  };

  void advanceSequence(uint32_t nowMs);
  void startAlertMelody(uint32_t nowMs, const BuzzerMelody &melody);
  void stopAlertMelody();
  void advanceAlertMelody(uint32_t nowMs);
  const BuzzerMelody &activeAlertMelody();

  bool active_ = false;
  SequenceState sequence_;
  AlertMelodyState melody_;
  uint32_t melodyGapUntilMs_ = 0;
};

extern BuzzerPlayer buzzerPlayer;

} // namespace bell_robot
