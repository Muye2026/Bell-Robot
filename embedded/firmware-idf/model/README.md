# 桌前坐姿模型数据集

目录约定：

```text
model/dataset/
  absent/
    absent_1.pgm
  seated/
    seated_1.pgm
```

采集方式：

1. 手机或电脑连接 `Bill-Camera`。
2. 人离开、画面无人或只有空桌椅时打开 `http://192.168.4.1/label?class=absent` 下载样本。
3. 人坐在办公桌前，正面、前斜侧面、轻微低头或转身时打开 `http://192.168.4.1/label?class=seated` 下载样本。
4. 把下载的 `8x8` 归一化 `.pgm` 放到对应目录。
5. 运行训练脚本生成 `main/seat_model_data.h`。

训练脚本同时兼容两种输入：

- 当前主线：`/label` 导出的 `8x8` 归一化 PGM。
- 兼容旧数据：整帧灰度 PGM，脚本会按当前 ROI 配置裁剪并做同样的归一化。

如果手头只有手机截图，而不是设备直接导出的 `/label` 样本，可先用：

```powershell
python model\prepare_seed_dataset.py --out model\dataset <截图1> <截图2> ...
```

这个脚本会自动从 `Bill Camera Preview` 手机截图里裁出真实预览画面，生成一批 `seated` 种子样本，并基于同视角背景插值合成一批 `absent` 种子样本。它只适合第一版冷启动，后续仍应尽快用设备直出的 `/label?class=absent|seated` 覆盖。

2026-05-01 的第一版非占位模型使用了两类正样本来源：

- 你提供的 3 张真实桌前坐姿截图。
- `MPIIGaze` 官方示例图里的上半身桌前样例块。

负样本来源：

- `prepare_seed_dataset.py` 从真实截图合成的同视角 `absent`。
- `Edinburgh office monitoring video dataset` 官方页面提供的空办公室样例帧。

建议第一轮至少采集：

- 离开/无人：白天 20 张、夜间 20 张。
- 桌前坐姿：正面 20 张、前斜侧面 20 张、深浅衣服各 20 张。
- 边界样本：半离开、弯腰、低头、身体只露一部分，各 10 张以上。
- 每次摄像头角度变动后重新采集。

2026-05-01 第二版种子模型：

- 使用用户确认的 Openverse 可商用候选方向扩充训练集。
- 正样本来源：`candidate_overview_openverse_selected_v13.jpg`，13 个桌前坐姿候选图块，每个生成 4 个灰度增强样本。
- 负样本来源：`candidate_overview_openverse_emptydesk_v14.jpg`，18 个空工位候选图块，每个生成 4 个灰度增强样本。
- 由于原始 Flickr/Openverse 图片下载触发 429 限流，本轮用已确认预览 sheet 裁切图块导入；后续能下载原图时应替换为原图导入。
- 当前训练集规模：`seated=115`，`absent=99`。
- 固件模型版本：`kSeatModelVersion = "1.1"`。

2026-05-01 第三版真实场景补充模型：

- 使用用户提供的真实办公场景截图补充训练集：一张无人空工位、一张有人桌前坐姿。
- 通过 `import_labeled_screenshots.py` 从浏览器截图中裁出摄像头预览区域，并对每类生成 32 个小幅平移、亮度、对比度、模糊和镜像增强样本。
- 当前训练集规模：`seated=147`，`absent=131`。
- 训练集内评估：`tp=111 tn=122 fp=9 fn=36`，继续优先控制无人误判为坐姿。
- 固件模型版本：`kSeatModelVersion = "1.2"`。

2026-05-01 第四版真实漏判补充模型：

- 保持固件模型版本 `kSeatModelVersion = "1.2"`，补入用户提供的 4 张真实工位坐下办公截图，专门修复“画面有人但不开始倒计时”的漏判场景。
- `import_labeled_screenshots.py` 支持一次导入多张 `--seated` 或 `--absent` 截图，便于连续补充真实误判样本。
- `train_seat_model.py` 新增 `--balance-classes`，本轮启用类别均衡训练，避免新增大量 seated 增强样本后空工位误判回升。
- 当前训练集规模：`seated=275`，`absent=131`。
- 训练集内评估：`tp=225 tn=127 fp=4 fn=50`；新增 4 张截图的增强样本 `128/128` 均超过 `0.65` 阈值，最低概率约 `0.852`。

2026-05-01 第五版 ROI 对齐修正：

- 保持固件模型版本 `kSeatModelVersion = "1.2"`。
- 修正 `train_seat_model.py` 的 ROI 参数，使训练特征与固件 `buildModelFeatures()` 使用同一块画面区域：`x=18%`、`y=10%`、`w=64%`、`h=72%`。
- 修正前训练偏向画面中下区域，固件实际偏向头部/肩部/上半身区域，可能造成离线评估正常但上板实时概率偏低。
- 训练集内评估：`tp=216 tn=129 fp=2 fn=59`；新增 4 张真实坐下截图增强样本 `128/128` 均超过 `0.65` 阈值，最低概率约 `0.837`。
