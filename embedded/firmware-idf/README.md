# Bell Robot ESP-IDF Firmware

ESP32-S3 N16R8 CAM 久坐提醒固件。设备默认优先连接路由器 Wi-Fi 并通过云中转远程访问；未配置或联网失败时开启 `Bell-Robot` 热点，手机可访问 `http://192.168.4.1/` 完成首次配置。

## 构建

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
```

## 烧录

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

## 本地接口

- `/`：摄像头预览、计时设置、云配置、样本采集入口
- `/capture`：当前 JPEG 画面
- `/status`：状态 JSON，包含模型概率、计时和联网诊断
- `/settings`：读取/保存倒计时和离场容忍分钟数
- `/cloud`：读取/保存 2.4G Wi-Fi、服务器地址、设备 ID、设备 Token
- `/cloud/forget`：清除云配置并重启
- `/reset`：重置当前计时并重新校准
- `/label?class=absent|seated`：下载一帧 `8x8` PGM 样本

## 分区

启用 HTTPS 证书包和云轮询后固件超过默认 1MB app 分区。当前使用 `partitions.csv`：

- `nvs`：24KB
- `phy_init`：4KB
- `factory`：3MB

## 当前验证

- 2026-05-01：云中转版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0x10e890`，3MB app 分区剩余约 65%。
- 2026-05-01：修复 AP 配网页保存云配置时 URL 编码未解码导致保存失败的问题；已构建并烧录到 `COM13`，固件大小 `0x10e960`。
- 2026-05-01：AP 云配置保存改为固件自有表单解析，并在网页显示具体失败原因，便于现场排查。
- 2026-05-01：表单解析增强版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0x10ebc0`。
- 2026-05-01：修复本地 HTTP 接口数量不足导致 `POST /cloud` 未注册、保存云配置返回 method invalid 的问题。
- 2026-05-01：`POST /cloud` 注册修复版本已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0x10eed0`。
- Docker 未在当前 Windows 环境安装，`cloud-relay` 的 Compose 配置需在服务器或装有 Docker 的机器上验证。
