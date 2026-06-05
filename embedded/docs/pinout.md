# 引脚连接

当前依据用户提供的 Freenove ESP32-S3 WROOM Pinout，以及官方 `Sketch_07.1_CameraWebServer/camera_pins.h`。ESP-IDF 主线固件引脚集中在 `firmware-idf/main/app_config.h`。

## OLED

当前屏幕是 SPI 版 SSD1306，丝印为 `GND / VCC / SCL / SDA / RES / DC / CS`。这里的 `SCL/SDA` 不是 I2C，而是 SPI 时钟和 SPI 数据：

| OLED | ESP32-S3 GPIO | 说明 |
| --- | ---: | --- |
| VCC | 3V3 | 优先使用 3.3V |
| GND | GND | 地 |
| SCL | GPIO 48 | SPI 时钟，可在 `PIN_OLED_CLK` 修改 |
| SDA | GPIO 47 | SPI 数据，可在 `PIN_OLED_MOSI` 修改 |
| RES | GPIO 40 | 可在 `PIN_OLED_RESET` 修改 |
| DC | GPIO 41 | 数据/命令选择，可在 `PIN_OLED_DC` 修改 |
| CS | GPIO 42 | 可在 `PIN_OLED_CS` 修改 |

2026-06-05 调整：OLED 的 `SCL/SDA` 从 GPIO14/GPIO3 迁移到 GPIO48/GPIO47，尽量使用开发板当前上排引脚；`RES/DC/CS` 继续使用 GPIO40/GPIO41/GPIO42。

不要按 I2C 屏理解这组 `SCL/SDA`；该屏幕因为有 `RES/DC/CS`，所以当前按 SPI 驱动，没有 I2C 地址。

## 无源蜂鸣器

| 无源蜂鸣器 | ESP32-S3 默认 GPIO | 说明 |
| --- | ---: | --- |
| 正极 | GPIO 21 | PWM 方波输出，可在 `PIN_BUZZER` 修改 |
| 负极 | GND | 地 |

当前按两引脚普通无源蜂鸣器处理，固件使用 LEDC PWM 输出 `BUZZER_FREQUENCY_HZ` 方波。若蜂鸣器电流较大或声音太小，应使用三极管或 MOSFET 驱动，不要长期直接由 GPIO 承载大电流。

## 按键

| 按键 | ESP32-S3 默认 GPIO | 说明 |
| --- | ---: | --- |
| 重置/重校准按钮一端 | GPIO 1 | `INPUT_PULLUP`，按下会消警并重新校准 |
| 采样按钮一端 | GPIO 2 | `INPUT_PULLUP`，按下保存当前 JPEG + 元数据 |
| 另一端 | GND | 按下为低电平 |

两个按钮都按“另一端接 GND、GPIO 侧走内部上拉”的接法，不需要改动现有 `GPIO1` 按钮线路，只是额外增加一个接到 `GPIO2` 的按钮。

不默认使用 GPIO0，因为它是 Boot 引脚，可能影响下载模式；也不建议把第二按钮接到 GPIO3 / GPIO45 / GPIO46 这类 strapping 管脚。

## 摄像头

当前摄像头已切到 `DC5640 AF 120度`。在没有新的排线定义前，固件暂沿用 Freenove OV5640 类接口映射；若现场发现排线或镜像方向不一致，再回到 `firmware-idf/main/app_config.h` 复核。

| 摄像头信号 | GPIO | 固件字段 |
| --- | ---: | --- |
| CAM_SIOD | GPIO 4 | `PIN_CAM_SIOD` |
| CAM_SIOC | GPIO 5 | `PIN_CAM_SIOC` |
| CAM_VSYNC | GPIO 6 | `PIN_CAM_VSYNC` |
| CAM_HREF | GPIO 7 | `PIN_CAM_HREF` |
| CAM_XCLK | GPIO 15 | `PIN_CAM_XCLK` |
| CAM_Y2 | GPIO 11 | `PIN_CAM_D0` |
| CAM_Y3 | GPIO 9 | `PIN_CAM_D1` |
| CAM_Y4 | GPIO 8 | `PIN_CAM_D2` |
| CAM_Y5 | GPIO 10 | `PIN_CAM_D3` |
| CAM_Y6 | GPIO 12 | `PIN_CAM_D4` |
| CAM_Y7 | GPIO 18 | `PIN_CAM_D5` |
| CAM_Y8 | GPIO 17 | `PIN_CAM_D6` |
| CAM_Y9 | GPIO 16 | `PIN_CAM_D7` |
| CAM_PCLK | GPIO 13 | `PIN_CAM_PCLK` |

若暂时没有确认摄像头引脚，可先把 `CAMERA_ENABLED` 设为 `0`，固件会用串口输入模拟有人/无人状态，方便先验证 OLED、蜂鸣器和计时逻辑。
