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
