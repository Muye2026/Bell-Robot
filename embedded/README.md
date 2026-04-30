# 久坐提醒外置硬件

本项目开发一个独立运行的久坐提醒硬件：通过 ESP32-S3 N16R8 CAM + OV5640CSP 判断设备前方是否有人保持桌前坐姿，连续坐满 45 分钟后通过 OLED 和无源蜂鸣器提醒站立。设备预计放在显示器上或桌面上，摄像头主要看到人的正面或前斜侧面坐姿，而不是完整椅子画面。图像只用于本地坐姿/离开判断，不做人脸识别，不上传图像。

## 当前主线

- 主控：Freenove ESP32-S3 N16R8 CAM，16MB Flash + 8MB PSRAM。
- 摄像头：OV5640CSP，丝印 `VVS OV5640CSP 8225N VC`。
- 显示：SPI SSD1306 128x64 OLED，丝印 `GND / VCC / SCL / SDA / RES / DCC / CS`。
- 提醒：两引脚无源蜂鸣器，PWM 方波驱动。
- 交互：单按键用于确认提醒、重置计时和重新校准。
- 识别：ESP-IDF 主线固件使用本地 int8 桌前坐姿模型；无训练模型时自动回退到 ROI 灰度差分。
- 计时：坐下后 45 分钟倒计时；离开 5 秒后提示即将重置；离开超过 10 秒后本轮计时作废。
- 固件入口：`firmware-idf/` 是当前唯一主线；`firmware/` 只作为 legacy fallback 保留到 `firmware-idf` 完成整机实机验证。

## 目录结构

```text
embedded/
  AGENTS.md
  README.md
  .gitignore
  docs/
    architecture.md
    bom.md
    pinout.md
    pretrain-data.md
  firmware/              # legacy PlatformIO + Arduino 回退工程，暂不删除
  firmware-idf/          # 当前唯一主线 ESP-IDF 工程
    main/
    model/
```

## 固件构建

ESP-IDF 主线：

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
idf.py set-target esp32s3
idf.py build
idf.py -p COM13 flash monitor
```

旧 Arduino 回退工程，仅用于回退对比，不再承载新功能开发：

```powershell
cd D:\Project\Bell-Robot\embedded\firmware
C:\Users\23171\.platformio\penv\Scripts\pio.exe run
```

## ESP-IDF Web 接口

开发板启动后创建 Wi-Fi AP：

- SSID：`Bill-Camera`
- 密码：`12345678`
- 地址：`http://192.168.4.1/`

接口：

- `/`：摄像头预览和采样入口。
- `/capture`：返回当前 JPEG 画面。
- `/status`：返回状态 JSON，包含 `model_prob`、`model_ready`、`inference_ms`、`fallback_reason`。
- `/reset`：重置计时并重新校准 ROI fallback。
- `/label?class=absent`：下载一帧离开/无人 `8x8` 归一化 PGM 样本。
- `/label?class=seated`：下载一帧桌前坐姿 `8x8` 归一化 PGM 样本。
- `/label?class=empty|occupied`：兼容旧标签，分别映射到 `absent|seated`。

## 模型训练流程

1. 按真实安装角度固定摄像头。
2. 连接 `Bill-Camera`。
3. 人离开、画面无人或只有空桌椅时访问 `/label?class=absent` 采集样本。
4. 人在办公桌前正面坐姿、前斜侧面坐姿、不同衣服颜色、不同光照时访问 `/label?class=seated` 采集样本。
5. 将 `.pgm` 放入 `firmware-idf/model/dataset/absent/` 和 `firmware-idf/model/dataset/seated/`。
6. 运行：

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
python model\train_seat_model.py --dataset model\dataset --out main\seat_model_data.h
idf.py build
```

第一版模型输入是画面中部偏上的 8x8 灰度特征，输出 `occupiedProbability`。这里的 occupied 表示“检测到桌前坐姿的人”。默认 ROI 会向上偏置，优先覆盖头部、肩部和上半身。默认阈值为 `0.65`，并沿用连续多帧确认，避免单帧抖动。

## 当前实现说明

- `firmware-idf/main/main.cpp`：ESP-IDF 主程序，包含摄像头、AP、HTTP 接口、状态机、按钮、蜂鸣器和显示刷新。
- `firmware-idf/main/seat_model.*`：本地 int8 模型推理接口。
- `firmware-idf/main/seat_model_data.h`：训练脚本生成的模型权重；当前默认未训练，固件会回退到 ROI 差分。
- `firmware-idf/main/ssd1306_spi.*`：不依赖 Arduino/U8g2 的最小 SPI SSD1306 文本显示驱动。
- `firmware-idf/model/train_seat_model.py`：从采集的 PGM 样本训练并生成模型头文件。
- `firmware-idf/tools/*.ps1`：本机固定路径的 ESP-IDF 环境和构建脚本。
- `docs/pretrain-data.md`：公开预训练数据源筛选和数据策略。
- `firmware/`：旧 PlatformIO 工程，当前仅保留用于回退验证；待 `firmware-idf` 完成实机闭环后再统一删除。

## 2026-05-01 更新

- 新增 `firmware-idf/` ESP-IDF 主线工程，迁移摄像头、SPI OLED、蜂鸣器、按键、AP、`/status`、`/reset` 和 45 分钟久坐状态机。
- 新增 `/label?class=absent|seated` 数据采集接口，返回 PGM 灰度样本，并兼容旧 `empty|occupied` 标签。
- 新增本地 int8 桌前坐姿模型接口和训练脚本；模型未训练时自动回退到 ROI 灰度差分。
- `/status` 新增 `model_prob`、`model_ready`、`inference_ms`、`fallback_reason`。
- 更新 `.gitignore`，忽略 `firmware-idf/build*` 等 ESP-IDF 构建输出。
- OLED 倒计时改为 3 倍大字居中显示，顶部状态/诊断信息压缩为 3 行小字，优先保证剩余时间醒目。
- 本机已完成 ESP-IDF 5.4.4 工具链安装，并补齐 `build-idf.ps1` 的增量构建目录支持。
- 已完成“软删除前收口”：`firmware-idf/` 明确为唯一主线，`firmware/` 明确标记为 legacy fallback，待 `firmware-idf` 实机验证通过后再统一删除旧 Arduino 目录和相关说明。

## 已验证

- 2026-04-29 至 2026-04-30：旧 PlatformIO 固件多轮编译和烧录通过，实物板识别为 ESP32-S3，PSRAM 8MB 正常。
- 2026-05-01：`firmware-idf` 已完成 `idf.py build`，生成 `build-run/bell_robot_seat_model.bin`。
- 2026-05-01：新 OLED 大字倒计时版本已成功烧录到 `COM13`，芯片识别为 `ESP32-S3`，MAC 为 `e0:72:a1:f6:39:10`。
- 2026-05-01：手机已可连接 `Bill-Camera` 热点，并正常打开摄像头预览网页，实时画面可见。
- 2026-05-01：SPI OLED 显示正常，按键功能正常。

## 待核对

- 蜂鸣器本轮未实测，当前按硬件连接和固件逻辑暂视为默认正常，但不记为已验证。
- 采集显示器上沿或桌面前方真实安装角度样本并生成 `seat_model_data.h`。
- 用白天/夜间、深浅衣服、正面坐姿、前斜侧面坐姿、半离开、短暂返回场景验证准确率。
