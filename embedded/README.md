# Bell Robot Embedded

ESP32-S3 久坐提醒器固件。设备本地完成坐姿识别、倒计时、OLED 显示和蜂鸣器提醒。当前开发默认使用 `Bell-Robot` AP 直连；STA 云中转代码保留，需要远程服务器时可通过编译期开关恢复。

## 当前主线

- 固件：`firmware-idf/`
- 硬件：Freenove ESP32-S3 N16R8 CAM + DC5640 AF 120度 + SPI SSD1306 OLED + 蜂鸣器 + 单按键
- 网络：当前默认 AP-only，热点名 `Bell-Robot`，密码 `12345678`
- 云端：`cloud-relay/` 仍保留；将 `ENABLE_CLOUD_REMOTE` 改为 `true` 后可恢复 STA 云远程
- 设备身份：远程模式下 `device_id` 根据芯片 MAC 自动生成；设备 token 保存在 NVS，AP-only 不会清除这些配置
- 计时：默认坐满 45 分钟提醒，暂离容忍默认 1 分钟；暂离期间倒计时暂停，回来继续，超时重置

## 本地开发使用

1. 手机连接设备热点 `Bell-Robot`。
2. 打开 `http://192.168.4.1/`。
3. 使用本地网页查看摄像头、调整倒计时/离场容忍、重置设备或采集样本。

当前 AP-only 固件不会自动连接路由器 Wi-Fi，也不会轮询云服务器。

## 切回云端远程

1. 打开 `embedded/firmware-idf/main/app_config.h`。
2. 将 `ENABLE_CLOUD_REMOTE` 改为 `true`。
3. 重新构建并烧录。

设备会继续使用 NVS 里已有的 Wi-Fi、服务器地址、`device_id` 和 token；通常不需要重新配网。

## 构建与烧录

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

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
