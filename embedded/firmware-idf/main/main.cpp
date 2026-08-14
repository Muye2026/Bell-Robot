// Bell Robot ESP32-S3 久坐提醒固件入口。
//
// 模块职责划分：
//   app_state         共享状态 / NVS 设置 / 按钮 / 采样 / 状态 JSON
//   sedentary_timer   久坐计时状态机（纯逻辑，可主机单测）
//   presence_detector 摄像头采集 + 桌前坐姿检测
//   buzzer_player     蜂鸣器 + 提示旋律播放
//   display_ui        OLED 启动画面 / 倒计时界面 / overlay
//   web_ui            本地 HTTP 接口与页面
//   ota_update        AP 网页 OTA 升级
//   wifi_net          AP / STA 网络模式
//   cloud_client      云中转轮询与命令执行
//
// 主循环只做编排：采样 -> 按钮 -> 计时 -> 蜂鸣器 -> 显示 -> 日志。

#include "app_config.h"
#include "app_state.h"
#include "buzzer_player.h"
#include "cloud_client.h"
#include "display_ui.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "presence_detector.h"
#include "sample_store.h"
#include "sedentary_timer.h"
#include "web_ui.h"
#include "wifi_net.h"

using namespace bell_robot;

namespace {
constexpr char kTag[] = "bell_robot";
uint32_t lastStatusLogMs = 0;

void logStatus(bool isPresent, uint32_t nowMs) {
  if (nowMs - lastStatusLogMs < 5000) {
    return;
  }
  lastStatusLogMs = nowMs;

  const PresenceDiagnostics diag = presenceDetector.diagnostics();
  ESP_LOGI(kTag,
           "tick state=%s pose=%s raw=%c on=%u off=%u model=%c prob=%u th=%u diff=%u base=%u btn=%c sit=%lu away=%lu",
           stateLabel(sedentaryTimer.state()),
           isPresent ? "sit" : "away",
           diag.rawPresent ? 'Y' : 'N',
           diag.onFrames,
           diag.offFrames,
           diag.modelReady ? 'Y' : 'N',
           static_cast<unsigned>(diag.modelProbability * 100.0f),
           static_cast<unsigned>(MODEL_OCCUPIED_THRESHOLD * 100.0f),
           diag.diff,
           diag.baseline,
           isButtonDown() ? 'L' : 'H',
           static_cast<unsigned long>(minutesFromMs(sedentaryTimer.sitTargetMs())),
           static_cast<unsigned long>(minutesFromMs(sedentaryTimer.awayResetMs())));
}
} // namespace

extern "C" void app_main(void) {
  const uint32_t bootStartMs = millis32();
  buzzerPlayer.begin();
  displayInit();
  startStartupFeedback(bootStartMs);

  nvsInit();
  loadTimerSettings();
  if (ENABLE_CLOUD_REMOTE) {
    loadCloudSettings();
  } else {
    setCloudError("disabled");
  }
  setupButtons();
  ESP_ERROR_CHECK(sample_store::init());
  if (ENABLE_OLED_DIAGNOSTICS) {
    runStartupDiagnosticsOverride(bootStartMs);
  }

  setStartupStage(StartupStage::Camera, millis32());
  const bool cameraOk = presenceDetector.begin();
  logStartupMilestone(cameraOk ? "camera_ready" : "camera_failed", millis32());

  setStartupStage(StartupStage::Wifi, millis32());
  startWifi();
  logStartupMilestone("wifi_ready", millis32());
  setStartupStage(StartupStage::Server, millis32());
  startWebServer();
  logStartupMilestone("web_ready", millis32());
  if (ENABLE_CLOUD_REMOTE) {
    startCloudPollTask();
  }

  stopStartupFeedback(millis32());
  if (!cameraOk) {
    showDisplayOverlay("CAM FAIL", millis32(), 3000);
  }
  ESP_LOGI(kTag, "ready. local URL: http://192.168.4.1/ when AP is active");
  while (true) {
    const uint32_t nowMs = millis32();
    if (rebootAtMs != 0 && static_cast<int32_t>(nowMs - rebootAtMs) >= 0) {
      esp_restart();
    }
    const bool isPresent = presenceDetector.update(nowMs);
    updateButtons(nowMs);
    sedentaryTimer.update(isPresent, nowMs);
    buzzerPlayer.update(nowMs, sedentaryTimer.state() == TimerState::Alerting);
    drawDisplay(isPresent, nowMs);
    logStatus(isPresent, nowMs);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
