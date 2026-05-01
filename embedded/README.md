# Bell Robot Embedded

ESP32-S3 久坐提醒器固件。设备本地完成坐姿识别、倒计时、OLED 显示和蜂鸣器提醒；联网后通过 `cloud-relay/` 远程查看状态、修改计时、重置和按需获取摄像头快照。

## 当前主线

- 固件：`firmware-idf/`
- 硬件：Freenove ESP32-S3 N16R8 CAM + OV5640CSP + SPI SSD1306 OLED + 蜂鸣器 + 单按键
- 网络：STA 联网优先；未配置或联网失败约 15 秒后开启 `Bell-Robot` 热点
- 云端：`cloud-relay/` 支持多设备，网页端不需要 `admin` 登录
- 设备身份：`device_id` 根据芯片 MAC 自动生成；设备 token 随机生成并保存在 NVS，用户不填写
- 计时：默认坐满 45 分钟提醒，暂离容忍默认 1 分钟；暂离期间倒计时暂停，回来继续，超时重置

## 首次配置

1. 手机连接设备热点 `Bell-Robot`。
2. 打开 `http://192.168.4.1/`。
3. 填写 2.4G Wi-Fi 名称、Wi-Fi 密码和服务器地址。
4. 保存后设备重启并尝试连接路由器 Wi-Fi；失败会回到 `Bell-Robot` 热点。

服务器地址可以所有设备共用，例如：

```text
http://43.134.30.245:8080
```

设备上线后会自动登记到服务器设备列表。

## 本地接口

- `/`：本地预览、计时设置、云配置和样本采集入口
- `/capture`：当前 JPEG 画面
- `/settings`：读取/保存 `sit_minutes=1..180`、`away_minutes=1..5`
- `/cloud`：读取/保存 Wi-Fi 和服务器地址
- `/cloud/forget`：清除 Wi-Fi/服务器配置并重启，保留设备身份
- `/status`：状态 JSON，包含模型概率、计时设置、联网诊断和 `device_id`
- `/reset`：重置当前计时并重新校准 ROI fallback
- `/label?class=absent|seated`：下载一帧 `8x8` PGM 样本

## 构建与烧录

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

当前使用 `partitions.csv`，factory app 分区为 3MB。

## 云中转

```powershell
cd D:\Project\Bell-Robot\cloud-relay
docker compose up -d --build
```

主要接口：

- `GET /`：手机网页
- `GET /api/devices`：设备列表
- `GET /api/status?device_id=...`：指定设备状态
- `POST /api/settings?device_id=...`：下发计时设置命令
- `POST /api/reset?device_id=...`：下发重置命令
- `GET /api/capture.jpg?device_id=...`：按需请求设备上传一张 JPEG
- `POST /device/poll`：设备轮询并上报状态
- `POST /device/capture?device_id=...`：设备上传快照
- `POST /device/result`：设备回传命令执行结果

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
- 2026-05-01：新增云中转、STA 优先/AP 兜底、按需 JPEG 快照。
- 2026-05-01：修复 AP 配网页保存云配置的 URL 解码和 `POST /cloud` 注册问题。
- 2026-05-01：云端计时输入框编辑时不再被 1 秒状态刷新覆盖。
- 2026-05-01：云中转改为多设备管理，取消网页登录密码；固件自动生成 `device_id` 和隐藏 token。
- 2026-05-01：旧默认 ID `bell-robot-1` 已迁移为按 MAC 生成的 `bell-robot-f63910`，并已烧录到 `COM13`。
- 2026-05-01：服务器 `43.134.30.245:8080` 已部署新版，网页设备列表显示 `bell-robot-f63910` 在线。
