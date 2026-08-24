# Bell Robot Tech One CNC R2

当前外观主线。比 `tech1-cnc-r1` 简单得多的形态：一块 `60 × 20 × 31mm` 的小砖，正面几乎全是显示。

## 形态

- **正面**：`60 × 20mm` 圆角矩形，圆角 `5mm`。深色显示面板占满，右端一个摄像头开孔。
- **CNC 件只有四周一圈**：`2.5mm` 壁厚的铝合金框，前后都是通的。前面用深色面板封，后面用盖板封，元器件和电池填在中间的腔体里。
- **厚度 `31mm`**：不是造型取舍，这是 Apple Studio Display 的外壳厚度（VESA 版深度 1.2 in / 3.1 cm，平背所以是均匀厚度）。目的是放在 Studio Display 旁边时读起来是同一块板厚。来源见 [Apple Studio Display 技术规格](https://www.apple.com/studio-display/specs/) 和 [Apple Support 规格页](https://support.apple.com/en-us/111890)。

从轴测图能看出一个直接后果：`31mm` 深比 `20mm` 高还大，所以它比正面看起来更敦实。这是把厚度钉死在 Studio Display 上换来的。

## 相比 R1 去掉了什么

- 八边形折面机身和三块分离的黑色功能面板。
- **穿孔铝片、铣削沉台，以及整个钻孔工艺问题**。这里没有任何穿孔——正面就是深色面板后面放 LED。更便宜，而且熄灭状态更好看：未点亮的点是淡纹理，不是钻孔铝片那种硬邦邦的黑点场。
- 后支撑臂。`60 × 31mm` 的底面积配 `20mm` 高，本身就是个稳的小砖，不需要挂在显示器上。

## 显示

点阵不是拍脑袋定的，是从正面开口减去摄像头之后推导出来的：

| | |
| --- | --- |
| 有效区 | `43.4 × 12.6mm` |
| 间距 | `1.4mm` |
| 点阵 | `32 × 10` = 320 点 |
| 驱动 | 2 × IS31FL3733 |

**32 列是那个决定性的数字。** `MM:SS` 用共用的 5×7 字库需要 30 列，所以左右各剩一列余量，字号没有放大空间。`2.0mm` 间距只能给出 24 列，根本显示不了倒计时——这是间距必须做到这么细的原因。

**10 行正好等于**「倒计时 7 行 + 间隔 1 行 + 进度条 2 行」，也就是固件 `display_layout.h` 里的 `Compact` 布局。再少一行就退回 `Minimal`，只剩倒计时。

### 评审图和设备画的是同一帧

字库直接取自固件的 `kFont5x7`，进度条轨道几何也逐行镜像 `renderDisplayFrame()`。可以验证：

```bash
# 固件侧
cd embedded/firmware-idf && ./tools/test-host.sh
```

`test_display_layout` 会把 `32×10` 的实际帧以字符网格打印出来，和本目录渲染的点位一一对应。之前有一版这里用了手绘字库，`45:00` 亮 71 点而固件是 77 点——评审图在展示一台设备永远不会产生的画面。

同理，渲染的是**一个真实状态**而不是拼凑：45 分钟目标、已坐 27 分钟，所以倒计时显示 `18:00`、进度条 60%。早先画的 `45:00` 配 60% 进度条是固件不可能输出的帧（剩余 45 分钟意味着已坐 0 分钟）。

## 运行

```bash
/Users/muye11/.venvs/cad/bin/python industrial-design/tech1-cnc-r2/tech1_cnc_r2_pipeline.py
```

改间距看看别的分辨率：

```bash
/Users/muye11/.venvs/cad/bin/python industrial-design/tech1-cnc-r2/tech1_cnc_r2_pipeline.py --pitch 1.2
```

约 10 秒跑完。

## 输出

CAD（`_cad-output/tech1-cnc-r2/`）：

- `tech1-cnc-r2.step` / `.stl` / `.obj` / `.mtl`
- `tech1-cnc-r2-validation.json`

OBJ 额外带了 LED PCB、主板、摄像头模组和电池的参考体，用来看内部堆叠；STEP/STL 只有外观件。

渲染（`_render-raster/tech1-cnc-r2/`）：

- 六视图 + 三张轴测
- `tech1-cnc-r2-display_detail.png`：正常环境光，看物理点阵
- `tech1-cnc-r2-display_lit.png`：压暗环境光，看点亮可读性
- `tech1-cnc-r2-multi-view-overview.png`
- `tech1-cnc-r2-display-lit.png`：带参数标注的主图
- `tech1-cnc-r2-notes.md`

这两个目录在 `.gitignore` 里，是可再生产物。

## 校验

脚本会检查并在失败时中止：

- 外包络 `60 × 31 × 20mm`。
- 深度等于 Studio Display 外壳厚度。
- **点阵列数 ≥ 30**，也就是 `MM:SS` 放得下。这条不满足的话整个正面就没意义了。
- 行数够不够放进度条。
- 内部件（LED PCB / 主板 / 摄像头 / 电池）三个方向都能塞进腔体。
- 导出的 STEP 能重新导入。

```bash
cat _cad-output/tech1-cnc-r2/tech1-cnc-r2-validation.json
```

## 两个待决问题

### 现有开发板放不进去

Freenove ESP32-S3 CAM 板是 `57 × 28mm`，腔体只有 `15mm` 高。没有任何摆法能放进去——这个形态要求围绕 WROOM-1U 模组自绘 PCB。校验里 `freenove_dev_board_fits` 一项就是盯这个的。

### 电池续航偏短

腔体 `55 × 27.9 × 15mm` = `23.0cm³`，扣掉 LED PCB、主板、摄像头和电池后剩 `10.8cm³`。一颗 `802040` 软包（`40 × 20 × 8mm`）按保守的 `0.25 Wh/cm³` 算约 `430mAh`，市售同尺寸电芯一般标到 600mAh 左右。摄像头每 500ms 采样加上点阵，平均电流大概 120–180mA，也就是 2.5–5 小时。

这意味着它更适合做成 USB-C 供电、电池只负责挪动位置的设备，而不是全天无线。加大电池就得加大机身，而深度已经被 Studio Display 钉死了。

## 天线：R1 的约束没有消失

AP 热点是核心功能，铝框裹着 2.4GHz 天线仍然是问题。R1 靠塑料后支撑臂解决，R2 已经没有这个零件了。**后盖是最合适的候选**——改成塑料件、天线贴在内侧。它背对用户，不是可见面，改材质的外观代价最小。

这一点脚本目前没有建模，是留给下一轮的。

## 尚未进入量产深化

外观和堆叠草模，不是量产结构图。尚未定义或验证：铝框壁厚对刚度是否够、面板固定与密封、LED PCB 叠层和散热路径、腔体内真实元件排布、充电电路与 USB-C 位置、天线位置的实测、螺丝或胶接方案、CNC DFM 成本复核。
