#pragma once

#include "esp_http_server.h"

// 本地 Web 服务：摄像头预览、状态、计时设置、样本管理、云配置与 OTA 页面。
//
// startWebServer() 注册全部路由；AP-only 模式不注册 /cloud 相关路由。
// HTML 页面内联在 web_ui.cpp 中，避免引入外部资源依赖。

namespace bell_robot {

void startWebServer();

} // namespace bell_robot
