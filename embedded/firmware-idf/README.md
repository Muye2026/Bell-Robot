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
- `/samples`：设备侧样本缓存页面，可预览、下载、清空
- `/samples/list`：样本列表 JSON
- `/samples/file?id=...`：读取单张 JPEG
- `/samples/meta?id=...`：读取单张元数据 JSON
- `/samples/clear`：清空全部缓存样本
- `/samples/capture`：调试入口，触发一次与第二按钮相同的采样流程

AP-only 模式不注册 `/cloud` 和 `/cloud/forget`。

## 第二按钮采样

- 新增独立采样按钮：`GPIO2 -> 按钮 -> GND`，沿用 `INPUT_PULLUP`。
- 现有 `GPIO1` 按钮行为不变，仍然负责“消警 + 重新校准”。
- 第二按钮短按一次后，设备会保存一张当前 `JPEG` 和同名 `.json` 元数据，页面与蜂鸣器会给出成功/失败反馈。
- 元数据包含 `sample_id`、`boot_ms`、`state`、`present`、`model_prob`、`wifi_mode`、`jpeg_bytes` 等运行时字段。
- 设备端固定保留最新 `64` 组样本，超出后自动淘汰最旧样本。

## 分区

当前使用 `partitions.csv`：

- `nvs`：24KB
- `phy_init`：4KB
- `factory`：6MB
- `samples`：4MB SPIFFS，用于暂存 JPEG 和元数据

分区表已变更，烧录时需要重刷 `bootloader + partition_table + app`，不能只刷 app。

## 当前验证

- 2026-05-01：云中转、多设备管理、自动设备 ID 和隐藏 token 已验证。
- 2026-05-10：主线摄像头切换为 `DC5640 AF 120度`，固件仍按 OV5640 类接口识别成功。
- 2026-05-10：STA 云远程模式已验证，设备 `bell-robot-f63910` 可持续在线并返回远程 JPEG。
- 2026-05-20：新增 `ENABLE_CLOUD_REMOTE=false` 默认 AP-only 模式，便于继续本地开发。
- 2026-06-03：新增第二按钮采样缓存、`/samples` 页面和 4MB SPIFFS 样本分区。
