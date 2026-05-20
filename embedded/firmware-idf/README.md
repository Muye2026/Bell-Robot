# Bell Robot ESP-IDF Firmware

ESP32-S3 N16R8 CAM 久坐提醒固件。当前开发默认 AP-only：设备启动后只开启 `Bell-Robot` 热点，手机访问 `http://192.168.4.1/` 使用本地网页。STA 云中转代码保留，可通过 `ENABLE_CLOUD_REMOTE` 切回。

## 网络模式

当前默认：

```cpp
static constexpr bool ENABLE_CLOUD_REMOTE = false;
```

- `false`：AP-only，本地开发模式；不连接路由器 Wi-Fi，不启动云轮询，不显示云配置表单。
- `true`：STA 云远程模式；优先连接路由器 Wi-Fi，失败或云端长期不可达时回到 AP 兜底。

切回云端时只需要把该值改为 `true`，重新构建并烧录。NVS 中已有的 Wi-Fi、服务器地址、`device_id` 和 token 会保留。

## 构建

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
```

## 烧录

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

最近一次目标：

- 串口：`COM13`
- 芯片：ESP32-S3
- AP：`Bell-Robot / 12345678`
- 本地地址：`http://192.168.4.1/`

## 本地接口

- `/`：摄像头预览、计时设置、重置、样本采集入口
- `/capture`：当前 JPEG 画面
- `/status`：状态 JSON，包含模型概率、计时、联网诊断和 `cloud_enabled`
- `/settings`：读取/保存倒计时和离场容忍分钟数
- `/reset`：重置当前计时并重新校准
- `/label?class=absent|seated`：下载一帧 `8x8` PGM 样本

AP-only 模式不注册 `/cloud` 和 `/cloud/forget`。

## 分区

当前使用 `partitions.csv`：

- `nvs`：24KB
- `phy_init`：4KB
- `factory`：3MB

## 当前验证

- 2026-05-01：云中转、多设备管理、自动设备 ID 和隐藏 token 已验证。
- 2026-05-10：主线摄像头切换为 `DC5640 AF 120度`，固件仍按 OV5640 类接口识别成功。
- 2026-05-10：STA 云远程模式已验证，设备 `bell-robot-f63910` 可持续在线并返回远程 JPEG。
- 2026-05-20：新增 `ENABLE_CLOUD_REMOTE=false` 默认 AP-only 模式，便于继续本地开发。
