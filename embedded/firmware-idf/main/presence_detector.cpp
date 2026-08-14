#include "presence_detector.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "app_config.h"
#include "app_state.h"
#include "esp_camera_af.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "bell_robot";
constexpr uint8_t kCameraSensorJpegQuality = 12;
const TickType_t kCameraMutexTimeoutTicks = pdMS_TO_TICKS(1500);
constexpr uint32_t kCameraAfTimeoutMs = 3000;

SemaphoreHandle_t cameraMutex = nullptr;
bool cameraAutofocusActive = false;

size_t cameraBytesPerPixel(pixformat_t format) {
  switch (format) {
  case PIXFORMAT_GRAYSCALE:
    return 1;
  case PIXFORMAT_RGB565:
    return 2;
  default:
    return 0;
  }
}

void configureCameraSensor(sensor_t *sensor) {
  if (sensor == nullptr) {
    return;
  }
  sensor->set_vflip(sensor, CAMERA_VFLIP ? 1 : 0);
  sensor->set_hmirror(sensor, CAMERA_HMIRROR ? 1 : 0);
  sensor->set_framesize(sensor, kCameraFrameSize);
  sensor->set_whitebal(sensor, 1);
  sensor->set_gain_ctrl(sensor, 1);
  sensor->set_exposure_ctrl(sensor, 1);
  sensor->set_brightness(sensor, 0);
  sensor->set_saturation(sensor, 1);
  sensor->set_contrast(sensor, 1);
  sensor->set_sharpness(sensor, 2);
  sensor->set_denoise(sensor, 1);
}

void configureCameraAutofocus(sensor_t *sensor) {
  if (sensor == nullptr) {
    return;
  }
  if (!esp_camera_af_is_supported(sensor)) {
    ESP_LOGW(kTag, "camera autofocus not supported by current sensor/driver config");
    return;
  }

  const esp_camera_af_config_t afConfig = {
      .mode = ESP_CAMERA_AF_MODE_AUTO,
      .step_size = 0,
      .range_min = 0,
      .range_max = 0,
      .timeout_ms = kCameraAfTimeoutMs,
  };
  esp_err_t err = esp_camera_af_init(sensor, &afConfig);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "camera autofocus init failed: %s", esp_err_to_name(err));
    return;
  }

  esp_camera_af_status_t status = {};
  err = esp_camera_af_wait(sensor, kCameraAfTimeoutMs, &status);
  if (err == ESP_OK) {
    ESP_LOGI(kTag,
             "camera autofocus ready: raw=%u focused=%c busy=%c",
             static_cast<unsigned>(status.raw),
             status.focused ? 'Y' : 'N',
             status.busy ? 'Y' : 'N');
  } else {
    ESP_LOGW(kTag, "camera autofocus wait failed: %s", esp_err_to_name(err));
  }
}

void cameraAutofocusTask(void *arg) {
  (void)arg;
  cameraAutofocusActive = true;
  ESP_LOGI(kTag, "camera autofocus task start at %lu ms",
           static_cast<unsigned long>(millis32()));
  if (cameraMutex != nullptr &&
      xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(kCameraAfTimeoutMs + 500)) == pdTRUE) {
    configureCameraAutofocus(esp_camera_sensor_get());
    xSemaphoreGive(cameraMutex);
  } else {
    ESP_LOGW(kTag, "camera autofocus task skipped: camera busy");
  }
  cameraAutofocusActive = false;
  ESP_LOGI(kTag, "camera autofocus task done at %lu ms",
           static_cast<unsigned long>(millis32()));
  vTaskDelete(nullptr);
}

uint8_t frameLumaAt(const camera_fb_t *frame, size_t x, size_t y) {
  if (frame == nullptr || frame->buf == nullptr || x >= frame->width || y >= frame->height) {
    return 0;
  }

  const size_t pixelIndex = y * frame->width + x;
  if (frame->format == PIXFORMAT_GRAYSCALE) {
    return pixelIndex < frame->len ? frame->buf[pixelIndex] : 0;
  }

  if (frame->format == PIXFORMAT_RGB565) {
    const size_t byteIndex = pixelIndex * 2;
    if (byteIndex + 1 >= frame->len) {
      return 0;
    }
    const uint8_t high = frame->buf[byteIndex];
    const uint8_t low = frame->buf[byteIndex + 1];
    const uint8_t r = high & 0xf8;
    const uint8_t g = static_cast<uint8_t>(((high & 0x07) << 5) | ((low & 0xe0) >> 3));
    const uint8_t b = static_cast<uint8_t>((low & 0x1f) << 3);
    return static_cast<uint8_t>((static_cast<uint16_t>(r) * 30 +
                                 static_cast<uint16_t>(g) * 59 +
                                 static_cast<uint16_t>(b) * 11) /
                                100);
  }

  return 0;
}
} // namespace

bool lockCamera(CapturedCameraFrame *capture) {
  if (capture == nullptr) {
    return false;
  }
  if (cameraMutex == nullptr) {
    capture->cameraLocked = true;
    return true;
  }
  if (xSemaphoreTake(cameraMutex, kCameraMutexTimeoutTicks) != pdTRUE) {
    return false;
  }
  capture->cameraLocked = true;
  return true;
}

void unlockCamera(CapturedCameraFrame *capture) {
  if (capture == nullptr || !capture->cameraLocked) {
    return;
  }
  capture->cameraLocked = false;
  if (cameraMutex != nullptr) {
    xSemaphoreGive(cameraMutex);
  }
}

bool setCameraFrameSize(framesize_t frameSize) {
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor == nullptr || sensor->set_framesize == nullptr) {
    return false;
  }
  if (sensor->status.framesize == frameSize) {
    return true;
  }
  if (sensor->set_framesize(sensor, frameSize) != 0) {
    ESP_LOGW(kTag, "camera set framesize failed: %d", static_cast<int>(frameSize));
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(80));
  return true;
}

bool captureCameraFrame(CapturedCameraFrame *capture, framesize_t frameSize) {
  if (capture == nullptr) {
    return false;
  }

  releaseCameraFrame(capture);
  if (!lockCamera(capture)) {
    ESP_LOGW(kTag, "camera lock timeout");
    return false;
  }

  if (!setCameraFrameSize(frameSize)) {
    releaseCameraFrame(capture);
    return false;
  }

  camera_fb_t *rawFrame = esp_camera_fb_get();
  if (rawFrame == nullptr) {
    if (frameSize != kCameraFrameSize) {
      setCameraFrameSize(kCameraFrameSize);
    }
    releaseCameraFrame(capture);
    return false;
  }

  capture->rawFrame = rawFrame;
  capture->logicalFrame = *rawFrame;
  if (frameSize != kCameraFrameSize) {
    setCameraFrameSize(kCameraFrameSize);
  }

  if (rawFrame->format == PIXFORMAT_JPEG) {
    const size_t decodedLength = static_cast<size_t>(rawFrame->width) *
                                 static_cast<size_t>(rawFrame->height) *
                                 cameraBytesPerPixel(PIXFORMAT_RGB565);
    uint8_t *decoded = static_cast<uint8_t *>(heap_caps_malloc(decodedLength, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decoded == nullptr) {
      decoded = static_cast<uint8_t *>(heap_caps_malloc(decodedLength, MALLOC_CAP_8BIT));
    }
    if (decoded == nullptr) {
      ESP_LOGE(kTag, "camera JPEG decode buffer allocation failed: %u bytes",
               static_cast<unsigned>(decodedLength));
      releaseCameraFrame(capture);
      return false;
    }

    if (!jpg2rgb565(rawFrame->buf, rawFrame->len, decoded, JPG_SCALE_NONE)) {
      ESP_LOGE(kTag, "camera JPEG decode failed");
      free(decoded);
      releaseCameraFrame(capture);
      return false;
    }

    capture->ownedBuffer = decoded;
    capture->logicalFrame.buf = decoded;
    capture->logicalFrame.len = decodedLength;
    capture->logicalFrame.width = rawFrame->width;
    capture->logicalFrame.height = rawFrame->height;
    capture->logicalFrame.format = PIXFORMAT_RGB565;
  }

  if (!CAMERA_ROTATE_CW_90) {
    return true;
  }

  const camera_fb_t sourceFrame = capture->logicalFrame;
  const size_t bytesPerPixel = cameraBytesPerPixel(sourceFrame.format);
  if (bytesPerPixel == 0 || sourceFrame.width == 0 || sourceFrame.height == 0) {
    ESP_LOGE(kTag, "camera rotate requires supported frame format with dimensions: format=%d",
             static_cast<int>(sourceFrame.format));
    releaseCameraFrame(capture);
    return false;
  }

  const size_t sourceWidth = sourceFrame.width;
  const size_t sourceHeight = sourceFrame.height;
  const size_t pixelCount = sourceWidth * sourceHeight;
  const size_t dataLength = pixelCount * bytesPerPixel;
  if (sourceFrame.len < dataLength) {
    ESP_LOGE(kTag, "camera frame too small for rotation: len=%u expected=%u",
             static_cast<unsigned>(sourceFrame.len),
             static_cast<unsigned>(dataLength));
    releaseCameraFrame(capture);
    return false;
  }

  uint8_t *rotated = static_cast<uint8_t *>(heap_caps_malloc(dataLength, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (rotated == nullptr) {
    rotated = static_cast<uint8_t *>(heap_caps_malloc(dataLength, MALLOC_CAP_8BIT));
  }
  if (rotated == nullptr) {
    ESP_LOGE(kTag, "camera rotate buffer allocation failed: %u bytes", static_cast<unsigned>(dataLength));
    releaseCameraFrame(capture);
    return false;
  }

  for (size_t y = 0; y < sourceHeight; ++y) {
    for (size_t x = 0; x < sourceWidth; ++x) {
      const size_t targetX = sourceHeight - 1 - y;
      const size_t targetY = x;
      const size_t sourceIndex = (y * sourceWidth + x) * bytesPerPixel;
      const size_t targetIndex = (targetY * sourceHeight + targetX) * bytesPerPixel;
      memcpy(rotated + targetIndex, sourceFrame.buf + sourceIndex, bytesPerPixel);
    }
  }

  uint8_t *previousOwnedBuffer = capture->ownedBuffer;
  capture->ownedBuffer = rotated;
  capture->logicalFrame.buf = rotated;
  capture->logicalFrame.len = dataLength;
  capture->logicalFrame.width = sourceHeight;
  capture->logicalFrame.height = sourceWidth;
  capture->logicalFrame.format = sourceFrame.format;
  if (previousOwnedBuffer != nullptr) {
    free(previousOwnedBuffer);
  }
  return true;
}

void releaseCameraFrame(CapturedCameraFrame *capture) {
  if (capture == nullptr) {
    return;
  }
  if (capture->ownedBuffer != nullptr) {
    free(capture->ownedBuffer);
  }
  if (capture->rawFrame != nullptr) {
    esp_camera_fb_return(capture->rawFrame);
  }
  unlockCamera(capture);
  capture->rawFrame = nullptr;
  capture->logicalFrame = {};
  capture->ownedBuffer = nullptr;
  capture->cameraLocked = false;
}

bool captureFrameAsJpeg(CapturedCameraFrame *capture,
                        uint8_t **outJpgBuffer,
                        size_t *outJpgLength,
                        bool *outConverted,
                        uint8_t jpegQuality,
                        framesize_t frameSize) {
  if (capture == nullptr || outJpgBuffer == nullptr || outJpgLength == nullptr || outConverted == nullptr) {
    return false;
  }

  if (!captureCameraFrame(capture, frameSize)) {
    return false;
  }
  camera_fb_t *frame = &capture->logicalFrame;

  uint8_t *jpgBuffer = nullptr;
  size_t jpgLength = 0;
  bool converted = false;
  if (frame->format == PIXFORMAT_JPEG) {
    jpgBuffer = frame->buf;
    jpgLength = frame->len;
  } else {
    converted = frame2jpg(frame, jpegQuality, &jpgBuffer, &jpgLength);
    if (!converted) {
      releaseCameraFrame(capture);
      return false;
    }
  }

  *outJpgBuffer = jpgBuffer;
  *outJpgLength = jpgLength;
  *outConverted = converted;
  return true;
}

void releaseCapturedJpeg(CapturedCameraFrame *capture, uint8_t *jpgBuffer, bool converted) {
  if (converted && jpgBuffer != nullptr) {
    free(jpgBuffer);
  }
  releaseCameraFrame(capture);
}

bool PresenceDetector::begin() {
  modelReady_ = seatModel.begin();
  if (cameraMutex == nullptr) {
    cameraMutex = xSemaphoreCreateMutex();
    if (cameraMutex == nullptr) {
      ESP_LOGE(kTag, "camera mutex allocation failed");
      return false;
    }
  }

  camera_config_t cameraConfig = {};
  cameraConfig.ledc_channel = LEDC_CHANNEL_0;
  cameraConfig.ledc_timer = LEDC_TIMER_0;
  cameraConfig.pin_d0 = PIN_CAM_D0;
  cameraConfig.pin_d1 = PIN_CAM_D1;
  cameraConfig.pin_d2 = PIN_CAM_D2;
  cameraConfig.pin_d3 = PIN_CAM_D3;
  cameraConfig.pin_d4 = PIN_CAM_D4;
  cameraConfig.pin_d5 = PIN_CAM_D5;
  cameraConfig.pin_d6 = PIN_CAM_D6;
  cameraConfig.pin_d7 = PIN_CAM_D7;
  cameraConfig.pin_xclk = PIN_CAM_XCLK;
  cameraConfig.pin_pclk = PIN_CAM_PCLK;
  cameraConfig.pin_vsync = PIN_CAM_VSYNC;
  cameraConfig.pin_href = PIN_CAM_HREF;
  cameraConfig.pin_sccb_sda = PIN_CAM_SIOD;
  cameraConfig.pin_sccb_scl = PIN_CAM_SIOC;
  cameraConfig.pin_pwdn = PIN_CAM_PWDN;
  cameraConfig.pin_reset = PIN_CAM_RESET;
  cameraConfig.xclk_freq_hz = 20000000;
  cameraConfig.frame_size = kCameraFrameSize;
  cameraConfig.pixel_format = PIXFORMAT_JPEG;
  cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
  cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
  cameraConfig.jpeg_quality = kCameraSensorJpegQuality;
  cameraConfig.fb_count = 3;

  const esp_err_t err = esp_camera_init(&cameraConfig);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "camera init failed: %s", esp_err_to_name(err));
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    configureCameraSensor(sensor);
    if (CAMERA_AUTOFOCUS_BLOCKING_STARTUP) {
      configureCameraAutofocus(sensor);
    } else {
      const BaseType_t created =
          xTaskCreate(cameraAutofocusTask, "camera_af", 4096, nullptr, 4, nullptr);
      if (created != pdPASS) {
        ESP_LOGW(kTag, "camera autofocus task create failed");
      }
    }
  }
  ESP_LOGI(kTag,
           "camera ready: %ux%u JPEG preview, capture=%ux%u%s",
           static_cast<unsigned>(kCameraFrameWidth),
           static_cast<unsigned>(kCameraFrameHeight),
           static_cast<unsigned>(kCameraCaptureFrameWidth),
           static_cast<unsigned>(kCameraCaptureFrameHeight),
           CAMERA_ROTATE_CW_90 ? " rotated cw90" : "");
  return true;
}

bool PresenceDetector::update(uint32_t nowMs) {
  if (nowMs - lastSampleMs_ < CAMERA_SAMPLE_INTERVAL_MS) {
    return diagnostics_.present;
  }
  lastSampleMs_ = nowMs;
  if (cameraAutofocusActive) {
    diagnostics_.fallbackReason = "autofocus";
    return diagnostics_.present;
  }

  const bool rawPresent = sampleCameraPresence();
  updateDebouncedPresence(rawPresent);
  diagnostics_.present = present_;
  diagnostics_.rawPresent = rawPresent;
  diagnostics_.onFrames = onFrames_;
  diagnostics_.offFrames = offFrames_;
  return present_;
}

void PresenceDetector::recalibrate() {
  present_ = false;
  calibrated_ = false;
  onFrames_ = 0;
  offFrames_ = 0;
  baseline_ = -1;
  calibrationSum_ = 0;
  calibrationFrames_ = 0;
  diagnostics_ = PresenceDiagnostics{};
}

PresenceDiagnostics PresenceDetector::diagnostics() const {
  PresenceDiagnostics value = diagnostics_;
  value.present = present_;
  value.calibrated = calibrated_;
  value.baseline = baseline_ < 0 ? 0 : static_cast<uint16_t>(baseline_);
  return value;
}

void PresenceDetector::exportNormalizedFeatures(const camera_fb_t *frame, int8_t *features, size_t featureCount) const {
  buildModelFeatures(frame, features, featureCount);
}

void PresenceDetector::updateDebouncedPresence(bool rawPresent) {
  diagnostics_.rawPresent = rawPresent;
  if (rawPresent) {
    onFrames_ = std::min<uint8_t>(onFrames_ + 1, PRESENCE_ON_FRAMES);
    offFrames_ = 0;
    if (onFrames_ >= PRESENCE_ON_FRAMES) {
      present_ = true;
    }
    return;
  }

  offFrames_ = std::min<uint8_t>(offFrames_ + 1, PRESENCE_OFF_FRAMES);
  onFrames_ = 0;
  if (offFrames_ >= PRESENCE_OFF_FRAMES) {
    present_ = false;
  }
}

bool PresenceDetector::sampleCameraPresence() {
  CapturedCameraFrame capture = {};
  if (!captureCameraFrame(&capture)) {
    diagnostics_.fallbackReason = "camera_frame_failed";
    return present_;
  }
  const camera_fb_t *frame = &capture.logicalFrame;

  int8_t features[kFeatureCount] = {};
  buildModelFeatures(frame, features, kFeatureCount);

  const uint16_t roiScore = calculateRoiScore(frame);
  releaseCameraFrame(&capture);
  diagnostics_.score = roiScore;

  const SeatModelResult modelResult = seatModel.infer(features, kFeatureCount);
  diagnostics_.modelReady = modelResult.ready;
  diagnostics_.modelProbability = modelResult.occupiedProbability;
  diagnostics_.inferenceMs = modelResult.inferenceMs;
  diagnostics_.fallbackReason = modelResult.fallbackReason;
  modelReady_ = modelResult.ready;

  if (modelReady_) {
    diagnostics_.diff = 0;
    return modelResult.occupiedProbability >= MODEL_OCCUPIED_THRESHOLD;
  }

  return sampleRoiFallback(roiScore);
}

bool PresenceDetector::sampleRoiFallback(uint16_t roiScore) {
  if (!calibrated_) {
    calibrationSum_ += roiScore;
    calibrationFrames_++;
    diagnostics_.diff = 0;
    if (calibrationFrames_ >= PRESENCE_CALIBRATION_FRAMES) {
      baseline_ = calibrationSum_ / calibrationFrames_;
      calibrated_ = true;
    }
    return false;
  }

  const uint16_t diff = abs(static_cast<int32_t>(roiScore) - baseline_);
  diagnostics_.diff = diff;
  return diff >= ROI_DIFF_THRESHOLD;
}

uint16_t PresenceDetector::calculateRoiScore(const camera_fb_t *frame) const {
  const size_t width = frame->width > 0 ? frame->width : 320;
  const size_t height = frame->height > 0 ? frame->height : 240;
  const size_t x0 = width * ROI_X_PERCENT / 100;
  const size_t y0 = height * ROI_Y_PERCENT / 100;
  const size_t x1 = x0 + width * ROI_W_PERCENT / 100;
  const size_t y1 = y0 + height * ROI_H_PERCENT / 100;

  uint32_t sum = 0;
  uint32_t count = 0;
  for (size_t y = y0; y < y1; y += 3) {
    for (size_t x = x0; x < x1; x += 3) {
      sum += frameLumaAt(frame, x, y);
      count++;
    }
  }
  return count == 0 ? 0 : static_cast<uint16_t>(sum / count);
}

void PresenceDetector::buildModelFeatures(const camera_fb_t *frame, int8_t *features, size_t featureCount) const {
  if (featureCount != kFeatureCount) {
    return;
  }

  uint8_t cells[kFeatureCount] = {};
  const size_t width = frame->width > 0 ? frame->width : 320;
  const size_t height = frame->height > 0 ? frame->height : 240;
  const size_t x0 = width * ROI_X_PERCENT / 100;
  const size_t y0 = height * ROI_Y_PERCENT / 100;
  const size_t roiW = width * ROI_W_PERCENT / 100;
  const size_t roiH = height * ROI_H_PERCENT / 100;
  uint32_t totalMean = 0;

  for (size_t gy = 0; gy < 8; ++gy) {
    for (size_t gx = 0; gx < 8; ++gx) {
      const size_t sx0 = x0 + gx * roiW / 8;
      const size_t sy0 = y0 + gy * roiH / 8;
      const size_t sx1 = x0 + (gx + 1) * roiW / 8;
      const size_t sy1 = y0 + (gy + 1) * roiH / 8;
      uint32_t sum = 0;
      uint32_t count = 0;
      for (size_t y = sy0; y < sy1; y += 2) {
        for (size_t x = sx0; x < sx1; x += 2) {
          sum += frameLumaAt(frame, x, y);
          count++;
        }
      }
      const uint8_t mean = count == 0 ? 0 : static_cast<uint8_t>(sum / count);
      cells[gy * 8 + gx] = mean;
      totalMean += mean;
    }
  }

  const int globalMean = static_cast<int>(totalMean / kFeatureCount);
  for (size_t i = 0; i < kFeatureCount; ++i) {
    int centered = (static_cast<int>(cells[i]) - globalMean) * 2;
    centered = std::max(-128, std::min(127, centered));
    features[i] = static_cast<int8_t>(centered);
  }
}

PresenceDetector presenceDetector;
SeatModel seatModel;

} // namespace bell_robot
