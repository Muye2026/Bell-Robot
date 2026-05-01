#include "ssd1306_spi.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

namespace {
constexpr uint8_t kFont5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5f, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7f, 0x14, 0x7f, 0x14}, // #
    {0x24, 0x2a, 0x7f, 0x2a, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1c, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1c, 0x00}, // )
    {0x14, 0x08, 0x3e, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3e, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, // 0
    {0x00, 0x42, 0x7f, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4b, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7f, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1e}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3e}, // @
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, // A
    {0x7f, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3e, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, // D
    {0x7f, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7f, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, // G
    {0x7f, 0x08, 0x08, 0x08, 0x7f}, // H
    {0x00, 0x41, 0x7f, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3f, 0x01}, // J
    {0x7f, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7f, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, // M
    {0x7f, 0x04, 0x08, 0x10, 0x7f}, // N
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, // O
    {0x7f, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, // Q
    {0x7f, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7f, 0x01, 0x01}, // T
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, // U
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, // V
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

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
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << PIN_OLED_MOSI) | (1ULL << PIN_OLED_CLK) |
                    (1ULL << PIN_OLED_DC) | (1ULL << PIN_OLED_CS) |
                    (1ULL << PIN_OLED_RESET);
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);

  reset();
  const uint8_t init[] = {
      0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40, 0x8d, 0x14,
      0x20, 0x00, 0xa1, 0xc8, 0xda, 0x12, 0x81, 0xcf, 0xd9, 0xf1,
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

void Ssd1306Spi::text(int col, int row, const char *value) {
  if (value == nullptr) {
    return;
  }
  int x = col * 6;
  int y = row * 8;
  for (const char *p = value; *p != '\0' && x < OLED_WIDTH - 5; ++p) {
    drawChar(x, y, *p);
    x += 6;
  }
}

void Ssd1306Spi::textf(int col, int row, const char *format, ...) {
  char line[32] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  text(col, row, line);
}

void Ssd1306Spi::textScaled(int x, int y, int scale, const char *value) {
  if (value == nullptr || scale <= 0) {
    return;
  }
  for (const char *p = value; *p != '\0' && x < OLED_WIDTH - (5 * scale); ++p) {
    drawChar(x, y, *p, scale);
    x += 6 * scale;
  }
}

void Ssd1306Spi::textScaledf(int x, int y, int scale, const char *format, ...) {
  char line[32] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  textScaled(x, y, scale, line);
}

void Ssd1306Spi::flush() {
  uint8_t physicalBuffer[OLED_PHYSICAL_WIDTH * OLED_PHYSICAL_HEIGHT / 8] = {};
  for (int y = 0; y < OLED_HEIGHT; ++y) {
    for (int x = 0; x < OLED_WIDTH; ++x) {
      if (!getPixel(x, y)) {
        continue;
      }
      if (OLED_ROTATE_CCW_90) {
        setPhysicalPixel(physicalBuffer, y, OLED_WIDTH - 1 - x);
      } else {
        setPhysicalPixel(physicalBuffer, x, y);
      }
    }
  }

  for (uint8_t page = 0; page < OLED_PHYSICAL_HEIGHT / 8; ++page) {
    command(0xb0 + page);
    command(0x00);
    command(0x10);
    data(&physicalBuffer[page * OLED_PHYSICAL_WIDTH], OLED_PHYSICAL_WIDTH);
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

void Ssd1306Spi::drawChar(int x, int y, char c, int scale) {
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  if (c < ' ' || c > 'Z') {
    c = '?';
  }
  if (scale <= 0) {
    return;
  }

  const uint8_t *glyph = kFont5x7[c - ' '];
  for (int col = 0; col < 5; ++col) {
    uint8_t bits = glyph[col];
    for (int row = 0; row < 7; ++row) {
      if ((bits & (1 << row)) == 0) {
        continue;
      }
      for (int dx = 0; dx < scale; ++dx) {
        for (int dy = 0; dy < scale; ++dy) {
          const int px = x + col * scale + dx;
          const int py = y + row * scale + dy;
          if (px < 0 || px >= OLED_WIDTH || py < 0 || py >= OLED_HEIGHT) {
            continue;
          }
          setPixel(px, py);
        }
      }
    }
  }
}

bool Ssd1306Spi::getPixel(int x, int y) const {
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
    return false;
  }
  return (buffer_[(y / 8) * OLED_WIDTH + x] & (1 << (y % 8))) != 0;
}

void Ssd1306Spi::setPixel(int x, int y) {
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
    return;
  }
  buffer_[(y / 8) * OLED_WIDTH + x] |= (1 << (y % 8));
}

void Ssd1306Spi::setPhysicalPixel(uint8_t *physicalBuffer, int x, int y) const {
  if (physicalBuffer == nullptr || x < 0 || x >= OLED_PHYSICAL_WIDTH || y < 0 || y >= OLED_PHYSICAL_HEIGHT) {
    return;
  }
  physicalBuffer[(y / 8) * OLED_PHYSICAL_WIDTH + x] |= (1 << (y % 8));
}
