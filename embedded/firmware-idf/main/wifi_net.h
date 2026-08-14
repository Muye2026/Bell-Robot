#pragma once

// Wi-Fi 网络模块：AP 直连 / STA 云远程两种模式。
//
// - ENABLE_CLOUD_REMOTE=false（默认）：只开 Bell-Robot 热点。
// - ENABLE_CLOUD_REMOTE=true：云配置完整时优先 STA 连接路由器，
//   失败或超时回退 AP 兜底。
//
// 内部持有 netif / event group 句柄，其他模块通过 wifiModeIsSta() /
// wifiIsConnected() 查询状态。

namespace bell_robot {

void startWifi();

// 当前是否运行在 STA（云远程）模式。
bool wifiModeIsSta();

// STA 是否已拿到 IP（AP 模式下恒为 false）。
bool wifiIsConnected();

// 云端长期不可达时回退：停止当前 Wi-Fi 并只开 AP（不重复初始化驱动）。
void restartWifiApFallback();

} // namespace bell_robot
