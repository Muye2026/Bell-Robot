# ESP-IDF 主线固件

ESP32-S3 N16R8 CAM 久坐提醒固件。设备固定创建 `Bell-Robot` 热点，手机连接后访问 `http://192.168.4.1/` 查看摄像头、调整计时、重置和采集样本。

## 构建

```powershell
cd D:\Project\Bell-Robot\embedded\firmware-idf
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1
```

烧录：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-idf.ps1 -Flash -Port COM13
```

## AP 与接口

- SSID：`Bell-Robot`
- 密码：`12345678`
- URL：`http://192.168.4.1/`

接口：

- `/`：预览页和设置页。
- `/capture`：当前摄像头 JPEG。
- `/status`：状态 JSON，含模型概率、推理耗时、倒计时配置等诊断字段。
- `/settings`：读取/保存倒计时和离场容忍分钟数。
- `/reset`：重置计时并重新校准 ROI fallback。
- `/label?class=absent|seated`：采集训练样本。

## 主要文件

- `main/main.cpp`：摄像头、AP、HTTP、状态机、蜂鸣器、按钮和 OLED。
- `main/seat_model.*`：本地 int8 模型推理接口。
- `main/seat_model_data.h`：训练脚本生成的模型权重。
- `main/ssd1306_spi.*`：SPI SSD1306 文本显示驱动。
- `tools/build-idf.ps1`：本机 ESP-IDF 构建/烧录入口。

## 模型

样本目录在仓库根目录：

```text
model/dataset/absent/
model/dataset/seated/
```

从仓库根目录训练：

```powershell
python model\train_seat_model.py --dataset model\dataset --out embedded\firmware-idf\main\seat_model_data.h --balance-classes
```

当前模型使用 `8x8` 归一化灰度特征；OLED 竖屏显示状态、识别概率、连续帧诊断和倒计时。
