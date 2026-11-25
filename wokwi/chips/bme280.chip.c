// BME280 の I2C のレジスタだけを再現する Wokwi のカスタムチップ（P7 の決定 #121・#127）。
// Wokwi の部品の一覧に BME280 がないため。値は P3 のフェイク（src/drivers/fake/fake_devices.cpp）と同じ：
// 補正係数と生の値（UT 519888・UP 415148）は BMP280 データシート 3.12 節の計算例、湿度は試験用の値（#127）。
// レジスタの番地とリセット状態は BME280 データシート（BST-BME280-DS001）5.3 節 表18、補正係数の並びは 4.2.2 節 表16。
// 計測（ctrl_meas の forced mode）は書いた時点で終わったものとし、status は常に 0 を返す。
#include <stdint.h>
#include <string.h>

#include "wokwi-api.h"

#define ADDR 0x76  // SDO を GND（6.2 節）

typedef struct {
  uint8_t regs[256];
  uint8_t pointer;
  int first_write;  // 書き込みの最初のバイトはレジスタの番地
  uint8_t pending_reg;
  int have_reg;
} chip_t;

static void put16(chip_t* c, uint8_t a, uint16_t v) {
  c->regs[a] = (uint8_t)(v & 0xFF);
  c->regs[a + 1] = (uint8_t)(v >> 8);
}

static void reset_regs(chip_t* c) {
  c->regs[0xD0] = 0x60;  // chip_id
  c->regs[0xF2] = 0x00;
  c->regs[0xF3] = 0x00;
  c->regs[0xF4] = 0x00;
  c->regs[0xF5] = 0x00;
  const uint8_t data_reset[8] = {0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80, 0x00};
  memcpy(&c->regs[0xF7], data_reset, sizeof data_reset);
}

static void load_calibration(chip_t* c) {
  put16(c, 0x88, 27504);
  put16(c, 0x8A, 26435);
  put16(c, 0x8C, (uint16_t)-1000);
  put16(c, 0x8E, 36477);
  put16(c, 0x90, (uint16_t)-10685);
  put16(c, 0x92, 3024);
  put16(c, 0x94, 2855);
  put16(c, 0x96, 140);
  put16(c, 0x98, (uint16_t)-7);
  put16(c, 0x9A, 15500);
  put16(c, 0x9C, (uint16_t)-14600);
  put16(c, 0x9E, 6000);
  c->regs[0xA1] = 75;  // dig_H1
  put16(c, 0xE1, 362);  // dig_H2
  c->regs[0xE3] = 0;  // dig_H3
  const uint16_t h4 = 313;
  const uint16_t h5 = 50;
  c->regs[0xE4] = (uint8_t)((h4 >> 4) & 0xFF);
  c->regs[0xE5] = (uint8_t)(((h5 & 0x0F) << 4) | (h4 & 0x0F));
  c->regs[0xE6] = (uint8_t)((h5 >> 4) & 0xFF);
  c->regs[0xE7] = 30;  // dig_H6
}

static void put20(chip_t* c, uint8_t a, uint32_t v) {
  c->regs[a] = (uint8_t)((v >> 12) & 0xFF);
  c->regs[a + 1] = (uint8_t)((v >> 4) & 0xFF);
  c->regs[a + 2] = (uint8_t)((v & 0x0F) << 4);
}

static void complete_measurement(chip_t* c) {
  put20(c, 0xF7, 415148);  // adc_P
  put20(c, 0xFA, 519888);  // adc_T
  c->regs[0xFD] = (uint8_t)(30000 >> 8);  // adc_H
  c->regs[0xFE] = (uint8_t)(30000 & 0xFF);
  c->regs[0xF4] = (uint8_t)(c->regs[0xF4] & 0xFC);  // sleep に戻る（3.3.3 節）
}

static void write_reg(chip_t* c, uint8_t reg, uint8_t value) {
  if (reg == 0xE0) {  // reset（5.4.2 節）
    if (value == 0xB6) {
      reset_regs(c);
    }
    return;
  }
  c->regs[reg] = value;
  if (reg == 0xF4 && ((value & 0x03) == 0x01 || (value & 0x03) == 0x02)) {
    complete_measurement(c);
  }
}

static bool on_connect(void* user_data, uint32_t address, bool read) {
  (void)address;
  chip_t* c = user_data;
  if (!read) {
    c->first_write = 1;
    c->have_reg = 0;
  }
  return true;
}

static uint8_t on_read(void* user_data) {
  chip_t* c = user_data;
  const uint8_t v = c->pointer == 0xF3 ? 0x00 : c->regs[c->pointer];
  c->pointer = (uint8_t)(c->pointer + 1);
  return v;
}

static bool on_write(void* user_data, uint8_t data) {
  chip_t* c = user_data;
  // 6.2.1 節：番地と値の組を続けて書ける。最初の番地は読み出しの開始の番地にもなる
  if (c->first_write) {
    c->first_write = 0;
    c->pointer = data;
    c->pending_reg = data;
    c->have_reg = 1;
    return true;
  }
  if (c->have_reg) {
    write_reg(c, c->pending_reg, data);
    c->have_reg = 0;
  } else {
    c->pending_reg = data;
    c->have_reg = 1;
  }
  return true;
}

static void on_disconnect(void* user_data) { (void)user_data; }

void chip_init(void) {
  static chip_t chip;
  memset(&chip, 0, sizeof chip);
  reset_regs(&chip);
  load_calibration(&chip);
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
