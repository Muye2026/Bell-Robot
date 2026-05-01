# 久坐提醒外置硬件

本项目开发一个独立运行的久坐提醒硬件：通过 ESP32-S3 N16R8 CAM + OV5640CSP 判断设备前方是否有人保持桌前坐姿，连续坐满 45 分钟后通过 OLED 和无源蜂鸣器提醒站立。设备预计放在显示器上或桌面上，摄像头主要看到人的正面或前斜侧面坐姿，而不是完整椅子画面。图像只用于本地坐姿/离开判断，不做人脸识别，不上传图像。

## 当前主线

- 主控：Freenove ESP32-S3 N16R8 CAM，16MB Flash + 8MB PSRAM。
- 摄像头：OV5640CSP，丝印 `VVS OV5640CSP 8225N VC`。
- 显示：SPI SSD1306 128x64 OLED，丝印 `GND / VCC / SCL / SDA / RES / DCC / CS`。
- 提醒：两引脚无源蜂鸣器，PWM 方波驱动。
- 交互：单按键用于确认提醒、重置计时和重新校准。
- 识别：ESP-IDF 主线固件使用本地 int8 桌前坐姿模型；无训练模型时自动回退到 ROI 灰度差分。
- 计时：坐下后默认 45 分钟倒计时；离开 10 秒后提示即将重置；离开默认 1 分钟后本轮计时作废；倒计时和离场容忍时间可在网页端调节并掉电保留；提醒响铃后检测到离开会自动静音并结束本轮计时。
- 固件入口：`firmware-idf/` 是当前唯一主线。

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

## ESP-IDF Web 接口

开发板启动后创建 Wi-Fi AP：

- SSID：`Bill-Camera`
- 密码：`12345678`
- 地址：`http://192.168.4.1/`

接口：

- `/`：摄像头预览和采样入口。
- `/capture`：返回当前 JPEG 画面。
- `/status`：返回状态 JSON，包含 `model_prob`、`model_ready`、`inference_ms`、`fallback_reason`、`sit_minutes`、`away_minutes`。
- `/settings`：`GET` 返回当前倒计时/离场容忍分钟数；`POST` 保存 `sit_minutes=1..180` 和 `away_minutes=1..5` 到 NVS。
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
python model\train_seat_model.py --dataset model\dataset --out main\seat_model_data.h --balance-classes
idf.py build
```

第一版模型输入是画面中部偏上的 8x8 灰度特征，输出 `occupiedProbability`。这里的 occupied 表示“检测到桌前坐姿的人”。默认 ROI 会向上偏置，优先覆盖头部、肩部和上半身。默认阈值为 `0.65`，并沿用连续多帧确认，避免单帧抖动。

## 当前实现说明

- `firmware-idf/main/main.cpp`：ESP-IDF 主程序，包含摄像头、AP、HTTP 接口、状态机、按钮、蜂鸣器和显示刷新。
- `firmware-idf/main/seat_model.*`：本地 int8 模型推理接口。
- `firmware-idf/main/seat_model_data.h`：训练脚本生成的模型权重；当前已切换到第一版非占位模型，后续继续用真实样本迭代。
- `firmware-idf/main/ssd1306_spi.*`：最小 SPI SSD1306 文本显示驱动。
- `firmware-idf/model/train_seat_model.py`：从采集的 PGM 样本训练并生成模型头文件。
- `firmware-idf/model/prepare_seed_dataset.py`：从手机预览截图提取真实预览画面并生成种子训练集。
- `firmware-idf/tools/*.ps1`：本机固定路径的 ESP-IDF 环境和构建脚本。
- `docs/pretrain-data.md`：公开预训练数据源筛选和数据策略。

## 2026-05-01 更新

- 新增 `firmware-idf/` ESP-IDF 主线工程，迁移摄像头、SPI OLED、蜂鸣器、按键、AP、`/status`、`/reset` 和 45 分钟久坐状态机。
- 新增 `/label?class=absent|seated` 数据采集接口，返回 PGM 灰度样本，并兼容旧 `empty|occupied` 标签。
- 新增本地 int8 桌前坐姿模型接口和训练脚本；模型未训练时自动回退到 ROI 灰度差分。
- 新增 `prepare_seed_dataset.py`，支持从手机预览截图生成第一版种子训练集。
- 已用 3 张真实桌前坐姿截图 + `MPIIGaze` 官方上半身样例 + `Edinburgh office monitoring video dataset` 空办公室样例训出第一版非占位 `seat_model_data.h`。
- `/status` 新增 `model_prob`、`model_ready`、`inference_ms`、`fallback_reason`。
- 更新 `.gitignore`，忽略 `firmware-idf/build*` 等 ESP-IDF 构建输出。
- OLED 倒计时改为 3 倍大字居中显示，顶部状态/诊断信息压缩为 3 行小字，优先保证剩余时间醒目。
- OLED 现已直接显示 `STATE:SEATED / AWAY / EMPTY / ALERT`，便于现场判断模型当前把画面识别成“有人坐着、暂时离开、无人、提醒中”的哪一种状态。
- 本机已完成 ESP-IDF 5.4.4 工具链安装，并补齐 `build-idf.ps1` 的增量构建目录支持。
- 已删除旧固件目录和相关回退说明，`firmware-idf/` 保持唯一主线。
- 模型 `1.2` 补入 4 张真实工位坐下办公截图作为强 seated 样本，训练脚本新增 `--balance-classes`，在补充正样本后仍按类别均衡训练，避免空工位误判回升。
- 修正训练脚本 ROI 与固件 ROI 不一致的问题；此前训练使用偏中下区域，固件实际使用偏上大区域，可能导致离线样本通过但上板实时 `model_prob` 偏低，坐下后无法进入倒计时。
- 为继续排查现场不倒计时问题，模型触发阈值从 `0.65` 临时下调到 `0.50`，仍保留连续 6 帧确认；OLED、串口和 `/status` 增加 `raw_present`、`on_frames`、`off_frames` 和 `model_threshold` 诊断字段。
- SSD1306 显示改为软件逆时针旋转 90 度，逻辑画布切换为 `64x128` 竖屏布局，倒计时使用 2 倍字高以适配竖屏宽度。
- 中途离场容忍时间改为 1 分钟：离开 10 秒进入重置提示，离开 1 分钟仍未返回则重置本轮计时。
- OLED 竖屏倒计时重新排版：分钟和秒拆成上下两块 4 倍大字显示，倒计时区域只保留数字，顶部保留状态、识别概率和连续帧诊断。
- 网页首页新增倒计时和离场容忍时间设置；配置写入 NVS，重启保留，修改时不清空当前已坐时长。
- 提醒响铃后继续保持当前滴滴响节奏；现有去抖检测确认人离开后自动关闭蜂鸣器并结束本轮计时，回来后重新开始下一轮。

## 已验证

- 2026-05-01：`firmware-idf` 已完成 `idf.py build`，生成 `build-run/bell_robot_seat_model.bin`。
- 2026-05-01：第一版非占位 `seat_model_data.h` 已生成，`build-run` 再次编译通过。
- 2026-05-01：新 OLED 大字倒计时版本已成功烧录到 `COM13`，芯片识别为 `ESP32-S3`，MAC 为 `e0:72:a1:f6:39:10`。
- 2026-05-01：手机已可连接 `Bill-Camera` 热点，并正常打开摄像头预览网页，实时画面可见。
- 2026-05-01：SPI OLED 显示正常，按键功能正常。
- 2026-05-01：模型 `1.1` 固件已成功烧录到 `COM13`。
- 2026-05-01：用真实办公场景无人/有人截图补充训练集，生成模型 `1.2`，ESP-IDF 构建通过并已烧录到 `COM13`。
- 2026-05-01：追加 4 张真实工位坐下办公截图后重新训练模型 `1.2`，训练集规模 `absent=131 / seated=275`，训练集内评估 `tp=225 tn=127 fp=4 fn=50`；新增 4 张截图增强样本 `128/128` 均超过 `0.65` 阈值，最低概率约 `0.852`。
- 2026-05-01：新模型 `1.2` 已重新构建并烧录到 `COM13`，写入和 Hash 校验通过，芯片识别为 `ESP32-S3`，MAC 为 `e0:72:a1:f6:39:10`。
- 2026-05-01：训练 ROI 对齐固件 ROI 后重新训练并烧录模型 `1.2`；训练集内评估 `tp=216 tn=129 fp=2 fn=59`，新增 4 张真实坐下截图增强样本 `128/128` 均超过 `0.65` 阈值，最低概率约 `0.837`。
- 2026-05-01：电脑未连接设备 AP，`http://192.168.4.1/status` 无法从本机访问；串口 `COM13` 可烧录，但短时间运行日志抓取未读到 `tick` 输出。
- 2026-05-01：阈值下调和诊断字段版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe0160`，APP 分区剩余约 12%。
- 2026-05-01：SSD1306 逆时针 90 度竖屏显示和 15 秒离场容忍版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe0160`，APP 分区剩余约 12%。
- 2026-05-01：OLED 大字倒计时重排版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe01e0`，APP 分区剩余约 12%。
- 2026-05-01：1 分钟离场容忍版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe01e0`，APP 分区剩余约 12%。
- 2026-05-01：网页可调倒计时和离场容忍版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe0ce0`，APP 分区剩余约 12%。
- 2026-05-01：蜂鸣器功能经现场验证正常。
- 2026-05-01：已撤回彩色网页预览改动，摄像头采集和网页预览恢复为灰度路径；已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe0ce0`，APP 分区剩余约 12%。
- 2026-05-01：提醒响铃后检测到离开即自动静音并结束本轮计时；已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe0cf0`，APP 分区剩余约 12%。

## 待核对

- 采集显示器上沿或桌面前方真实安装角度样本并生成 `seat_model_data.h`。
- 用白天/夜间、深浅衣服、正面坐姿、前斜侧面坐姿、半离开、短暂返回场景验证准确率。
