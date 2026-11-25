// BH1750FVI の I2C の命令と読み出しだけを再現する Wokwi のカスタムチップ（P7 の決定 #121・#127）。
// Wokwi の部品の一覧に BH1750 がないため。値は P3 のフェイク（src/drivers/fake/fake_devices.cpp）と同じ既定の 300 lx。
// 命令（Power Down 0x00・Power On 0x01・Continuously H-Resolution Mode 0x10）と、計測値の2バイト（High・Low）、
// 1 lx = 1.2 カウント、H-Resolution の計測時間 120ms（typ.）は ROHM Technical Note（2011.11 Rev.D）による。
// 照度は属性 lux（既定 300）で変えられる。
#include <stdint.h>
#include <string.h>

#include "wokwi-api.h"

#define ADDR 0x23  // ADDR を GND
#define MEASURE_NS (120ULL * 1000ULL * 1000ULL)

typedef struct {
  uint32_t lux_attr;
  int continuous_h;
  uint64_t mode_set_ns;
  int byte_index;
} chip_t;

static uint16_t count_of(const chip_t* c) {
  if (!c->continuous_h || get_sim_nanos() - c->mode_set_ns < MEASURE_NS) {
    return 0;
  }
  const float counts = attr_read_float(c->lux_attr) * 1.2f + 0.5f;
  return counts >= 65535.0f ? 65535 : (counts <= 0.0f ? 0 : (uint16_t)counts);
}

static bool on_connect(void* user_data, uint32_t address, bool read) {
  (void)address;
  (void)read;
  chip_t* c = user_data;
  c->byte_index = 0;
  return true;
}

static uint8_t on_read(void* user_data) {
  chip_t* c = user_data;
  const uint16_t v = count_of(c);
  const uint8_t b = c->byte_index == 0 ? (uint8_t)(v >> 8) : (uint8_t)(v & 0xFF);
  c->byte_index++;
  return b;
}

static bool on_write(void* user_data, uint8_t op) {
  chip_t* c = user_data;
  if (op == 0x00) {
    c->continuous_h = 0;
  } else if (op == 0x10) {
    c->continuous_h = 1;
    c->mode_set_ns = get_sim_nanos();
  }
  return true;
}

static void on_disconnect(void* user_data) { (void)user_data; }

void chip_init(void) {
  static chip_t chip;
  memset(&chip, 0, sizeof chip);
  chip.lux_attr = attr_init_float("lux", 300.0f);
  const i2c_config_t config = {
      .user_data = &chip,
      .address = ADDR,
      .scl = pin_init("SCL", INPUT),
      .sda = pin_init("SDA", INPUT),
      .connect = on_connect,
      .read = on_read,
      .write = on_write,
      .disconnect = on_disconnect,
  };
  i2c_init(&config);
}
