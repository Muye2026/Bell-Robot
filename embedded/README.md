# 久坐提醒外置硬件

独立运行的 ESP32-S3 久坐提醒器。摄像头只在本地判断桌前坐姿/离开，不做人脸识别，不上传图像。连续坐满目标时间后通过 OLED 和蜂鸣器提醒站立；提醒响铃后检测到人离开会自动静音并开始下一轮。

## 当前主线

- 固件：`firmware-idf/` 是唯一主线。
- 硬件：Freenove ESP32-S3 N16R8 CAM + OV5640CSP + SPI SSD1306 OLED + 无源蜂鸣器 + 单按键。
- 网络：固定 AP 直连，SSID `Bell-Robot`，密码 `12345678`，地址 `http://192.168.4.1/`。
- 计时：默认坐满 45 分钟提醒；暂离容忍时间内倒计时暂停，回来后继续；默认离开 1 分钟重置本轮。倒计时和离场容忍时间可在网页调整并写入 NVS。
- 模型：本地 int8 桌前坐姿二分类；模型不可用时回退到 ROI 灰度差分。

## 目录

```text
Bell-Robot/
  model/                         # 训练数据和模型生成脚本
  embedded/
    README.md
    docs/
    firmware-idf/                # ESP-IDF 固件
      main/
      tools/
```

## 使用

1. 手机连接 `Bell-Robot`。
2. 打开 `http://192.168.4.1/`。
3. 在网页查看摄像头画面，调整倒计时和离场容忍时间。
4. `/status` 可查看模型概率、推理耗时、当前倒计时配置等诊断字段。

可用接口：

- `/`：摄像头预览、设置和样本采集入口。
- `/capture`：当前 JPEG 画面。
- `/settings`：读取/保存 `sit_minutes=1..180`、`away_minutes=1..5`。
- `/status`：状态 JSON。
- `/reset`：重置当前计时并重新校准 ROI fallback。
- `/label?class=absent|seated`：下载一帧 `8x8` 归一化 PGM 样本。

## 构建与烧录

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

## 模型训练

采集样本后放入：

- `model/dataset/absent/`
- `model/dataset/seated/`

从仓库根目录训练并生成固件头文件：

```powershell
cd D:\Project\Bell-Robot
python model\train_seat_model.py --dataset model\dataset --out embedded\firmware-idf\main\seat_model_data.h --balance-classes
```

当前模型输入是画面中部偏上的 `8x8` 灰度特征，输出 `occupiedProbability`。训练 ROI 已与固件 `buildModelFeatures()` 对齐。

## 已验证

- 2026-05-01：AP 直连固件已构建并烧录到 `COM13`，芯片为 ESP32-S3，MAC `e0:72:a1:f6:39:10`。
- 2026-05-01：网页可调整倒计时和离场容忍时间，设置写入 NVS，重启保留。
- 2026-05-01：蜂鸣器功能正常；响铃后检测到离开会自动静音并结束本轮计时。
- 2026-05-01：已删除 STA/mDNS/网络配网页面和 `/network*` 接口，固件固定使用 AP 直连。
- 2026-05-01：设备热点 SSID 改为 `Bell-Robot`；已构建并烧录到 `COM13`，写入和 Hash 校验通过；固件大小 `0xe0cf0`，APP 分区剩余约 12%。
- 2026-05-01：已清理可再生成产物：`firmware-idf/build*`、`managed_components`、`sdkconfig`、`dependencies.lock` 和 `model/__pycache__`。
- 2026-05-01：OLED 倒计时主界面精简为状态、倒计时和 `PROB` 识别概率三项，移除帧数、raw、RST 和按键提示等诊断文字。
- 2026-05-01：暂离容忍时间内倒计时暂停，回来后继续；入座确认从 6 帧改为 5 帧，采样间隔保持 500ms。
- 2026-05-01：暂离暂停倒计时版本已构建通过；固件大小 `0xe0b80`，APP 分区剩余约 12%。
- 2026-05-01：OLED 正常倒计时恢复为静态显示；暂离保留 `PAUSED`/`PAUSED .` 呼吸，提醒状态保留 `ALERT` 闪烁。
- 2026-05-01：OLED 静态倒计时版本已构建通过；固件大小 `0xe0c00`，APP 分区剩余约 12%。

## 待核对

- 用最终安装角度继续采集真实 `absent/seated` 样本并重训。
- 用白天/夜间、深浅衣服、正面/侧面坐姿、半离开和短暂返回场景验证准确率。
