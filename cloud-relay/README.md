# Bell Robot Cloud Relay

Bell Robot 的 Node.js 云中转服务。设备主动轮询服务器，手机网页通过服务器远程查看状态、修改计时、重置设备，并按需拉取一张摄像头 JPEG 快照。

## 当前状态

- 支持多设备管理，网页顶部可选择设备。
- 网页端不再使用 `admin` 登录。
- 设备第一次上线时自动登记到 `data/devices.json`。
- 同一 `device_id` 后续必须继续使用同一个隐藏 token。
- 摄像头图片只在请求快照时临时转发，不落盘保存。

## 启动

```powershell
cd D:\Project\Bell-Robot\cloud-relay
docker compose up -d --build
```

默认端口是 `8080`。

服务器部署路径：

```bash
cd /opt/bell-robot/cloud-relay
docker compose up -d --build
docker compose ps
```

验证：

```bash
curl -I http://127.0.0.1:8080/
curl http://127.0.0.1:8080/api/devices
```

正常情况下首页返回 `200 OK`，不是 `401 Unauthorized`。

## 设备配置

首次配置时连接设备热点 `Bell-Robot`，打开：

```text
http://192.168.4.1/
```

只需要填写：

- 2.4G Wi-Fi SSID
- Wi-Fi password
- Server URL，例如 `http://43.134.30.245:8080`

`device_id` 由固件根据芯片 MAC 自动生成，例如 `bell-robot-f63910`。设备 token 由固件随机生成并保存在 NVS，用户不需要填写。

## 数据文件

设备登记信息保存在：

```text
cloud-relay/data/devices.json
```

该目录已加入 `.gitignore`，不要提交。

## 主要接口

- `GET /`：手机网页
- `GET /api/devices`：设备列表
- `GET /api/status?device_id=...`：指定设备状态
- `POST /api/settings?device_id=...`：下发计时设置
- `POST /api/reset?device_id=...`：下发重置
- `GET /api/capture.jpg?device_id=...`：按需请求一张 JPEG
- `POST /device/poll`：设备轮询并上报状态
- `POST /device/capture?device_id=...`：设备上传快照
- `POST /device/result`：设备回传命令执行结果

## 安全说明

当前版本按现场需求取消网页登录密码。谁能打开服务器网页，谁就能查看和操作已登记设备。公网部署时建议后续加 HTTPS 和访问控制，当前服务器地址不要随意公开。

## 已验证

- 2026-05-01：服务器 `43.134.30.245:8080` 已部署新版，多设备下拉框正常。
- 2026-05-01：设备 `bell-robot-f63910` 已在线，状态上报正常。
