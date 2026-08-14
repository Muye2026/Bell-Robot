#include "buzzer_player.h"

#include "app_config.h"
#include "driver/ledc.h"

namespace bell_robot {

void BuzzerPlayer::begin() {
  ledc_timer_config_t timer = {};
  timer.speed_mode = LEDC_LOW_SPEED_MODE;
  timer.duty_resolution = LEDC_TIMER_8_BIT;
  timer.timer_num = LEDC_TIMER_1;
  timer.freq_hz = BUZZER_FREQUENCY_HZ;
  timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer);

  ledc_channel_config_t channel = {};
  channel.gpio_num = PIN_BUZZER;
  channel.speed_mode = LEDC_LOW_SPEED_MODE;
  channel.channel = LEDC_CHANNEL_1;
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = LEDC_TIMER_1;
  channel.duty = 0;
  ledc_channel_config(&channel);
}

void BuzzerPlayer::tone(uint32_t freqHz) {
  if (freqHz == 0) {
    off();
    return;
  }
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, freqHz);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, BUZZER_DUTY);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  active_ = true;
}

void BuzzerPlayer::off() {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  active_ = false;
}

void BuzzerPlayer::startSequence(uint32_t nowMs, uint8_t beeps, uint32_t onMs, uint32_t offMs) {
  if (beeps == 0) {
    return;
  }
  sequence_.active = true;
  sequence_.toneOn = true;
  sequence_.remainingPhases = static_cast<uint8_t>(beeps * 2);
  sequence_.nextToggleMs = nowMs + onMs;
  sequence_.onMs = onMs;
  sequence_.offMs = offMs;
  tone(BUZZER_FREQUENCY_HZ);
}

void BuzzerPlayer::advanceSequence(uint32_t nowMs) {
  if (!sequence_.active) {
    return;
  }
  if (static_cast<int32_t>(nowMs - sequence_.nextToggleMs) < 0) {
    return;
  }
  if (sequence_.remainingPhases > 0) {
    sequence_.remainingPhases--;
  }
  if (sequence_.remainingPhases == 0) {
    sequence_.active = false;
    off();
  } else {
    sequence_.toneOn = !sequence_.toneOn;
    if (sequence_.toneOn) {
      tone(BUZZER_FREQUENCY_HZ);
      sequence_.nextToggleMs = nowMs + sequence_.onMs;
    } else {
      off();
      sequence_.nextToggleMs = nowMs + sequence_.offMs;
    }
  }
}

const BuzzerMelody &BuzzerPlayer::activeAlertMelody() {
  int id = BUZZER_ALERT_MELODY;
  if (id < 0 || static_cast<size_t>(id) >= kBuzzerMelodyCount) {
    id = 0;
  }
  return kBuzzerMelodies[id];
}

void BuzzerPlayer::startAlertMelody(uint32_t nowMs, const BuzzerMelody &melody) {
  if (melody.notes == nullptr || melody.count == 0) {
    return;
  }
  melody_.notes = melody.notes;
  melody_.count = melody.count;
  melody_.index = 0;
  melody_.active = true;
  tone(melody.notes[0].freqHz);
  melody_.segmentEndMs = nowMs + melody.notes[0].durationMs;
}

void BuzzerPlayer::stopAlertMelody() {
  melody_.active = false;
  off();
}

void BuzzerPlayer::advanceAlertMelody(uint32_t nowMs) {
  if (!melody_.active) {
    return;
  }
  if (static_cast<int32_t>(nowMs - melody_.segmentEndMs) < 0) {
    return;
  }
  melody_.index++;
  if (melody_.index >= melody_.count) {
    melody_.active = false;
    off();
    return;
  }
  const BuzzerNote &note = melody_.notes[melody_.index];
  tone(note.freqHz);
  melody_.segmentEndMs = nowMs + note.durationMs;
}

void BuzzerPlayer::update(uint32_t nowMs, bool alerting) {
  if (sequence_.active) {
    advanceSequence(nowMs);
    return;
  }

  if (alerting) {
    // 到时：循环播放选定的提示音旋律，每播完一遍静音
    // BUZZER_ALERT_REPEAT_GAP_MS 再重播，直到用户起身（reset 停止）。
    if (melody_.active) {
      advanceAlertMelody(nowMs);
      if (!melody_.active) {
        melodyGapUntilMs_ = nowMs + BUZZER_ALERT_REPEAT_GAP_MS;
      }
    } else if (static_cast<int32_t>(nowMs - melodyGapUntilMs_) >= 0) {
      startAlertMelody(nowMs, activeAlertMelody());
    }
    return;
  }

  if (melody_.active) {
    stopAlertMelody();
  }
  melodyGapUntilMs_ = 0;
  off();
}

void BuzzerPlayer::stopAll() {
  sequence_.active = false;
  stopAlertMelody();
  melodyGapUntilMs_ = 0;
  off();
}

BuzzerPlayer buzzerPlayer;

} // namespace bell_robot
