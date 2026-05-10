# Bell Robot Embedded

ESP32-S3 久坐提醒器固件。设备本地完成坐姿识别、倒计时、OLED 显示和蜂鸣器提醒；联网后通过 `cloud-relay/` 远程查看状态、修改计时、重置和按需获取摄像头快照。

## 当前主线

- 固件：`firmware-idf/`
- 硬件：Freenove ESP32-S3 N16R8 CAM + DC5640 AF 120度 + SPI SSD1306 OLED + 蜂鸣器 + 单按键
- 网络：STA 联网优先；未配置、联网失败或云端连续不可达时开启 `Bell-Robot` 热点
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

## 构建与烧录

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

## 云中转

```powershell
cd D:\Project\Bell-Robot\cloud-relay
docker compose up -d --build
```

主要接口：

- `GET /api/devices`：设备列表
- `GET /api/status?device_id=...`：指定设备状态
- `POST /api/settings?device_id=...`：下发计时设置命令
- `POST /api/reset?device_id=...`：下发重置命令
- `GET /api/capture.jpg?device_id=...`：按需请求设备上传一张 JPEG

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
- 2026-05-10：STA 云远程固件已烧录到 `COM13`，设备连接 `Innoxsz-2.4G` 成功。
- 2026-05-10：修复云请求超时容错，设备 `bell-robot-f63910` 持续在线，远程快照返回 JPEG。

## 后续关注

- 如果现场网络再次长时间无法访问云端，设备应在约 90 秒后回到 `Bell-Robot` 热点。
- 如果 AF 默认行为不稳定，再补充模组初始化或固定焦点策略。
- 若 120 度视角导致人体占比变化明显，再复核 ROI 和模型样本。
