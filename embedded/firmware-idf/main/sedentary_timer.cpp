#include "sedentary_timer.h"

#include "app_config.h"

namespace bell_robot {

SedentaryTimer::SedentaryTimer() {
  settings_.sitTargetMs = DEFAULT_SIT_TARGET_MS;
  settings_.awayResetMs = DEFAULT_AWAY_RESET_MS;
}

void SedentaryTimer::configure(uint32_t sitTargetMs, uint32_t awayResetMs) {
  settings_.sitTargetMs = sitTargetMs;
  settings_.awayResetMs = awayResetMs;
}

void SedentaryTimer::update(bool isPresent, uint32_t nowMs) {
  switch (context_.state) {
  case TimerState::Idle:
    if (isPresent) {
      startSitting(nowMs);
    }
    break;
  case TimerState::Sitting:
    if (!isPresent) {
      context_.state = TimerState::AwayGrace;
      context_.awayStartMs = nowMs;
    } else if (elapsedSittingMs(nowMs) >= settings_.sitTargetMs) {
      context_.state = TimerState::Alerting;
    }
    break;
  case TimerState::AwayGrace: {
    if (isPresent) {
      resumeSittingAfterAway(nowMs);
      break;
    }
    const uint32_t awayMs = nowMs - context_.awayStartMs;
    if (awayMs >= settings_.awayResetMs) {
      reset();
    } else if (awayMs >= AWAY_GRACE_MS) {
      context_.state = TimerState::AwayWarning;
    }
    break;
  }
  case TimerState::AwayWarning:
    if (isPresent) {
      resumeSittingAfterAway(nowMs);
    } else if (nowMs - context_.awayStartMs >= settings_.awayResetMs) {
      reset();
    }
    break;
  case TimerState::Alerting:
    if (!isPresent) {
      reset();
    }
    break;
  }
}

void SedentaryTimer::reset() {
  context_ = TimerContext{};
}

void SedentaryTimer::startSitting(uint32_t nowMs) {
  context_.state = TimerState::Sitting;
  context_.sitStartMs = nowMs;
  context_.awayStartMs = 0;
}

void SedentaryTimer::resumeSittingAfterAway(uint32_t nowMs) {
  context_.sitStartMs += nowMs - context_.awayStartMs;
  context_.awayStartMs = 0;
  context_.state = TimerState::Sitting;
}

uint32_t SedentaryTimer::elapsedSittingMs(uint32_t nowMs) const {
  switch (context_.state) {
  case TimerState::Idle:
    return 0;
  case TimerState::AwayGrace:
  case TimerState::AwayWarning:
    return context_.awayStartMs - context_.sitStartMs;
  case TimerState::Sitting:
  case TimerState::Alerting:
    return nowMs - context_.sitStartMs;
  }
  return 0;
}

uint32_t SedentaryTimer::remainingSitMs(uint32_t nowMs) const {
  const uint32_t elapsed = elapsedSittingMs(nowMs);
  return elapsed >= settings_.sitTargetMs ? 0 : settings_.sitTargetMs - elapsed;
}

uint32_t minutesFromMs(uint32_t ms) {
  return ms / kMsPerMinute;
}

uint32_t msFromMinutes(uint32_t minutes) {
  return minutes * kMsPerMinute;
}

bool validTimerMinutes(uint32_t sitMinutes, uint32_t awayMinutes) {
  return sitMinutes >= kSitMinMinutes && sitMinutes <= kSitMaxMinutes &&
         awayMinutes >= kAwayMinMinutes && awayMinutes <= kAwayMaxMinutes;
}

const char *stateLabel(TimerState state) {
  switch (state) {
  case TimerState::Idle:
    return "WAIT";
  case TimerState::Sitting:
    return "SIT";
  case TimerState::AwayGrace:
    return "AWAY";
  case TimerState::AwayWarning:
    return "RESET";
  case TimerState::Alerting:
    return "STAND";
  }
  return "UNKNOWN";
}

const char *displayStateLabel(TimerState state, bool isPresent) {
  switch (state) {
  case TimerState::Idle:
    return isPresent ? "SEATED" : "EMPTY";
  case TimerState::Sitting:
    return "SEATED";
  case TimerState::AwayGrace:
  case TimerState::AwayWarning:
    return "AWAY";
  case TimerState::Alerting:
    return "ALERT";
  }
  return "UNKNOWN";
}

} // namespace bell_robot
