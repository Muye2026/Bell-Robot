# Bell Robot ESP-IDF Firmware

ESP32-S3 N16R8 CAM 久坐提醒固件。设备本地完成坐姿识别、倒计时、OLED 显示和蜂鸣器提醒。当前开发默认 AP-only：设备启动后只开启 `Bell-Robot` 热点，手机访问 `http://192.168.4.1/` 使用本地网页。STA 云中转代码保留，可通过 `ENABLE_CLOUD_REMOTE` 切回。

## 网络模式

当前默认：

```cpp
static constexpr bool ENABLE_CLOUD_REMOTE = false;
```

- `false`：AP-only，本地开发模式；不连接路由器 Wi-Fi，不启动云轮询，不显示云配置表单。
- `true`：STA 云远程模式；优先连接路由器 Wi-Fi，失败或云端长期不可达时回到 AP 兜底。

切回云端时只需要把该值改为 `true`，重新构建并烧录。NVS 中已有的 Wi-Fi、服务器地址、`device_id` 和 token 会保留。

## 代码结构（2026-08-14 拆分后）

`main/` 下按模块组织，`main.cpp` 只保留启动编排和主循环：

| 文件 | 职责 |
| --- | --- |
| `main.cpp` | 启动流程 + 主循环编排 + 状态日志 |
| `app_state.h/.cpp` | 共享状态、NVS 设置、设备身份、按键、采样、状态 JSON |
| `sedentary_timer.h/.cpp` | 久坐计时状态机（纯逻辑，可主机单测） |
| `presence_detector.h/.cpp` | 摄像头采集 + 桌前坐姿检测（模型优先 + ROI 差分回退） |
| `buzzer_player.h/.cpp` | 蜂鸣器驱动 + 到时旋律/短提示播放 |
| `display_ui.h/.cpp` | OLED 启动画面、倒计时界面、overlay、OLED 自检 |
| `web_ui.h/.cpp` | 本地 HTTP 接口与页面 |
| `ota_update.h/.cpp` | AP 网页 OTA 升级 |
| `wifi_net.h/.cpp` | AP / STA 网络模式切换 |
| `cloud_client.h/.cpp` | 云中转轮询与命令执行 |
| `sample_store.h/.cpp` | SPIFFS 采样缓存 |
| `seat_model.h/.cpp` | 8x8 灰度 int8 逻辑回归推理 |

主机单元测试在 `test/`（`sedentary_timer` 状态机），不依赖 ESP-IDF。

## 构建

### macOS / Linux

```bash
cd embedded/firmware-idf
./tools/build.sh                    # 构建（默认找 ~/esp/esp-idf-v6.0.2，可用 IDF_PATH 覆盖）
./tools/build.sh set-target         # 强制重新指定 esp32s3 目标
./tools/build.sh --flash /dev/cu.usbmodem01
./tools/test-host.sh                # 主机端状态机单元测试
```

### Windows

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

`sdkconfig.defaults` 已锁定 `CONFIG_IDF_TARGET="esp32s3"`，全新 clone 后直接 `idf.py build` 即可（无需手动 set-target）。

## 烧录

- 芯片：ESP32-S3（最近一次目标串口 `COM13`）
- AP：`Bell-Robot / 12345678`
- 本地地址：`http://192.168.4.1/`

## 网页 OTA 升级

主页有 `Firmware` 入口（`/ota`）：

1. 手机连 `Bell-Robot` 热点，打开 `http://192.168.4.1/`，点 `Firmware`。
2. 选择本地构建出的 `build/bell_robot_seat_model.bin`（≤ 5MB），点 Upload。
3. 设备写入空闲 OTA 分区、校验后自动重启；计时设置与样本缓存保留。

- `GET /ota` 上传页面；`POST /ota` 接收 `application/octet-stream` 字节流。
- 分区表为 `ota_0/ota_1` 双分区，OTA 永远写入非运行分区，升级失败不影响当前固件。
- 本功能已编译验证；实机上传与重启流程待上板确认。

## 本地接口

- `/`：摄像头预览、计时设置、重置、样本采集入口。预览为 JS 每 500ms 拉取 `/capture` 单帧（页面隐藏时自动暂停），不再使用 `/stream` 常驻连接——esp_http_server 是单任务模型，常驻流会阻塞其它所有请求
- `/ota`：固件升级页面（见上）
- `/stream`：MJPEG 流（保留，直连使用；打开期间会独占服务器任务）
- `/capture`：当前 JPEG 画面（640x480 已旋转）
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

## 到时提示音旋律

- 倒计时到时（`Alerting` 状态）的提示音从原来固定 2400Hz 方波改为可选旋律，乐谱定义在 `main/buzzer_music.h`，播放器在 `main/buzzer_player.cpp`。
- 无源蜂鸣器由 LEDC PWM 驱动，播放器逐音符改变方波频率，整套播放非阻塞，不影响计时与摄像头逻辑。
- 在 `main/app_config.h` 用 `BUZZER_ALERT_MELODY` 选择：`0` 超级马里奥过关音（默认）、`1` 最终幻想胜利号角、`2` 升级小铃声。
- 到时后循环播放，每遍之间静音 `BUZZER_ALERT_REPEAT_GAP_MS`（默认 1200ms），起身离场后自动消音复位。
- 新增旋律：在 `buzzer_music.h` 按 `{频率Hz, 时长ms}` 加一张表并登记到 `kBuzzerMelodies`，再用新编号引用。

## 分区

当前使用 `partitions.csv`：

- `nvs`：24KB
- `phy_init`：4KB
- `ota_0`：5.5MB（应用 A）
- `ota_1`：5.5MB（应用 B，OTA 目标）
- `samples`：4MB SPIFFS，用于暂存 JPEG 和元数据

分区表已从 `factory` 单分区改为双 OTA 分区。烧录时需要重刷 `bootloader + partition_table + app`，不能只刷 app；分区偏移变化会清空原有 samples 缓存。

## CI

`.github/workflows/ci.yml` 在 push / PR 时自动运行：

1. 主机端状态机单元测试（`tools/test-host.sh`）
2. 模型脚本语法与 CLI 冒烟测试
3. ESP-IDF（espressif/idf 镜像）完整构建 esp32s3 固件并上传产物

## 当前验证

- 2026-05-01：云中转、多设备管理、自动设备 ID 和隐藏 token 已验证。
- 2026-05-10：主线摄像头切换为 `DC5640 AF 120度`，固件仍按 OV5640 类接口识别成功。
- 2026-05-10：STA 云远程模式已验证，设备 `bell-robot-f63910` 可持续在线并返回远程 JPEG。
- 2026-05-20：新增 `ENABLE_CLOUD_REMOTE=false` 默认 AP-only 模式，便于继续本地开发。
- 2026-06-03：新增第二按钮采样缓存、`/samples` 页面和 4MB SPIFFS 样本分区。
- 2026-06-07：到时提示音改为可选旋律（默认马里奥过关音），新增 `buzzer_music.h` 与非阻塞旋律播放器。已在主机端模拟三段乐谱的音符时序（均按序播完并停止，频率 392–2093Hz），并用方波合成试听确认；`idf.py build` 上板编译待用户在本地 ESP-IDF 环境验证。
- 2026-08-14：工程基础改造。① `main.cpp` 按模块拆分（sedentary_timer / presence_detector / buzzer_player / display_ui / web_ui / ota_update / wifi_net / cloud_client / app_state），行为保持等价；② 新增 `sedentary_timer` 主机端单元测试（53 项断言全过，`tools/test-host.sh`）；③ 新增 `/ota` 网页固件升级与 `ota_0/ota_1` 双 OTA 分区表（samples 仍为 4MB）；④ `sdkconfig.defaults` 锁定 `esp32s3` 目标（与 `build-idf.ps1` 一致，修正此前本机默认 esp32 目标的偏差）；⑤ 新增 `tools/build.sh`（macOS/Linux）、GitHub Actions CI（主机测试 + 模型脚本冒烟 + ESP-IDF 构建）。本机 ESP-IDF v6.0.2 干净构建通过（app 1.0MB / 分区余量 82%）。
- 2026-08-14：实机验证（串口日志）。全量刷写后设备正常启动：boot → OLED 首帧 258ms → 摄像头（OV5640 + 自动对焦后台完成，focused）→ AP `Bell-Robot` + DHCP → 网页服务 → 倒计时首帧 1661ms；NVS 计时设置保留（sit=45 / away=2）；坐姿识别模型概率 89–90%，状态机 WAIT→SIT 转移正常；samples SPIFFS 分区挂载正常。`/ota` 上传与双分区启动切换仍需手机端走一遍验证。
