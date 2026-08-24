#include "led_matrix_is31fl3733.h"

#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

namespace bell_robot {

namespace {
constexpr char kTag[] = "led_matrix";

// IS31FL3733 寄存器。
constexpr uint8_t kRegCommand = 0xfd;      // 页选择
constexpr uint8_t kRegCommandLock = 0xfe;  // 页选择写锁
constexpr uint8_t kCommandUnlock = 0xc5;   // 解锁后只允许写一次 0xfd

constexpr uint8_t kPageOnOff = 0x00;   // 0x00-0x17，每点 1 bit 的使能矩阵
constexpr uint8_t kPagePwm = 0x01;     // 0x00-0xbf，每点 8 位 PWM
constexpr uint8_t kPageFunction = 0x03;

constexpr uint8_t kFuncConfiguration = 0x00; // bit0 SSD：0=关断，1=正常
constexpr uint8_t kFuncGlobalCurrent = 0x01;

constexpr uint8_t kConfigNormalOperation = 0x01;

constexpr int kI2cTimeoutMs = 50;
constexpr int kOnOffBytes = kIs31fl3733SwCount * 2;

i2c_master_bus_handle_t gBus = nullptr;
i2c_master_dev_handle_t gDevices[kMatrixChipCount] = {};

static_assert(sizeof(MATRIX_I2C_ADDRESSES) / sizeof(MATRIX_I2C_ADDRESSES[0]) >= kMatrixChipCount,
              "MATRIX_I2C_ADDRESSES 的地址数量少于 MATRIX_WIDTH x MATRIX_HEIGHT 所需的芯片数");

bool setupBus() {
  if (gBus != nullptr) {
    return true;
  }
  i2c_master_bus_config_t busConfig = {};
  busConfig.i2c_port = MATRIX_I2C_PORT;
  busConfig.sda_io_num = static_cast<gpio_num_t>(PIN_MATRIX_SDA);
  busConfig.scl_io_num = static_cast<gpio_num_t>(PIN_MATRIX_SCL);
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = true;

  const esp_err_t err = i2c_new_master_bus(&busConfig, &gBus);
  if (err != ESP_OK) {
    gBus = nullptr;
    ESP_LOGE(kTag, "i2c bus init failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool attachDevice(int chip) {
  if (gDevices[chip] != nullptr) {
    return true;
  }
  i2c_device_config_t deviceConfig = {};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = MATRIX_I2C_ADDRESSES[chip];
  deviceConfig.scl_speed_hz = MATRIX_I2C_HZ;

  const esp_err_t err = i2c_master_bus_add_device(gBus, &deviceConfig, &gDevices[chip]);
  if (err != ESP_OK) {
    gDevices[chip] = nullptr;
    ESP_LOGE(kTag,
             "i2c add device 0x%02x failed: %s",
             MATRIX_I2C_ADDRESSES[chip],
             esp_err_to_name(err));
    return false;
  }
  return true;
}
} // namespace

MatrixTarget matrixTarget(int x, int y) {
  const int index = y * MATRIX_WIDTH + x;
  const int chip = index / kIs31fl3733DotsPerChip;
  const int within = index % kIs31fl3733DotsPerChip;
  return MatrixTarget{chip, within / kIs31fl3733CsCount, within % kIs31fl3733CsCount};
}

bool LedMatrixIs31fl3733::writeRegister(int chip, uint8_t reg, uint8_t value) {
  const uint8_t payload[2] = {reg, value};
  return i2c_master_transmit(gDevices[chip], payload, sizeof(payload), kI2cTimeoutMs) == ESP_OK;
}

bool LedMatrixIs31fl3733::writeBlock(int chip, uint8_t reg, const uint8_t *values, int length) {
  // 一次事务写完整页，避免每字节一次 START/STOP：192 字节在 400kHz 下约 4ms。
  uint8_t payload[1 + kIs31fl3733DotsPerChip] = {};
  if (length > kIs31fl3733DotsPerChip) {
    return false;
  }
  payload[0] = reg;
  memcpy(&payload[1], values, static_cast<size_t>(length));
  return i2c_master_transmit(gDevices[chip], payload, static_cast<size_t>(length + 1), kI2cTimeoutMs) == ESP_OK;
}

bool LedMatrixIs31fl3733::selectPage(int chip, uint8_t page) {
  // 页寄存器有写锁，每次换页都要先解锁；解锁只对紧接着的一次写有效。
  if (!writeRegister(chip, kRegCommandLock, kCommandUnlock)) {
    return false;
  }
  return writeRegister(chip, kRegCommand, page);
}

bool LedMatrixIs31fl3733::initChip(int chip) {
  if (!selectPage(chip, kPageFunction)) {
    return false;
  }
  if (!writeRegister(chip, kFuncConfiguration, kConfigNormalOperation)) {
    return false;
  }
  if (!writeRegister(chip, kFuncGlobalCurrent, MATRIX_GLOBAL_CURRENT)) {
    return false;
  }

  // 使能矩阵全开，运行时只改 PWM。省掉每帧都要写两个页。
  if (!selectPage(chip, kPageOnOff)) {
    return false;
  }
  uint8_t onOff[kOnOffBytes];
  memset(onOff, 0xff, sizeof(onOff));
  if (!writeBlock(chip, 0x00, onOff, sizeof(onOff))) {
    return false;
  }

  if (!selectPage(chip, kPagePwm)) {
    return false;
  }
  uint8_t zero[kIs31fl3733DotsPerChip] = {};
  return writeBlock(chip, 0x00, zero, sizeof(zero));
}

bool LedMatrixIs31fl3733::begin() {
  ready_ = false;
  if (!setupBus()) {
    return false;
  }
  for (int chip = 0; chip < kMatrixChipCount; ++chip) {
    if (!attachDevice(chip) || !initChip(chip)) {
      ESP_LOGE(kTag, "chip %d init failed", chip);
      return false;
    }
  }
  ready_ = true;
  clear();
  flush();
  ESP_LOGI(kTag,
           "ready: %dx%d dots on %d chip(s)",
           MATRIX_WIDTH,
           MATRIX_HEIGHT,
           kMatrixChipCount);
  return true;
}

void LedMatrixIs31fl3733::clear() {
  memset(pwm_, 0, sizeof(pwm_));
  for (int chip = 0; chip < kMatrixChipCount; ++chip) {
    dirty_[chip] = true;
  }
}

void LedMatrixIs31fl3733::fill(bool on) {
  memset(pwm_, on ? onLevel_ : 0, sizeof(pwm_));
  for (int chip = 0; chip < kMatrixChipCount; ++chip) {
    dirty_[chip] = true;
  }
}

void LedMatrixIs31fl3733::setPixel(int x, int y, bool on) {
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) {
    return;
  }
  const MatrixTarget target = matrixTarget(x, y);
  if (target.chip < 0 || target.chip >= kMatrixChipCount) {
    return;
  }
  const int offset = target.sw * kIs31fl3733CsCount + target.cs;
  const uint8_t value = on ? onLevel_ : 0;
  if (pwm_[target.chip][offset] == value) {
    return;
  }
  pwm_[target.chip][offset] = value;
  dirty_[target.chip] = true;
}

void LedMatrixIs31fl3733::flush() {
  if (!ready_) {
    return;
  }
  for (int chip = 0; chip < kMatrixChipCount; ++chip) {
    if (!dirty_[chip]) {
      continue;
    }
    if (!selectPage(chip, kPagePwm) ||
        !writeBlock(chip, 0x00, pwm_[chip], kIs31fl3733DotsPerChip)) {
      ESP_LOGW(kTag, "chip %d flush failed", chip);
      continue;
    }
    dirty_[chip] = false;
  }
}

void LedMatrixIs31fl3733::setBrightness(uint8_t level) {
  // 全局电流控制整体亮度；onLevel_ 控制单点 PWM。两者相乘决定最终亮度，
  // 这里只动全局电流，保持已经画好的帧内容不变。
  onLevel_ = level == 0 ? 1 : level;
  if (!ready_) {
    return;
  }
  for (int chip = 0; chip < kMatrixChipCount; ++chip) {
    if (selectPage(chip, kPageFunction)) {
      writeRegister(chip, kFuncGlobalCurrent, level);
    }
  }
}

} // namespace bell_robot
