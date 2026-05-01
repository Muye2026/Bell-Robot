# Bell Robot Embedded

ESP32-S3 久坐提醒器固件。设备本地完成坐姿/离开识别、计时、OLED 显示和蜂鸣器提醒；联网后可通过云中转远程查看状态、修改计时、重置和按需获取摄像头快照。

## 当前主线

- 固件：`firmware-idf/`
- 硬件：Freenove ESP32-S3 N16R8 CAM + OV5640CSP + SPI SSD1306 OLED + 蜂鸣器 + 单按键
- 网络：STA 联网优先；未配置或联网失败约 15 秒后开启 `Bell-Robot` 热点，地址 `http://192.168.4.1/`
- 云端：仓库内 `cloud-relay/` 提供 Node.js + Docker 中转服务，网页登录默认 `admin / 69696966`
- 计时：默认坐满 45 分钟提醒；暂离容忍默认 1 分钟；暂离期间倒计时暂停，回来继续，超时重置
- 模型：本地 int8 桌前坐姿二分类；模型不可用时回退到 ROI 灰度差分

## 使用

首次配置：

1. 手机连接设备热点 `Bell-Robot`。
2. 打开 `http://192.168.4.1/`。
3. 填写 2.4G Wi-Fi、云服务器地址、设备 ID 和设备 Token。
4. 保存后设备重启并尝试连接路由器 Wi-Fi；失败会回到 `Bell-Robot` 热点。

远程使用：

1. 在服务器部署 `cloud-relay/`，并配置 `ADMIN_PASS`、`DEVICE_ID`、`DEVICE_TOKEN`。
2. 手机访问服务器网页，登录后查看状态、设置倒计时、重置或刷新摄像头快照。
3. 摄像头只在网页请求快照时上传 JPEG；无人查看时不持续上传图像。

本地接口：

- `/`：本地预览、计时设置、云配置和样本采集入口
- `/capture`：当前 JPEG 画面
- `/settings`：读取/保存 `sit_minutes=1..180`、`away_minutes=1..5`
- `/cloud`：读取/保存 Wi-Fi 和云端连接配置
- `/cloud/forget`：清除云配置并重启
- `/status`：状态 JSON，包含模型概率、计时设置和联网诊断
- `/reset`：重置当前计时并重新校准 ROI fallback
- `/label?class=absent|seated`：下载一帧 `8x8` PGM 样本

## 构建与烧录

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

固件启用 HTTPS 证书包和云轮询后超过默认 1MB app 分区，当前使用 `partitions.csv` 将 factory app 分区设为 3MB。

## 云中转

```powershell
cd D:\Project\Bell-Robot\cloud-relay
docker compose up -d --build
```

服务端接口：

- `GET /`：手机网页
- `GET /api/status`：远程状态
- `POST /api/settings`：下发计时设置命令
- `POST /api/reset`：下发重置命令
- `GET /api/capture.jpg`：按需请求设备上传一张 JPEG
- `POST /device/poll`：设备轮询并上报状态
- `POST /device/capture`：设备上传快照
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

- 2026-05-01：AP 直连固件已构建并烧录到 `COM13`，芯片为 ESP32-S3，MAC `e0:72:a1:f6:39:10`。
- 2026-05-01：网页可调整倒计时和离场容忍时间，设置写入 NVS，重启保留。
- 2026-05-01：蜂鸣器功能正常；响铃后检测到离开会自动静音并结束本轮计时。
- 2026-05-01：OLED 主界面精简为状态、倒计时和 `PROB`，正常倒计时静态显示，暂离显示 `PAUSED`/`PAUSED .`，提醒状态闪烁 `ALERT`。
- 2026-05-01：暂离容忍时间内倒计时暂停，回来后继续；入座确认从 6 帧改为 5 帧，采样间隔 500ms。
- 2026-05-01：新增云中转方案、STA 优先/AP 兜底、AP 云配置页、设备 HTTPS 轮询和按需 JPEG 快照；已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0x10e890`，3MB app 分区剩余约 65%。
- 2026-05-01：修复本地 AP 云配置保存时 `application/x-www-form-urlencoded` 未解码导致 `Server URL` 校验失败的问题；已构建并烧录到 `COM13`，固件大小 `0x10e960`。
- 2026-05-01：AP 云配置保存改为固件自有表单解析，并在网页显示具体失败原因，便于现场排查。
- 2026-05-01：表单解析增强版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0x10ebc0`。
- 2026-05-01：修复本地 HTTP 接口数量不足导致 `POST /cloud` 未注册、保存云配置返回 method invalid 的问题。
- 2026-05-01：`POST /cloud` 注册修复版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0x10eed0`。

## 待核对

- 在真实服务器和 HTTPS 域名下验证设备轮询、远程设置、重置和按需快照。
- 用最终安装角度继续采集真实 `absent/seated` 样本并重训模型。
- 用白天、夜间、深浅衣服、正面/侧面坐姿、半离开和短暂返回场景验证准确率。
