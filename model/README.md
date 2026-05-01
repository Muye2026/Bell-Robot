# 桌前坐姿模型数据

本目录存放训练样本、候选数据和模型生成脚本。固件运行时只使用生成后的 `embedded/firmware-idf/main/seat_model_data.h`。

## 数据目录

```text
model/dataset/
  absent/
  seated/
```

## 采集

1. 手机连接 `Bell-Robot`。
2. 打开 `http://192.168.4.1/`。
3. 用 `/label?class=absent` 下载离开/无人样本。
4. 用 `/label?class=seated` 下载桌前坐姿样本。
5. 将下载的 `8x8` 归一化 `.pgm` 放入对应目录。

建议每次摄像头角度变动后重新采集。第一轮至少覆盖白天/夜间、正面/侧面坐姿、深浅衣服、半离开和空桌椅。

## 训练

```powershell
cd D:\Project\Bell-Robot
python model\train_seat_model.py --dataset model\dataset --out embedded\firmware-idf\main\seat_model_data.h --balance-classes
```

如果只有手机预览截图，可先生成种子样本：

```powershell
python model\prepare_seed_dataset.py --out model\dataset <截图1> <截图2> ...
```

当前训练脚本的 ROI 已与固件 `buildModelFeatures()` 对齐，输入为画面中部偏上的 `8x8` 灰度特征。
