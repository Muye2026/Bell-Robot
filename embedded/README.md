# 涔呭潗鎻愰啋澶栫疆纭欢

寮€鍙戜竴涓嫭绔嬭繍琛岀殑涔呭潗鎻愰啋纭欢鍘熷瀷锛氶€氳繃澶栫疆鎽勫儚澶村垽鏂瀛愬尯鍩熸槸鍚︽湁浜哄潗涓嬶紝杩炵画鍧愭弧 45 鍒嗛挓鍚庨€氳繃 OLED 鍜岃渹楦ｅ櫒鎻愰啋绔欑珛锛屽苟鐢?0.96 瀵?SSD1306 OLED 鏄剧ず鐘舵€佸拰鍊掕鏃躲€?
## 褰撳墠鏂规

- 涓绘帶锛欶reenove ESP32-S3 N16R8 CAM 寮€鍙戞澘锛?6MB Flash + 8MB PSRAM銆?- 鎽勫儚澶达細OV5640CSP锛屼笣鍗?`VVS OV5640CSP 8225N VC`銆?- 鏄剧ず锛歋SD1306 128x64 OLED锛孲PI 鎺ュ彛锛屼笣鍗?`GND / VCC / SCL / SDA / RES / DCC / CS`銆?- 璇嗗埆锛氱涓€鐗堝彧鍋氭瀛?ROI 鍖哄煙鏈変汉/鏃犱汉鍒ゆ柇锛屼笉鍋氫汉鑴歌瘑鍒紝涓嶄繚瀛樺浘鍍忋€?- 鎻愰啋锛歄LED 鎻愮ず + 涓ゅ紩鑴氭棤婧愯渹楦ｅ櫒銆?- 浜や簰锛氬崟鎸夐敭鐢ㄤ簬纭鎻愰啋鎴栭噸缃€?- 璁℃椂锛氬潗涓嬪悗 45 鍒嗛挓鍊掕鏃讹紱绂诲紑 5 绉掑悗鎻愮ず鍗冲皢閲嶇疆锛涚寮€瓒呰繃 10 绉掗噸鏂板紑濮嬨€?
## 鐩綍缁撴瀯

```text
embedded/
  AGENTS.md
  README.md
  .gitignore
  docs/
    architecture.md
    bom.md
    pinout.md
  firmware/
    platformio.ini
    include/
      config.h
    src/
      main.cpp
```
## 2026-05-01 更新

- 更新 `embedded/.gitignore`，修正原有 `.pio/` 无法覆盖 `firmware/.pio/` 的问题。
- 新增 PlatformIO 构建缓存、`compile_commands.json`、`.clangd/`、编辑器配置和常见 OS 杂项忽略规则。
- 本次仅调整忽略规则，未改动固件逻辑。


## 褰撳墠鏂囦欢

- `README.md`锛氶」鐩鏄庛€佽繘搴﹁褰曘€侀噷绋嬬鍜屼氦鎺ヤ俊鎭€?- `docs/architecture.md`锛氱‖浠舵灦鏋勩€佽蒋浠舵ā鍧椼€佺姸鎬佹満鍜岄殣绉佽竟鐣屻€?- `docs/bom.md`锛氭帹鑽愬厓鍣ㄤ欢娓呭崟鍜屽彲閫夊寮恒€?- `docs/pinout.md`锛歄LED銆佽渹楦ｅ櫒銆佹寜閿€佹憚鍍忓ご寮曡剼璇存槑銆?- `firmware/platformio.ini`锛欵SP32-S3 N16R8 CAM PlatformIO 宸ョ▼閰嶇疆銆?- `firmware/include/config.h`锛氳鏃堕槇鍊笺€丟PIO銆佹憚鍍忓ご鍜?ROI 鍙傛暟銆?- `firmware/src/main.cpp`锛歄LED銆佽鏃剁姸鎬佹満銆佽渹楦ｅ櫒銆佹寜閿拰鎽勫儚澶村崰鐢ㄦ娴嬮鏋躲€?- `AGENTS.md`锛氭湰鐩綍鍚庣画鍗忎綔瑙勫垯銆?
## 鍥轰欢鏋勫缓

鍥轰欢浣跨敤 PlatformIO + Arduino-ESP32銆?
```powershell
cd D:\Project\Bill\embedded\firmware
pio run
```

鐑у綍锛?
```powershell
pio run -t upload
```

涓插彛鐩戣锛?
```powershell
pio device monitor
```

## 褰撳墠瀹炵幇璇存槑

`firmware/src/main.cpp` 宸插寘鍚畬鏁磋鏃剁姸鎬佹満銆丱LED UI銆佽渹楦ｅ櫒銆佹寜閿€昏緫鍜屾憚鍍忓ご鍗犵敤妫€娴嬫帴鍙ｃ€傛憚鍍忓ご寮曡剼鍜屾娴嬮槇鍊奸泦涓湪 `firmware/include/config.h`锛屽疄闄呬娇鐢ㄥ墠闇€瑕佹寜鍏蜂綋 ESP32-S3 N16R8 CAM 寮€鍙戞澘鍘熺悊鍥炬牳瀵瑰紩鑴氥€傚綋鍓嶄富鎺у凡鏈?8MB PSRAM锛屽浐浠堕粯璁や娇鐢?OV5640CSP銆丵QVGA + PSRAM 甯х紦鍐层€?
## 纭欢璧勬枡鏉ユ簮

- 鐢ㄦ埛璧勬枡鐩綍锛歚D:\BaiduNetdiskDownload\92108\ESP32-S3 CAM寮€鍙戞澘璧勬枡`銆?- 寮曡剼鍥撅細`ESP32S3_Pinout.png`锛屾爣棰樹负 `FREENOVE ESP32-S3 WROOM Pinout`銆?- 瀹樻柟鎽勫儚澶寸ず渚嬶細`C\Sketches\Sketch_07.1_CameraWebServer`锛岀ず渚嬮€夋嫨 `CAMERA_MODEL_ESP32S3_EYE`銆?- 瀹樻柟 `camera_pins.h` 涓?`CAMERA_MODEL_ESP32S3_EYE` 鐨勬憚鍍忓ご寮曡剼宸插拰鏈伐绋?`config.h` 瀵归綈銆?
## 閲岀▼纰?
1. 2026-04-29锛氬畬鎴?`embedded` 鍒濆椤圭洰缁撴瀯銆佹枃妗ｅ拰 PlatformIO 鍥轰欢楠ㄦ灦銆?2. 2026-04-29锛氬疄鐜?45 鍒嗛挓涔呭潗璁℃椂銆?0 绉掓殏绂诲蹇嶃€?0 绉掔寮€閲嶇疆銆丱LED 鏄剧ず銆佽渹楦ｅ櫒鎻愰啋鍜屾寜閿‘璁ら€昏緫銆?3. 2026-04-29锛氭柊澧?`AGENTS.md` 绾︽潫鍚庣画鍗忎綔瑙勫垯銆?4. 2026-04-29锛氱敤鎴风‘璁ゆ敼鐢?ESP32-S3-WROOM-1-MCN16R8锛涗富绾跨‘瀹氫负 PlatformIO + Arduino-ESP32銆?5. 2026-04-29锛氬悎骞?`寮€鍙戣繘搴?md` 鍒?`README.md`锛屽悗缁?README 鍚屾椂浣滀负寮€鍙戣繘搴︺€侀噷绋嬬鍜屼氦鎺ユ枃妗ｃ€?6. 2026-04-29锛氱敤鎴风‘璁ゆ柊纭欢涓?ESP32-S3 N16R8 CAM + OV5640CSP锛屼富绾夸粠瑁?ESP32-S3-WROOM-1-MCN16R8 璋冩暣涓?CAM 寮€鍙戞澘銆?7. 2026-04-30锛氭牴鎹?Freenove 璧勬枡鐩綍鍜屽畼鏂?`camera_pins.h` 鏍稿鎽勫儚澶村紩鑴氾紱鎸夐敭榛樿鑴氫粠 GPIO0 鏀逛负 GPIO1锛岄伩鍏嶅崰鐢?Boot 寮曡剼銆?8. 2026-04-30锛氭寜鐢ㄦ埛瑕佹眰灏?OLED I2C 璋冩暣涓?`SDA=GPIO3`銆乣SCL=GPIO14`锛涜渹楦ｅ櫒浠?GPIO14 璋冩暣鍒?GPIO21锛岄伩鍏嶅拰 OLED SCL 鍐茬獊銆?9. 2026-04-30锛氳渹楦ｅ櫒纭鏀逛负涓ゅ紩鑴氭棤婧愯渹楦ｅ櫒锛屽浐浠舵彁閱掕緭鍑轰粠 GPIO 楂樹綆鐢靛钩鏀逛负 `tone()` PWM 鏂规尝銆?10. 2026-04-30锛氱‘璁?OLED 鏄?SPI 鐗?SSD1306锛屼笣鍗?`GND / VCC / SCL / SDA / RES / DCC / CS`锛涙樉绀洪┍鍔ㄤ粠 I2C 鏀逛负 SPI銆?11. 2026-04-30锛氶拡瀵?OLED 浜睆浣嗕贡鐮侊紝鍥轰欢鍔犲叆 OLED 鎺у埗鑴氫笂鐢垫媺绋炽€佺嫭绔嬬‖澶嶄綅锛屼互鍙婂惎鍔ㄩ樁娈甸粦鐧藉叏灞忛棯鐑?+ 杈规鏂囧瓧鑷銆?12. 2026-04-30锛氭柊澧炴憚鍍忓ご棰勮妯″紡锛屽紑鍙戞澘鑷缓 Wi-Fi 鐑偣 `Bill-Camera`锛屾祻瑙堝櫒璁块棶 `http://192.168.4.1/` 鏌ョ湅鎽勫儚澶?JPEG 鐢婚潰銆?13. 2026-04-30锛氶拡瀵规墜鏈鸿闂綉椤靛け璐ワ紝鎽勫儚澶撮瑙堢儹鐐规敼涓烘樉寮?`192.168.4.1/24` 鍥哄畾 AP IP锛屽苟鍏抽棴 Wi-Fi sleep銆?14. 2026-04-30锛氱敤鎴风‘璁?OLED 涓?`128x64 SPI SSD1306`锛涙樉绀哄簱鍒囨崲涓?U8g2锛岄┍鍔ㄥ浐瀹氫负 SSD1306锛涙憚鍍忓ご寮€鍚瀭鐩寸炕杞慨姝ｄ笂涓嬮鍊掋€?15. 2026-04-30锛氫慨澶嶉瑙堟ā寮忎笅涓嶆娴嬪崰鐢ㄧ殑闂锛涙憚鍍忓ご鏀逛负鐏板害甯ф娴嬶紝缃戦〉棰勮鏃跺啀杞?JPEG锛涜繛缁娴嬪埌鍗犵敤绾?3 绉掑悗寮€濮嬪€掕鏃躲€?16. 2026-04-30锛氶拡瀵硅瑙﹀彂銆佺寮€涓嶅仠姝㈠拰鎸夐敭鏃犳晥锛屾柊澧炲紑鏈虹┖妞呮牎鍑嗐€佸浐瀹氱┖妞呭熀绾裤€丱LED 璋冭瘯鍊?`D/B/Btn`銆乣/status` 鐘舵€佹帴鍙ｅ拰 `/reset` 閲嶇疆鎺ュ彛锛涙寜閿噸缃悓鏃惰Е鍙戦噸鏂版牎鍑嗐€?17. 2026-04-30锛氭寜鐢ㄦ埛瑕佹眰灏嗚鏃朵腑绂诲紑閲嶇疆鏃堕棿鏀逛负 10 绉掞紱绂诲紑 5 绉掑悗杩涘叆鍗冲皢閲嶇疆鎻愮ず锛?0 绉掑悗鏈疆璁℃椂浣滃簾銆?
## 宸查獙璇?
- 2026-04-29锛氫娇鐢?`C:\Users\23171\.platformio\penv\Scripts\pio.exe run` 缂栬瘧閫氳繃銆?- 2026-04-29锛氬伐绋嬪綋鍓嶄娇鐢?`esp32-s3-wroom-1-mcn16r8` PlatformIO 鐜锛涘簳灞?board ID 鏆傜敤 `esp32-s3-devkitc-1` 鍏煎閰嶇疆銆?- 2026-04-29锛氬凡灏?PlatformIO 鐜鍚嶆洿鏂颁负 `esp32-s3-wroom-1-mcn16r8`锛岄厤缃?16MB Flash銆?MB PSRAM銆乣BOARD_HAS_PSRAM` 鍜?`default_16MB.csv` 鍒嗗尯銆?- 2026-04-29锛氬垏鎹?ESP32-S3-WROOM-1-MCN16R8 鍚庨噸鏂扮紪璇戦€氳繃锛沗.pio/` 宸插姞鍏?`.gitignore`锛屼綔涓烘瀯寤虹紦瀛樹笉绾冲叆浜ゆ帴鏂囦欢銆?- 2026-04-29锛歅latformIO 鐜鍚嶆洿鏂颁负 `esp32-s3-n16r8-cam`锛岀户缁娇鐢?16MB Flash銆?MB PSRAM銆乣BOARD_HAS_PSRAM` 鍜?`default_16MB.csv` 鍒嗗尯銆?- 2026-04-29锛氬垏鎹?ESP32-S3 N16R8 CAM + OV5640CSP 鍚庨噸鏂扮紪璇戦€氳繃銆?- 2026-04-30锛欶reenove 瀹樻柟 `CAMERA_MODEL_ESP32S3_EYE` 寮曡剼鍜?`config.h` 鎽勫儚澶撮厤缃竴鑷淬€?- 2026-04-30锛氭寜閿剼浠?GPIO0 鏀逛负 GPIO1 鍚庨噸鏂扮紪璇戦€氳繃銆?- 2026-04-30锛歄LED 鏀逛负 `SDA=GPIO3`銆乣SCL=GPIO14`锛岃渹楦ｅ櫒鏀逛负 `GPIO21` 鍚庨噸鏂扮紪璇戦€氳繃銆?- 2026-04-30锛氭棤婧愯渹楦ｅ櫒鏀逛负 `tone()` PWM 鏂规尝椹卞姩鍚庨噸鏂扮紪璇戦€氳繃銆?- 2026-04-30锛欳OM13 璇嗗埆涓?CH343 涓插彛锛涘叧闂?`ARDUINO_USB_CDC_ON_BOOT`锛岃搴旂敤鏃ュ織杈撳嚭鍒?CH343 UART銆?- 2026-04-30锛氶€氳繃 `COM13` 鎴愬姛鐑у綍鍒板疄鐗╂澘锛沞sptool 璇嗗埆涓?ESP32-S3锛屾娴嬪埌 Embedded PSRAM 8MB锛孧AC 涓?`e0:72:a1:f6:39:10`銆?- 2026-04-30锛氫覆鍙ｅ惎鍔ㄦ棩蹇楀凡杈撳嚭 `Ready. If CAMERA_ENABLED=0, send 1 for occupied, 0 for empty.`锛涙湭鐪嬪埌 `SSD1306 init failed` 鎴?`Camera init failed`銆?- 2026-04-30锛歄LED 涓嶆樉绀烘椂鍔犲叆 I2C 鎵弿锛沗SDA/SCL=3/14`銆乣14/3`銆乣41/42`銆乣42/41` 鍧囨湭鎵弿鍒?I2C 璁惧锛岄渶浼樺厛妫€鏌?OLED 鎺ョ嚎銆佷緵鐢点€丟ND 鎴栨ā鍧楁帴鍙ｇ被鍨嬨€?- 2026-04-30锛氱敤鎴风‘璁?OLED 鏄?SPI 鐗堬紝涓濆嵃 `GND / VCC / SCL / SDA / RES / DCC / CS`锛涘浐浠跺凡鏀逛负 SPI SSD1306锛屽綋鍓嶆帴绾夸负 `SCL=GPIO14`銆乣SDA=GPIO3`銆乣RES=GPIO40`銆乣DCC=GPIO41`銆乣CS=GPIO42`銆?- 2026-04-30锛歋PI OLED 鐗堟湰宸茬紪璇戦€氳繃锛屼絾鐑у綍鏃?`COM13` 鏈灇涓撅紝闇€閲嶆柊鎻掓嫈 USB 鍚庡啀涓婁紶銆?- 2026-04-30锛氶噸鏂版彃绾垮悗璁惧鏋氫妇涓?`COM14`锛孲PI OLED 鍥轰欢宸叉垚鍔熺儳褰曪紱esptool 鏄剧ず USB mode 涓?`USB-Serial/JTAG`锛孭SRAM 8MB 姝ｅ父銆?- 2026-04-30锛氬綋鍓嶈澶囨灇涓句负 `COM13` / CH343锛汷LED 璇婃柇鍥轰欢宸查噸鏂扮紪璇戝苟鎴愬姛鐑у綍锛岀儳褰曟棩蹇楃‘璁?ESP32-S3銆?MB PSRAM銆丮AC `e0:72:a1:f6:39:10`銆?- 2026-04-30锛氭憚鍍忓ご棰勮鍥轰欢宸茬紪璇戝苟閫氳繃 `COM13` 鎴愬姛鐑у綍锛涢瑙堟ā寮忎笅鎽勫儚澶翠娇鐢?QVGA JPEG锛屾殏鏃舵殏鍋?ROI 涔呭潗璇嗗埆銆?- 2026-04-30锛氬浐瀹?AP IP 鐗堟憚鍍忓ご棰勮鍥轰欢宸茬紪璇戝苟閫氳繃 `COM13` 鎴愬姛鐑у綍銆?- 2026-04-30锛歚128x64 SPI SSD1306 + U8g2` 鏄剧ず鐗堟湰宸茬紪璇戝苟閫氳繃 `COM13` 鎴愬姛鐑у綍锛涙憚鍍忓ご淇濈暀 `CAMERA_VFLIP=true`銆?- 2026-04-30锛歄LED 涔辩爜鍘熷洜瀹氫綅涓烘帴绾块敊璇細`DC` 鍜?`CS` 琚悓鏃舵帴鍒?GPIO42锛涘浐浠跺綋鍓嶈姹?`RES=GPIO40`銆乣DC=GPIO41`銆乣CS=GPIO42`銆?- 2026-04-30锛氭憚鍍忓ご棰勮涓?ROI 鍗犵敤妫€娴嬪苟琛岀増鏈凡缂栬瘧骞堕€氳繃 `COM13` 鎴愬姛鐑у綍锛沗PRESENCE_ON_FRAMES=6`銆乣CAMERA_SAMPLE_INTERVAL_MS=500`锛岀害 3 绉掔‘璁ゅ潗涓嬨€?- 2026-04-30锛氱┖妞呮牎鍑?璋冭瘯鍊?鎸夐敭閲嶆柊鏍″噯鐗堟湰宸茬紪璇戝苟閫氳繃 `COM13` 鎴愬姛鐑у綍锛涘綋鍓?`ROI_DIFF_THRESHOLD=35`锛屾牎鍑嗗抚鏁颁负 8 甯с€?- 2026-04-30锛氱寮€ 10 绉掗噸缃増鏈凡缂栬瘧骞堕€氳繃 `COM13` 鎴愬姛鐑у綍锛涘綋鍓?`AWAY_GRACE_MS=5000`銆乣AWAY_RESET_MS=10000`銆?
## 寰呮牳瀵?
- OV5640CSP 鎺掔嚎鍜屼緵鐢垫槸鍚﹀尮閰嶅疄闄呭紑鍙戞澘銆?- OLED SPI 褰撳墠蹇呴』鎺ヤ负 `SCL=GPIO14`銆乣SDA=GPIO3`銆乣RES=GPIO40`銆乣DCC/DC=GPIO41`銆乣CS=GPIO42`銆?- OLED 宸茬敱鐢ㄦ埛纭鏄?`128x64 SPI SSD1306`锛屽綋鍓嶅浐浠朵娇鐢?U8g2 鐨?SSD1306 4-wire software SPI 鏋勯€犲櫒锛涜嫢浠嶆樉绀轰贡鐮侊紝浼樺厛鏍稿 `RES/DCC/CS` 鏄惁鎺ュ弽銆佺煭鎺ユ垨鎺ヨЕ涓嶈壇銆?- 鎽勫儚澶撮瑙堟ā寮忕儹鐐逛负 `Bill-Camera`锛屽瘑鐮?`12345678`锛屽湴鍧€蹇呴』杈撳叆 `http://192.168.4.1/`锛涗笉瑕佷娇鐢?`https://192.168.4.1/`锛屽紑鍙戞澘褰撳墠娌℃湁 HTTPS 鏈嶅姟銆?- 鎵嬫満鑻ユ彁绀衡€滄棤浜掕仈缃戣繛鎺モ€濓紝闇€瑕侀€夋嫨缁х画浣跨敤璇?WLAN锛岄伩鍏嶈嚜鍔ㄥ垏鍥炶渹绐濈綉缁滄垨鍏朵粬 Wi-Fi銆?- 褰撳墠 ROI 妫€娴嬪熀浜庡惎鍔ㄥ悗鐨勭┖妞呭熀绾匡紱娴嬭瘯鏃跺簲鍏堣妞呭瓙绌虹潃鍚姩璁惧锛屽緟 OLED 杩涘叆涓荤晫闈㈠悗鍐嶅潗涓嬶紝杩炵画绾?3 绉掑悗搴斾粠 `WAIT/empty` 杩涘叆鍊掕鏃躲€?- OLED 璋冭瘯鍊艰鏄庯細`D` 鏄綋鍓?ROI 涓庣┖妞呭熀绾跨殑宸€硷紝`B` 鏄┖妞呭熀绾匡紝`Btn:H/L` 鏄寜閿數骞筹紱鎸夐敭鎺?GPIO1 鍒?GND锛屾寜涓嬪簲鏄剧ず `Btn:L`銆?- 鎵嬫満杩炴帴鐑偣鍚庡彲璁块棶 `http://192.168.4.1/status` 鏌ョ湅 JSON 鐘舵€侊紝璁块棶 `http://192.168.4.1/reset` 鍙繙绋嬮噸缃苟閲嶆柊鏍″噯銆?- 鎸夐敭榛樿浣跨敤 `GPIO1`锛屼竴绔帴 GPIO1锛屼竴绔帴 GND锛涢渶瀹炵墿鎺ョ嚎楠岃瘉銆?- 鏃犳簮铚傞福鍣ㄥ綋鍓嶄负姝ｆ瀬鎺?`GPIO21`銆佽礋鏋佹帴 GND锛涜嫢鐢垫祦杈冨ぇ鎴栧０闊冲お灏忥紝闇€澧炲姞涓夋瀬绠℃垨 MOSFET 椹卞姩銆?
## 涓嬩竴姝?
1. 纭 ESP32-S3 N16R8 CAM 寮€鍙戞澘寮曡剼鍜?OV5640CSP 鍏煎鎬с€?2. 鎺ョ嚎楠岃瘉 OLED銆佹寜閿€佽渹楦ｅ櫒銆?3. 鑻ユ憚鍍忓ご鏆傛湭鎺ュソ锛屽彲灏?`CAMERA_ENABLED` 璁句负 `0`锛屽厛鐢ㄤ覆鍙?`1/0` 妯℃嫙鏈変汉/鏃犱汉锛岄獙璇佽鏃剁姸鎬佹満銆?4. 涓婃澘鍚庤皟璇?ROI 浣嶇疆鍜?`ROI_DIFF_THRESHOLD` 闃堝€笺€?5. 涓哄悗缁?App/灏忕▼搴忛鐣?Wi-Fi HTTP JSON API锛岀敤浜庤鍙栫姸鎬併€佽缃彁閱掓椂闂淬€佹墜鍔ㄩ噸缃拰鏌ョ湅缁熻銆?
