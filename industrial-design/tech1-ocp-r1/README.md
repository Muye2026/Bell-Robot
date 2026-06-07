# Bell Robot Tech One A OCP CAD Pipeline R1

本目录是 `Tech One A` 在当前 Mac 上可运行的 CAD 建模、导出、渲染、对比和迭代闭环。它替代旧的 `tech1-step-r1` Windows FreeCAD 主链路，但仍参考旧脚本里的尺寸、命名和审阅输出思路。

## 参考资料

- `industrial-design/concept-r3/01-tech1-angular-a.png`
- `industrial-design/concept-r3/tech1-a-render-style-brief.md`
- `industrial-design/tech1-structure-r1/`
- `industrial-design/tech1-step-r1/`

## 工具链

不依赖 `FreeCADCmd`、`freecad`、Windows 路径或 VTK。

当前已验证使用 DXL 项目的 Python CAD 环境：

```bash
/Users/muye11/Desktop/Project/DXL/backend/.venv/bin/python industrial-design/tech1-ocp-r1/tech1_ocp_pipeline.py
```

脚本使用：

- `OCP/OpenCascade`：生成真实 CAD 实体、导出 STEP、重新导入 STEP 验证。
- `OCP BRepMesh` + `trimesh`：导出 STL/OBJ 网格。
- `numpy` + `Pillow`：软件渲染六视图、等轴侧视图、参考对比图和多视图总览图。

## 输出

完整运行会生成 `initial` 和 `final` 两轮。`final` 是本轮迭代后的推荐草模。

CAD 输出：

- `_cad-output/tech1-ocp-r1/tech1-a-ocp-final.step`
- `_cad-output/tech1-ocp-r1/tech1-a-ocp-final.stl`
- `_cad-output/tech1-ocp-r1/tech1-a-ocp-final.obj`
- `_cad-output/tech1-ocp-r1/tech1-a-ocp-final-validation.json`

渲染和审阅输出：

- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-front.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-rear.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-left.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-right.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-top.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-bottom.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-axo_front_right.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-axo_front_left.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-axo_rear_right.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-axo_rear_left.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-reference-vs-model.png`
- `_render-raster/tech1-ocp-r1/final/tech1-a-ocp-final-multi-view-overview.png`
- `_render-raster/tech1-ocp-r1/tech1-a-ocp-iteration-notes.md`

## 分步命令

只生成第一轮：

```bash
/Users/muye11/Desktop/Project/DXL/backend/.venv/bin/python industrial-design/tech1-ocp-r1/tech1_ocp_pipeline.py --variant initial
```

只生成最终轮：

```bash
/Users/muye11/Desktop/Project/DXL/backend/.venv/bin/python industrial-design/tech1-ocp-r1/tech1_ocp_pipeline.py --variant final
```

完整闭环：

```bash
/Users/muye11/Desktop/Project/DXL/backend/.venv/bin/python industrial-design/tech1-ocp-r1/tech1_ocp_pipeline.py --variant all
```

## 当前建模内容

最终草模保留 Tech One A 的核心外观和挂屏结构：

- `130 x 34 x 18mm` 细长主体。
- 左右端部镜像斜切，最终版单侧斜切量为 `5.4mm`。
- 主体前后面采用同一八边形截面，避免上视图变成前窄后宽的梯形。
- 正面左右外侧边做轻微镜像内收，最终版下端内收 `2.2mm`。
- 银色折面主壳，正面不是连续黑带。
- 左 OLED 黑色窗口、中央摄像头岛、右服务区三段分离黑色功能面板。
- 中央圆形镜头、镜头环、状态点。
- 右侧服务区按键、正面微孔、右端 USB/微孔。
- 后支撑臂、后软垫、底部居中前软垫。

## 验证摘要

脚本会检查：

- 主体外包络是否为 `130 x 18 x 34mm`。
- 后支撑臂后伸是否落在结构约束 `32-38mm`。
- 导出的 STEP 能否用 OCP 重新导入并取得合理包络。

当前输出的详细结果见：

```bash
cat _cad-output/tech1-ocp-r1/tech1-a-ocp-final-validation.json
```

## 尚未进入量产深化

这仍是外观/结构草模，不是量产结构图。尚未定义或验证：

- 壁厚、拔模角、公差链。
- 螺丝柱、卡扣、定位筋、加强筋。
- 真实 PCB/OLED/摄像头/USB/扬声器的实测装配。
- 线束固定、胶粘/泡棉/硅胶垫材料细节。
- 防尘防水、EMI、热设计、跌落和寿命测试。
- CNC/压铸/注塑拆件方式和 DFM 成本验证。
