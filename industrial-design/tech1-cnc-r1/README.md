# Bell Robot Tech One CNC R1

> 外观主线已移到 `industrial-design/tech1-cnc-r2/`：`60 × 20 × 31mm` 小砖，四周一圈 CNC 铝框，正面整面点阵显示，没有穿孔。本目录保留为历史版本。
>
> R2 取消了穿孔铝片，因为深色面板的熄灭状态更好（未点亮的点是淡纹理，不是黑点场），也不用处理钻孔工艺和开孔率。本目录关于喷砂/阳极工序和铝壳屏蔽 Wi-Fi 的结论仍然适用。

铝合金 CNC 外观方向的建模、导出、渲染、对比和校验闭环。接替 `tech1-ocp-r1`，沿用同一套工具链（build123d/OCP + trimesh + Pillow，不依赖 FreeCAD 和 VTK）。

## 这一版换了什么

外观方向从「银色折面壳 + 三块分离黑色功能面板」改为：

- **整块铝正面**。左 OLED 黑窗、中央摄像头黑岛、右服务区黑面板全部取消，正面是一整面喷砂 + 本色阳极铝。
- **穿孔点阵显示**替代 OLED。孔是真实布尔切出来的，做在一块独立的 `0.5mm` 铝薄片上，压入 CNC 主体铣出的沉台。导出的 STEP 带真实孔阵，不是贴图。
- **摄像头保留独立开孔**。穿孔面拍不了照，镜头需要自己的通孔加盖板玻璃。
- **后支撑臂改塑料件**，作为 2.4GHz 天线的 RF 窗口。
- 端部斜切从 `5.4mm` 收到 `4.6mm`，正面侧边内收从 `2.2mm` 收到 `1.0mm`，让机身读作铣削实体而不是折面壳。
- 按键从正面移到顶面（`GPIO1` 消警/重校准、`GPIO2` 采样），保持正面完整。
- 底部软垫做进铣槽，只留 `0.35mm` 外露，不再是横跨正面底边的黑色厚块。

## 两个点阵档位

硬件分辨率还没定，所以两档都建模，从渲染而不是从表格里做决定。

| | matrix-a | matrix-b |
| --- | --- | --- |
| 点阵 | 32 x 8 | 32 x 16 |
| 点数 | 256 | 512 |
| 间距 | 2.0mm | 1.6mm |
| 孔径 | 0.9mm | 0.75mm |
| 有效区 | 62.0 x 14.0mm | 49.6 x 24.0mm |
| 孔板尺寸 | 68.0 x 20.0mm | 53.6 x 28.0mm |
| 窗口上下留铝 | 7.0mm | 3.0mm |
| 开孔率 | 11.98% | 15.07% |
| IS31FL3733 片数 | 2 | 3 |

`matrix-a` 八行刚好被一行 5x7 字体占满，`PROB` 和状态文字要永久移到网页端。`matrix-b` 放得下倒计时加一行进度条，但只能靠 1.6mm 间距才塞进 34mm 机身，窗口上下只剩 3.0mm 铝。

## 运行

```bash
/Users/muye11/.venvs/cad/bin/python industrial-design/tech1-cnc-r1/tech1_cnc_pipeline.py
```

单独跑一档：

```bash
/Users/muye11/.venvs/cad/bin/python industrial-design/tech1-cnc-r1/tech1_cnc_pipeline.py --variant matrix-a
/Users/muye11/.venvs/cad/bin/python industrial-design/tech1-cnc-r1/tech1_cnc_pipeline.py --variant matrix-b
```

完整跑两档约 25 秒。

## 输出

CAD（`_cad-output/tech1-cnc-r1/`）：

- `tech1-cnc-{variant}.step` / `.stl` / `.obj` / `.mtl`
- `tech1-cnc-{variant}-validation.json`

渲染（`_render-raster/tech1-cnc-r1/`）：

- `{variant}/tech1-cnc-{variant}-{front,rear,left,right,top,bottom}.png`
- `{variant}/tech1-cnc-{variant}-axo_{front,rear}_{left,right}.png`
- `{variant}/tech1-cnc-{variant}-display_detail.png`：显示窗特写，常规环境光，看物理孔阵
- `{variant}/tech1-cnc-{variant}-display_lit.png`：同一取景压暗环境光，看点亮后的可读性
- `{variant}/tech1-cnc-{variant}-multi-view-overview.png`
- `{variant}/tech1-cnc-{variant}-reference-vs-model.png`
- `tech1-cnc-display-resolution-comparison.png`：两档并排，同比例
- `tech1-cnc-iteration-notes.md`

这两个目录在 `.gitignore` 里，属于可再生产物，跑一次脚本就能重建。

### 为什么要两张显示窗渲染

`display_detail` 用正常环境光，反映白天看到的孔阵外观。`display_lit` 把非自发光部分压到 40% 亮度，模拟实际使用时「亮的是 LED、不是阳极面」的条件。奶白色亮点打在银色阳极上，在全环境光下几乎看不出来，哪怕图案完全正确——只看 `display_detail` 会误判可读性。

## 校验

脚本会检查并在失败时中止：

- 主体外包络 `130 x 18 x 34mm`。
- 后支撑臂后伸落在 `32-38mm`。
- **孔板体积等于「板体积 − 孔数 x πr²t」**。这是布尔的实际验证：漏孔或相邻孔合并都会让体积对不上，肉眼在渲染里看不出来。
- 孔径小于间距的 75%。
- 导出的 STEP 能用 OCP 重新导入并取得合理包络。

```bash
cat _cad-output/tech1-cnc-r1/tech1-cnc-matrix-a-validation.json
```

## 两个实现上的坑

**主体必须是朝向正确的实体。** `tech1-ocp-r1` 的 `make_shell_solid` 把散面拼成壳，面朝向不一致，算出来的体积是负的。那边只做导出和渲染所以一直没事；这边一旦做布尔就出问题——OpenCascade 把负体积实体理解为目标形状的补集，「切」会变成「并」。本版用单个平面拉伸成 prism，并在 `orient_solid()` 里统一绕向。`tech1-ocp-r1` 如果以后要加布尔运算，需要同样处理。

**网格缓存不能只用 `id()` 做 key。** 缓存必须同时持有 shape 引用，否则前一个变体的形状被回收后，后一个变体的新形状会复用同一个内存地址，缓存就会静默返回上一个变体的几何。这个 bug 的表现是 `--variant all` 时 matrix-b 的孔完全不渲染，而单独跑 matrix-b 一切正常。

## 尚未进入量产深化

仍然是外观和布局草模，不是量产结构图。尚未定义或验证：

- 铣削主体的壁厚和加强筋布局。
- 孔板压配公差和固定方式。
- 相邻孔之间的遮光隔离。
- 阳极氧化在亚毫米孔内的膜厚增长。
- LED PCB 叠层和散热路径。
- 实测摄像头模组和开发板装配。
- 天线位置的实际 RF 实测（目前只有 keepout 体积）。
- 螺丝柱、线束走向、CNC DFM 成本复核。
