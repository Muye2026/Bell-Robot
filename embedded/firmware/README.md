# Legacy Arduino Firmware

本目录是旧的 PlatformIO + Arduino 回退工程。

当前策略：
- 主线开发只在 `../firmware-idf/` 继续推进。
- 本目录仅保留用于回退对比、历史参考和应急验证。
- 未经明确要求，不要继续在这里添加新功能。
- 等 `firmware-idf` 完成摄像头、OLED、蜂鸣器、按键、AP 与识别链路的实机闭环验证后，再统一删除本目录和相关说明。

如果只是正常开发、编译、烧录，请优先进入 `../firmware-idf/`。
