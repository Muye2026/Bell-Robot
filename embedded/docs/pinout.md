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

## LED 点阵（IS31FL3733）

对应 `industrial-design/tech1-cnc-r2/` 的 CNC 外壳方案（`32 x 10` @ `1.4mm` 间距，320 点，2 片驱动），用点阵替代 OLED。**尚未在实机上验证**，引脚是按「避开已占用管脚」选的，硬件到手后需要复核。

| 点阵驱动板 | ESP32-S3 GPIO | 说明 |
| --- | ---: | --- |
| SDA | GPIO 38 | 可在 `PIN_MATRIX_SDA` 修改 |
| SCL | GPIO 39 | 可在 `PIN_MATRIX_SCL` 修改 |
| VCC | 3V3 | |
| GND | GND | |

选 GPIO38/39 而不是 OLED 腾出来的 47/48，是为了让两块屏可以同时接着做台面对比。等 OLED 彻底退役后可以迁回 47/48。

不要用 GPIO33-37：N16R8 模组的八线 PSRAM 占用这一段，接上去会导致启动失败。

芯片 I2C 地址由每片的 `ADDR1/ADDR2` 拉阻决定，在 `MATRIX_I2C_ADDRESSES` 里配置，数量必须不少于 `MATRIX_WIDTH x MATRIX_HEIGHT / 192` 算出的片数（编译期有 `static_assert` 兜底）。

固件里 `(x, y) -> (第几片, SW, CS)` 的映射集中在 `led_matrix_is31fl3733.cpp` 的 `matrixTarget()`。当前是按行优先线性编号的占位实现，**真实映射由 LED PCB 走线决定，打样前必须回到这个函数对齐**。

### 切换显示后端

不改代码的做法：

```bash
cd embedded/firmware-idf
idf.py -DDISPLAY_BACKEND=1 build     # IS31FL3733 LED 点阵
idf.py build                         # 默认 SSD1306 OLED
```

或者改 `main/app_config.h` 里的 `DISPLAY_BACKEND` 默认值。界面布局不需要跟着改——`display_layout.h` 按后端上报的画布尺寸推导，`128x64` 走三行布局，`32x10` 和 `32x16` 走倒计时加进度条，`32x8` 只剩倒计时。

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

若暂时没有确认摄像头引脚，可先注释掉摄像头初始化代码（`setupCamera()` 调用），固件会用串口输入模拟有人/无人状态，方便先验证 OLED、蜂鸣器和计时逻辑。
