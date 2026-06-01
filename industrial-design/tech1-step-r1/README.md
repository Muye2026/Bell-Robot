# Bell Robot Tech1 A 风格结构草模 STEP R1

本目录用于生成 Tech1 A 风格的可编辑结构草模。模型以 `concept-r3/01-tech1-angular-a.png` 的棱角化外观为主，同时按 `tech1-structure-r1` 的结构约束建模。

## 当前保留的主线脚本

- `generate_tech1_step.py`：参数化生成 `FCStd/STEP` 主模型。
- `validate_tech1_step.py`：检查外包络、后支撑后伸和 STEP 导入。
- `render_tech1_views_vtk.py`：输出前后左右上下和多张等轴测渲染图。
- `compose_tech1_review_sheet.py`：把参考渲染图和当前模型拼成对比图。
- `run_tech1_pipeline.ps1`：一键串起生成、验证、渲染和拼图。

## 基准尺寸

- 主体外包络：`130 x 34 x 18mm`。
- 内腔参考：`124 x 31mm`。
- 后支撑臂后伸：`36mm`，落在结构文档建议的 `32-38mm` 范围内。
- 默认显示器厚度：`8-20mm`。
- 摄像头姿态：按 A 渲染图正面平齐处理，不额外设置镜头倾角。

## 结构内容

模型按草模拆成多个独立实体，方便后续 CAD 中单独编辑：

- `front_body_shell_faceted_130x34x18`：棱角化对称主壳。
- `left_oled_glass_window_29x16`：左侧 OLED 前窗。
- `center_camera_black_island_flush_38x17`：中部正面平齐摄像头黑色功能岛。
- `right_service_black_panel_balancer`：右侧服务面板，用于平衡左侧 OLED 的视觉重量。
- `rear_support_arm_36mm_extension`：背部挂屏支撑臂。
- `front_rubber_pad_small_support` / `rear_rubber_pad_soft_contact`：前后软垫接触点。
- `dev_board_keepout_57x28`、`speaker_or_buzzer_keepout_dia12_lower` 等：内部器件占位。

## 注意

这版是结构草模，不是量产装配图。它没有包含最终壁厚、公差、螺丝柱、卡扣、拔模角、防水防尘或真实线束固定方案。后续进入详细结构设计时，建议先实测开发板 USB 外壳、插头占用、摄像头模组厚度和蜂鸣器高度，再收紧 Z 向空间。

## 生成与验证

本次使用 FreeCAD `1.1.1`，安装位置：

```powershell
D:\Program Files\FreeCAD\FreeCAD_1.1.1-Windows-x86_64-py311\FreeCADCmd.exe
```

一键跑完整闭环：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_tech1_pipeline.ps1
```

如果要切换参考图：

```powershell
powershell -ExecutionPolicy Bypass -File .\run_tech1_pipeline.ps1 -ReferenceImage 'D:\Project\Bell-Robot\industrial-design\concept-r3\03-tech1-angular-c.png'
```

分步使用时：

重新生成：

```powershell
& 'D:\Program Files\FreeCAD\FreeCAD_1.1.1-Windows-x86_64-py311\FreeCADCmd.exe' -c "p=r'D:\Project\Bell-Robot\industrial-design\tech1-step-r1\generate_tech1_step.py'; exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), {'__file__': p, '__name__': '__main__'})"
```

重新验证：

```powershell
& 'D:\Program Files\FreeCAD\FreeCAD_1.1.1-Windows-x86_64-py311\FreeCADCmd.exe' -c "p=r'D:\Project\Bell-Robot\industrial-design\tech1-step-r1\validate_tech1_step.py'; exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), {'__file__': p, '__name__': '__main__'})"
```

重新渲染和拼图：

```powershell
& 'D:\Program Files\FreeCAD\FreeCAD_1.1.1-Windows-x86_64-py311\FreeCADCmd.exe' -c "p=r'D:\Project\Bell-Robot\industrial-design\tech1-step-r1\render_tech1_views_vtk.py'; exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), {'__file__': p, '__name__': '__main__'})"
& 'C:\Users\23171\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' D:\Project\Bell-Robot\industrial-design\tech1-step-r1\compose_tech1_review_sheet.py
```

当前验证结果：

- `.FCStd` 文件大小：`38440 bytes`。
- `.step` 文件大小：`233178 bytes`。
- 主体外包络：`130.00 x 18.00 x 34.00mm`。
- 后支撑臂后伸：`36.00mm`。
- STEP 可导入，导入后合理实体包络约 `131.00 x 56.60 x 36.40mm`。

## A 渲染图口径修订

当前模型按 `concept-r3/tech1-a-render-style-brief.md` 修订：摄像头按正面平齐建模，镜头轴线近似垂直前面板，不再使用早期结构文档里的镜头倾角假设。正面 OLED、摄像头岛和右服务区均改为独立的黑色斜角面板，以更接近 A 渲染图的正面外观。
