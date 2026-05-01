# Bell Robot Cloud Relay

轻量云中转服务。设备主动轮询服务器，手机登录服务器网页远程查看状态、修改计时、重置和按需拉取摄像头快照。

## 启动

```powershell
cd D:\Project\Bell-Robot\cloud-relay
docker compose up -d --build
```

默认端口是 `8080`，默认网页登录账号是 `admin / 69696966`。部署到公网时建议放在 Nginx/Caddy 后面启用 HTTPS。

远程网页会每秒刷新设备状态；倒计时和离场容忍输入框在编辑期间不会被自动刷新覆盖，保存后再恢复同步。

生产环境至少修改：

- `ADMIN_PASS`
- `DEVICE_TOKEN`
- `DEVICE_ID`

## 设备端配置

首次配置时连接设备热点 `Bell-Robot`，在本地网页填写：

- 2.4G Wi-Fi 名称和密码
- 服务器地址，例如 `https://your-domain.example`
- 设备 ID，例如 `bell-robot-1`
- 设备 Token，必须和服务端 `DEVICE_TOKEN` 一致
