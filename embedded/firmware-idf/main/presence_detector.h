#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_camera.h"
#include "seat_model.h"

// 摄像头采集与桌前坐姿检测模块。
//
// 包含两部分：
// 1. 摄像头底层工具：帧锁、采集、JPEG 转换与释放（供 web / 采样 / 云端共用）。
// 2. PresenceDetector：模型优先的坐姿二分类，模型不可用时回退 ROI 灰度差分，
//    并做连续帧去抖（PRESENCE_ON_FRAMES / PRESENCE_OFF_FRAMES）。
//
// 实例 presenceDetector 与 seatModel 在 presence_detector.cpp 中定义，
// 其他模块通过 extern 声明直接使用。

namespace bell_robot {

constexpr uint32_t kFeatureCount = 64;
constexpr framesize_t kCameraFrameSize = FRAMESIZE_QVGA;
constexpr uint16_t kCameraFrameWidth = 320;
constexpr uint16_t kCameraFrameHeight = 240;
constexpr framesize_t kCameraCaptureFrameSize = FRAMESIZE_VGA;
constexpr uint16_t kCameraCaptureFrameWidth = 640;
constexpr uint16_t kCameraCaptureFrameHeight = 480;
constexpr uint8_t kCameraJpegQuality = 90;

struct CapturedCameraFrame {
  camera_fb_t *rawFrame = nullptr;
  camera_fb_t logicalFrame = {};
  uint8_t *ownedBuffer = nullptr;
  bool cameraLocked = false;
};

struct PresenceDiagnostics {
  bool present = false;
  bool calibrated = false;
  bool modelReady = false;
  bool rawPresent = false;
  uint8_t onFrames = 0;
  uint8_t offFrames = 0;
  float modelProbability = 0.0f;
  uint32_t inferenceMs = 0;
  const char *fallbackReason = "not_sampled";
  uint16_t score = 0;
  uint16_t baseline = 0;
  uint16_t diff = 0;
};

bool lockCamera(CapturedCameraFrame *capture);
void unlockCamera(CapturedCameraFrame *capture);
bool setCameraFrameSize(framesize_t frameSize);
bool captureCameraFrame(CapturedCameraFrame *capture, framesize_t frameSize = kCameraFrameSize);
void releaseCameraFrame(CapturedCameraFrame *capture);
bool captureFrameAsJpeg(CapturedCameraFrame *capture,
                        uint8_t **outJpgBuffer,
                        size_t *outJpgLength,
                        bool *outConverted,
                        uint8_t jpegQuality = kCameraJpegQuality,
                        framesize_t frameSize = kCameraCaptureFrameSize);
void releaseCapturedJpeg(CapturedCameraFrame *capture, uint8_t *jpgBuffer, bool converted);

class PresenceDetector {
public:
  bool begin();
  bool update(uint32_t nowMs);
  void recalibrate();
  PresenceDiagnostics diagnostics() const;
  void exportNormalizedFeatures(const camera_fb_t *frame, int8_t *features, size_t featureCount) const;

private:
  bool present_ = false;
  bool calibrated_ = false;
  bool modelReady_ = false;
  uint32_t lastSampleMs_ = 0;
  uint8_t onFrames_ = 0;
  uint8_t offFrames_ = 0;
  int32_t baseline_ = -1;
  uint32_t calibrationSum_ = 0;
  uint8_t calibrationFrames_ = 0;
  PresenceDiagnostics diagnostics_ = {};

  void updateDebouncedPresence(bool rawPresent);
  bool sampleCameraPresence();
  bool sampleRoiFallback(uint16_t roiScore);
  uint16_t calculateRoiScore(const camera_fb_t *frame) const;
  void buildModelFeatures(const camera_fb_t *frame, int8_t *features, size_t featureCount) const;
};

extern PresenceDetector presenceDetector;
extern SeatModel seatModel;

} // namespace bell_robot
