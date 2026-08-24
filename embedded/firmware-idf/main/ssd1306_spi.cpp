#include "ssd1306_spi.h"

#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

namespace bell_robot {

namespace {
void writePin(int pin, int level) {
  gpio_set_level(static_cast<gpio_num_t>(pin), level);
}

void spiWriteByte(uint8_t value) {
  for (int bit = 7; bit >= 0; --bit) {
    writePin(PIN_OLED_CLK, 0);
    writePin(PIN_OLED_MOSI, (value >> bit) & 0x01);
    writePin(PIN_OLED_CLK, 1);
  }
}
} // namespace

bool Ssd1306Spi::begin() {
  return beginWithProfile(0xc8, 0x12);
}

bool Ssd1306Spi::beginWithProfile(uint8_t comScanDirection, uint8_t comPinsConfig) {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << PIN_OLED_MOSI) | (1ULL << PIN_OLED_CLK) |
                    (1ULL << PIN_OLED_DC) | (1ULL << PIN_OLED_CS) |
                    (1ULL << PIN_OLED_RESET);
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);

  reset();
  const uint8_t init[] = {
      0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40, 0x8d, 0x14,
      0x20, 0x00, 0xa1, comScanDirection, 0xda, comPinsConfig, 0x81, 0xcf, 0xd9, 0xf1,
      0xdb, 0x40, 0xa4, 0xa6, 0xaf,
  };
  for (uint8_t value : init) {
    command(value);
  }
  clear();
  flush();
  return true;
}

void Ssd1306Spi::clear() {
  memset(buffer_, 0, sizeof(buffer_));
}

void Ssd1306Spi::fill(bool on) {
  memset(buffer_, on ? 0xff : 0x00, sizeof(buffer_));
}

void Ssd1306Spi::setPixel(int x, int y, bool on) {
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
    return;
  }
  const uint16_t index = static_cast<uint16_t>((y / 8) * OLED_WIDTH + x);
  const uint8_t mask = static_cast<uint8_t>(1 << (y % 8));
  if (on) {
    buffer_[index] |= mask;
  } else {
    buffer_[index] &= static_cast<uint8_t>(~mask);
  }
}

void Ssd1306Spi::flush() {
  for (uint8_t page = 0; page < OLED_HEIGHT / 8; ++page) {
    command(static_cast<uint8_t>(0xb0 + page));
    command(0x00);
    command(0x10);
    data(&buffer_[page * OLED_WIDTH], OLED_WIDTH);
  }
}

void Ssd1306Spi::command(uint8_t value) {
  writePin(PIN_OLED_DC, 0);
  writePin(PIN_OLED_CS, 0);
  spiWriteByte(value);
  writePin(PIN_OLED_CS, 1);
}

void Ssd1306Spi::data(const uint8_t *values, int length) {
  writePin(PIN_OLED_DC, 1);
  writePin(PIN_OLED_CS, 0);
  for (int i = 0; i < length; ++i) {
    spiWriteByte(values[i]);
  }
  writePin(PIN_OLED_CS, 1);
}

void Ssd1306Spi::reset() {
  writePin(PIN_OLED_CS, 1);
  writePin(PIN_OLED_CLK, 0);
  writePin(PIN_OLED_MOSI, 0);
  writePin(PIN_OLED_DC, 0);
  writePin(PIN_OLED_RESET, 1);
  esp_rom_delay_us(20000);
  writePin(PIN_OLED_RESET, 0);
  esp_rom_delay_us(80000);
  writePin(PIN_OLED_RESET, 1);
  esp_rom_delay_us(150000);
}

} // namespace bell_robot
