# 预训练数据来源

目标视角：设备放在显示器上沿或桌面前方，画面主要是人的正面或前斜侧面，上半身占主导，任务是“桌前坐姿 / 离开”二分类，而不是通用椅子占用检测。

## 优先级

### A. 最接近当前视角

- WEyeDS
  - 官网：https://robbinslab.github.io/weyeds/
  - 适配原因：桌面摄像头数据，包含 full-face 图像，视角接近电脑前正面使用场景。
  - 限制：以 gaze estimation 为目标，主要是脸部，不一定总能覆盖手臂和桌面。
  - 获取方式：需要填写表单申请下载。

- MPIIFaceGaze
  - 官网：https://collaborative-ai.org/research/datasets/MPIIFaceGaze/
  - 适配原因：基于日常 laptop 使用采集，真实光照变化明显，和你的显示器前场景较接近。
  - 限制：背景被屏蔽，偏向脸部区域；更适合作为“有人在屏幕前”底模，不适合直接当完整坐姿数据。
  - 授权：页面注明仅限 non-commercial scientific purposes。

- GazeCapture
  - 官网：https://gazecapture.csail.mit.edu/dataset.php
  - 适配原因：大规模前置摄像头人脸/上半身使用场景，能提供正面设备视角的人体先验。
  - 限制：以手机/平板前摄为主，不完全等同于显示器上沿摄像头。
  - 获取方式：需要注册。

### B. 适合做通用人体/姿态底模

- COCO
  - 官网：https://cocodataset.org/
  - 适配原因：可提供通用 person detection / keypoints 先验，适合作为底层“有人体/上半身存在”的预训练来源。
  - 限制：视角分布太杂，不适合直接当你的最终分类数据。

- MPII Human Pose
  - 官网：https://www.mpi-inf.mpg.de/departments/computer-vision-and-machine-learning/software-and-datasets/mpii-human-pose-dataset
  - 适配原因：人体姿态覆盖广，可帮助学习坐姿、前斜侧面、上半身结构。
  - 限制：活动场景很多，桌前办公占比不高。

### C. 参考价值高，但不适合直接迁移

- SitPose
  - 论文：https://arxiv.org/abs/2412.12216
  - 适配原因：主题就是 sitting posture。
  - 限制：使用 Kinect depth camera，不是当前单目灰度/RGB 摄像头；更适合借鉴标签定义和评估方法，不适合作为直接预训练数据。

## 建议的数据策略

1. 先用 `WEyeDS + MPIIFaceGaze + GazeCapture` 作为近域底模。
2. 再用 `COCO + MPII` 补充通用人体和姿态先验。
3. 最后保留少量本机场景样本做部署校准，不需要为每个用户单独训练。

## 标签建议

- `seated`：人在桌前，正面或前斜侧面，头肩胸部明显可见，包含轻微低头、看手机、看屏幕、打字。
- `absent`：人离开、画面无人、只有桌面/背景、或人体离开到不足以支持“仍在桌前工作”的程度。

## 实施建议

- 第一阶段不要直接训练当前 `8x8` 小特征分类器。
- 先在 PC 侧用公开数据训练一个更强的教师模型，目标是 `seated / absent`。
- 再用教师模型在公开视频或公开图片上自动打伪标签，蒸馏到当前 ESP32-S3 可跑的小模型。
- 当前固件里的 `/label` 接口继续保留，用于后续少量现场校准。
