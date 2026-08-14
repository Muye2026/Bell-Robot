# Bell Robot Embedded

ESP32-S3 久坐提醒器固件。设备本地完成坐姿识别、倒计时、OLED 显示和蜂鸣器提醒。当前开发默认使用 `Bell-Robot` AP 直连；STA 云中转代码保留，需要远程服务器时可通过编译期开关恢复。

## 当前主线

- 固件：`firmware-idf/`（2026-08-14 起按模块拆分，见 `firmware-idf/README.md`）
- 硬件：Freenove ESP32-S3 N16R8 CAM + DC5640 AF 120度 + SPI SSD1306 OLED + 蜂鸣器 + 双按键
- 构建目标：`esp32s3`（`sdkconfig.defaults` 已锁定，Windows 脚本 `tools/build-idf.ps1` 同样执行 `set-target esp32s3`）
- 网络：当前默认 AP-only，热点名 `Bell-Robot`，密码 `12345678`
- 云端：`cloud-relay/` 仍保留；将 `ENABLE_CLOUD_REMOTE` 改为 `true` 后可恢复 STA 云远程
- 设备身份：远程模式下 `device_id` 根据芯片 MAC 自动生成；设备 token 保存在 NVS，AP-only 不会清除这些配置
- 计时：默认坐满 45 分钟提醒，暂离容忍默认 1 分钟；暂离期间倒计时暂停，回来继续，超时重置

## 本地开发使用

1. 手机连接设备热点 `Bell-Robot`。
2. 打开 `http://192.168.4.1/`。
3. 使用本地网页查看摄像头、调整倒计时/离场容忍、重置设备、进入 `/samples` 管理采样缓存，或在 `/ota` 上传新固件（OTA 升级）。

当前 AP-only 固件不会自动连接路由器 Wi-Fi，也不会轮询云服务器。

## 第二按钮采样缓存

- `GPIO1` 保持现有用途：消警 + 重新校准。
- 新增 `GPIO2` 采样按钮：短按一次保存当前 `JPEG` 和同名元数据到设备 SPIFFS。
- 样本不自动打标签，先按 `raw` 数据保存，后续在电脑侧再筛选整理。
- 设备固定保留最新 `64` 组样本；手机连接设备后可在 `/samples` 页面预览、下载、清空。
- 调试时也可以直接调用 `POST /samples/capture` 触发同一套采样流程。

## 切回云端远程

1. 打开 `embedded/firmware-idf/main/app_config.h`。
2. 将 `ENABLE_CLOUD_REMOTE` 改为 `true`。
3. 重新构建并烧录。

设备会继续使用 NVS 里已有的 Wi-Fi、服务器地址、`device_id` 和 token；通常不需要重新配网。

## 构建与烧录

macOS / Linux：

```bash
cd embedded/firmware-idf
./tools/build.sh                       # 构建（默认找 ~/esp/esp-idf-v6.0.2，可用 IDF_PATH 覆盖）
./tools/build.sh --flash /dev/cu.usbmodem01
./tools/test-host.sh                   # 主机端状态机单元测试
```

Windows：

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

注意：分区表现在是 `ota_0/ota_1` 双 OTA 结构，首次烧录必须跟随 `partition-table.bin` 一起完整重刷，不是只刷 app；旧 samples 缓存会因分区偏移变化被清空。

## 网页 OTA 升级

主页 `Firmware` 入口上传 `build/bell_robot_seat_model.bin`（≤5MB）：写入空闲 OTA 分区 → 校验 → 自动重启。计时设置与样本缓存保留，失败不影响当前固件。详见 `firmware-idf/README.md`。

## 模型训练

样本目录：

- `model/dataset/absent/`
- `model/dataset/seated/`

生成固件头文件：

```powershell
cd D:\Project\Bell-Robot
python model\train_seat_model.py --dataset model\dataset --out embedded\firmware-idf\main\seat_model_data.h --balance-classes
```

## 已验证

- 2026-05-01：OLED 主界面精简为状态、倒计时和 `PROB`。
- 2026-05-01：暂离容忍时间内倒计时暂停，回来继续；超过容忍时间重置。
- 2026-05-01：云中转改为多设备管理，取消网页登录密码；固件自动生成 `device_id` 和隐藏 token。
- 2026-05-10：主线摄像头切换为 `DC5640 AF 120度`，固件仍按 OV5640 类接口识别成功。
- 2026-05-20：新增 `ENABLE_CLOUD_REMOTE` 编译开关，当前默认 AP-only，用于本地开发。
- 2026-06-03：新增第二按钮样本缓存，设备侧支持 `/samples` 预览、下载与清空。
- 2026-08-14：工程基础改造。`main.cpp` 按模块拆分（sedentary_timer / presence_detector / buzzer_player / display_ui / web_ui / ota_update / wifi_net / cloud_client / app_state），行为保持等价；新增 `sedentary_timer` 主机端单元测试（53 项断言全过）；新增 `/ota` 网页固件升级与 `ota_0/ota_1` 双 OTA 分区表；`sdkconfig.defaults` 锁定 `esp32s3` 目标；新增 `tools/build.sh`（macOS/Linux）与 GitHub Actions CI（主机测试 + 模型脚本冒烟 + ESP-IDF 构建）。本机 ESP-IDF v6.0.2 干净构建通过。
- 2026-08-14：实机验证（串口日志）。全量刷写后启动正常：OLED 首帧 258ms、摄像头 OV5640 + 自动对焦、AP 与网页服务就绪、倒计时首帧 1661ms；NVS 计时设置保留；坐姿识别模型概率 89–90%，WAIT→SIT 转移正常。`/ota` 上传流程待手机端验证。
