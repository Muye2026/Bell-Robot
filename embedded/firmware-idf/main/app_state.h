#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sedentary_timer.h"

// 跨模块共享状态与“组合根”胶水层。
//
// 固件各模块（web_ui / cloud_client / display_ui / wifi_net / main）通过
// 本头文件访问共享对象（计时器、云配置、重启请求等），避免模块间互相
// include。具体定义在 app_state.cpp。

namespace bell_robot {

struct CloudSettings {
  char ssid[33] = {};
  char password[65] = {};
  char serverUrl[160] = {};
  char deviceId[48] = {};
  char token[96] = {};
};

// 采样反馈提示音参数（app_state 与 web_ui 共用）。
constexpr uint32_t kCaptureBeepOnMs = 100;
constexpr uint32_t kCaptureBeepOffMs = 90;
constexpr uint32_t kTransientDisplayMs = 1000;

extern SedentaryTimer sedentaryTimer;
extern CloudSettings cloudSettings;
extern uint32_t rebootAtMs;
extern uint32_t cloudLastPollMs;
extern uint32_t cloudLastSuccessMs;
extern char cloudLastError[64];
extern uint32_t sampleCounter;

// 系统毫秒计时（FreeRTOS tick 无关，使用 esp_timer）。
uint32_t millis32();

void setCloudError(const char *message);
bool cloudSettingsComplete();

// NVS 初始化（erase 兜底）。
void nvsInit();

// 计时设置存取。
void loadTimerSettings();
esp_err_t saveTimerSettings(uint32_t sitMinutes, uint32_t awayMinutes);

// 云配置存取（设备 ID / token 自动生成并落 NVS）。
void loadCloudSettings();
esp_err_t saveCloudSettings(const CloudSettings &settings);
esp_err_t forgetCloudSettings();

// 字符串工具。
void safeCopy(char *dest, size_t destSize, const char *src);
bool startsWith(const char *value, const char *prefix);
bool validServerUrl(const char *url);

// 极简 JSON 取值工具（web 样本列表与云端命令解析共用）。
bool jsonFindString(const char *json, const char *key, char *out, size_t outSize);
bool jsonFindUint(const char *json, const char *key, uint32_t *out);
bool jsonFindBool(const char *json, const char *key, bool *out);
bool jsonFindFloat(const char *json, const char *key, float *out);

// 按键：GPIO1 消警+重校准，GPIO2 采样。
bool isButtonDown();
bool isCaptureButtonDown();
void setupButtons();
// 主循环每周期调用：轮询 + 消抖 + 事件处理。
void updateButtons(uint32_t nowMs);

// 采样：抓一帧 JPEG + 元数据写入 sample_store，返回是否成功。
bool captureAndStoreSample(const char *source, uint32_t nowMs, char *outId, size_t outIdSize);

// 生成 /status 与云端上报共用的状态 JSON。
void buildStatusPayload(char *payload, size_t payloadSize);

// 复位计时并停止提示音（按钮 / 网页 / 云端共用）。
void resetTimer();

// 请求延时重启（云配置变更 / OTA 完成后）。
void requestReboot(uint32_t delayMs);

} // namespace bell_robot
