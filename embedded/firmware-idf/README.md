# Bell Robot ESP-IDF Firmware

ESP32-S3 N16R8 CAM 久坐提醒固件。设备优先连接路由器 Wi-Fi 并轮询云中转；未配置或联网失败时开启 `Bell-Robot` 热点，手机访问 `http://192.168.4.1/` 完成配置。

## 构建

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
```

## 烧录

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

最近一次烧录：

- 日期：2026-05-01
- 串口：`COM13`
- 芯片：ESP32-S3
- MAC：`e0:72:a1:f6:39:10`
- 生成设备 ID：`bell-robot-f63910`
- app 大小：约 `0x10ef10`
- 写入和 Hash 校验：通过

## 配网

AP 配网页只需要用户填写：

- 2.4G Wi-Fi SSID
- Wi-Fi password
- Server URL

`device_id` 根据芯片 MAC 自动生成，设备 token 随机生成并保存在 NVS，用户不填写。旧默认 ID `bell-robot-1` 会在启动时自动迁移为 MAC ID。

## 本地接口

- `/`：摄像头预览、计时设置、云配置、样本采集入口
- `/capture`：当前 JPEG 画面
- `/status`：状态 JSON，包含模型概率、计时、联网诊断和 `device_id`
- `/settings`：读取/保存倒计时和离场容忍分钟数
- `/cloud`：读取/保存 2.4G Wi-Fi 和服务器地址
- `/cloud/forget`：清除 Wi-Fi/服务器配置并重启，保留设备身份
- `/reset`：重置当前计时并重新校准
- `/label?class=absent|seated`：下载一帧 `8x8` PGM 样本

## 分区

启用 HTTPS 证书包和云轮询后固件超过默认 1MB app 分区。当前使用 `partitions.csv`：

- `nvs`：24KB
- `phy_init`：4KB
- `factory`：3MB

## 当前验证

- 2026-05-01：云中转版本已多次构建并烧录到 `COM13`，写入和 Hash 校验通过。
- 2026-05-01：修复 AP 配网页保存云配置的 URL 解码和 `POST /cloud` 注册问题。
- 2026-05-01：固件自动生成 `device_id` 和隐藏 token；配网页不再要求用户填写设备 ID 或 token。
- 2026-05-01：旧默认 ID 自动迁移已验证，远程网页显示 `bell-robot-f63910` 在线。
