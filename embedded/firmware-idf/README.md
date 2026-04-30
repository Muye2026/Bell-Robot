# ESP-IDF 主线固件

该目录是 ESP32-S3 N16R8 CAM 的当前唯一主线固件，用于本地桌前坐姿/离开二分类模型推理。设备预计放在显示器上或桌面上，摄像头主要看到人的正面或前斜侧面坐姿，而不是完整椅子画面。旧 `embedded/firmware` Arduino/PlatformIO 工程当前只作为 legacy fallback 保留，在 `firmware-idf` 完成整机实机闭环验证前暂不删除，也不再继续承载新功能开发。

## 构建

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
idf.py set-target esp32s3
idf.py build
```

烧录和串口监视：

```powershell
idf.py -p COM13 flash monitor
```

本机已完成 ESP-IDF 5.4.4 工具链安装；常用入口是：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
```

## Web 接口

开发板启动后创建 AP：

- SSID: `Bill-Camera`
- Password: `12345678`
- URL: `http://192.168.4.1/`

接口：

- `/`：预览页面。
- `/capture`：当前摄像头 JPEG 图。
- `/status`：JSON 状态，包含 `model_prob`、`model_ready`、`inference_ms`、`fallback_reason`。
- `/reset`：重置计时并重新校准 ROI fallback。
- `/label?class=seated`：采集一帧桌前坐姿样本。
- `/label?class=absent`：采集一帧离开/无人样本。
- `/label?class=occupied|empty`：兼容旧标签，会分别映射到 `seated|absent`。

`/label` 当前把样本以 `8x8` 归一化 PGM 灰度图返回给浏览器下载，文件名包含类别和序号。这样训练输入和设备侧推理特征完全一致。先用显示器上沿或桌面前方的真实安装角度采集样本，再训练模型。

## 模型流程

1. 连接 `Bill-Camera` 热点。
2. 使用 `/label?class=absent` 采集离开/无人样本。
3. 使用 `/label?class=seated` 采集正面坐姿、前斜侧面坐姿、半离开、不同衣服、不同光照样本。
4. 把下载的 `.pgm` 放入 `model/dataset/absent/` 和 `model/dataset/seated/`。
5. 运行：

```powershell
python model\train_seat_model.py --dataset model\dataset --out main\seat_model_data.h
```

6. 重新 `idf.py build flash`。

第一版模型是本地 int8 二分类器，输入为画面中部偏上的 `8x8` 归一化灰度特征，用于区分桌前坐姿和离开。特征会先减去 ROI 全局平均亮度，再做有限幅度缩放，降低整体光照变化影响。后续若迁入完整 TFLite Micro 坐姿/人体模型，可复用当前 `SeatModel` 接口，不需要重写计时和 Web 接口。

## 本地脚本

- `tools/use-idf-env.ps1`：为当前终端注入 `IDF_PATH`、`IDF_TOOLS_PATH` 和本机 Python 路径。
- `tools/install-idf.ps1`：按本机固定路径安装 ESP-IDF 工具链，日志写到 `D:\Espressif\install-esp-idf-v5.4.4.log`。
- `tools/build-idf.ps1`：执行 `set-target + build`，也支持 `-Flash`、`-Monitor`、`-Port COM13`。

示例：

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
```
