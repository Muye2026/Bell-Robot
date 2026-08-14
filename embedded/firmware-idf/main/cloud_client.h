#pragma once

// 云中转客户端：STA 模式下每秒轮询 /device/poll 上报状态并执行云端命令
// （capture / set_settings / reset）。仅在 ENABLE_CLOUD_REMOTE=true 时启动。

namespace bell_robot {

void startCloudPollTask();

} // namespace bell_robot
