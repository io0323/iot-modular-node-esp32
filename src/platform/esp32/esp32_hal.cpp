#include "platform/esp32/esp32_hal.hpp"

#include <algorithm>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "sdkconfig.h"

namespace node::platform::esp32 {

using support::ErrorCode;
using support::make_error;
using support::Result;

TickType_t ticks_of(std::uint32_t ms) {
  const std::uint64_t t = static_cast<std::uint64_t>(ms) * static_cast<std::uint64_t>(configTICK_RATE_HZ) / 1000U;
  return static_cast<TickType_t>(t);
}

namespace {
Result<void> check(esp_err_t err, const char* what) {
  if (err == ESP_OK) {
    return support::ok();
  }
  return make_error(ErrorCode::IoError, what);
}

const config::esp32::PinMap* find_pin(std::span<const config::esp32::PinMap> pins, hal::Pin logical) {
  const auto it = std::ranges::find_if(pins, [logical](const config::esp32::PinMap& p) { return p.logical == logical; });
  return it == pins.end() ? nullptr : &*it;
}
}  // namespace

// ---------------- I2C

Result<void> EspI2cBus::init() {
  i2c_master_bus_config_t cfg{};
  cfg.i2c_port = 0;
  cfg.sda_io_num = static_cast<gpio_num_t>(sda_);
  cfg.scl_io_num = static_cast<gpio_num_t>(scl_);
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;  // ドキュメントの typical（#113）
  cfg.flags.enable_internal_pullup = 1;
  return check(i2c_new_master_bus(&cfg, &bus_), "i2c_new_master_bus");
}

i2c_master_dev_handle_t EspI2cBus::device(std::uint8_t addr) {
  const auto* it = std::ranges::find_if(devices_, [addr](const Device& d) { return d.addr == addr; });
  if (it != devices_.end()) {
    return it->handle;
  }
  if (bus_ == nullptr || devices_.full()) {
    return nullptr;
  }
  i2c_device_config_t dc{};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr;
  dc.scl_speed_hz = kSclSpeedHz;
  i2c_master_dev_handle_t h = nullptr;
  if (i2c_master_bus_add_device(bus_, &dc, &h) != ESP_OK) {
    return nullptr;
  }
  (void)devices_.push_back(Device{addr, h});
  return h;
}

Result<void> EspI2cBus::write_read(std::uint8_t addr, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx,
                                   std::uint32_t timeout_ms) {
  i2c_master_dev_handle_t h = device(addr);
  if (h == nullptr) {
    return make_error(ErrorCode::IoError, "i2c device");
  }
  const int t = static_cast<int>(timeout_ms);
  if (tx.empty()) {
    return check(i2c_master_receive(h, rx.data(), rx.size(), t), "i2c_master_receive");
  }
  if (rx.empty()) {
    return check(i2c_master_transmit(h, tx.data(), tx.size(), t), "i2c_master_transmit");
  }
  return check(i2c_master_transmit_receive(h, tx.data(), tx.size(), rx.data(), rx.size(), t),
               "i2c_master_transmit_receive");
}

Result<void> EspI2cBus::write(std::uint8_t addr, std::span<const std::uint8_t> tx, std::uint32_t timeout_ms) {
  return write_read(addr, tx, {}, timeout_ms);
}

void EspI2cBus::delete_bus() {
  for (const Device& d : devices_) {
    (void)i2c_master_bus_rm_device(d.handle);
  }
  devices_.clear();
  if (bus_ != nullptr) {
    (void)i2c_del_master_bus(bus_);
    bus_ = nullptr;
  }
}

Result<void> EspI2cBus::recover() {
  // ドライバを外し、SCL を GPIO（オープンドレイン）として15回クロックを出してから、ドライバを作り直す（#106）
  delete_bus();
  gpio_config_t io{};
  io.pin_bit_mask = (std::uint64_t{1} << static_cast<unsigned>(scl_)) | (std::uint64_t{1} << static_cast<unsigned>(sda_));
  io.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  (void)gpio_config(&io);
  const auto scl = static_cast<gpio_num_t>(scl_);
  (void)gpio_set_level(static_cast<gpio_num_t>(sda_), 1);
  for (std::uint32_t i = 0; i < kRecoveryClocks; ++i) {
    (void)gpio_set_level(scl, 0);
    esp_rom_delay_us(5);  // 100kHz 相当の半周期
    (void)gpio_set_level(scl, 1);
    esp_rom_delay_us(5);
  }
  (void)gpio_reset_pin(scl);
  (void)gpio_reset_pin(static_cast<gpio_num_t>(sda_));
  return init();
}

// ---------------- ADC

Result<void> EspAdc::init() {
#if CONFIG_NODE_TEST_NO_ADC
  // 試験用（QEMU）：ADC のドライバを呼ばない。esp_adc の起動時の自己校正が QEMU で止まるため（#129）
  return make_error(ErrorCode::IoError, "adc disabled (NODE_TEST_NO_ADC)");
#else
  adc_oneshot_unit_init_cfg_t uc{};
  uc.unit_id = ADC_UNIT_1;
  if (auto r = check(adc_oneshot_new_unit(&uc, &unit_), "adc_oneshot_new_unit"); !r) {
    return r;
  }
  for (const config::esp32::PinMap& p : pins_) {
    adc_unit_t unit{};
    adc_channel_t ch{};
    if (auto r = check(adc_oneshot_io_to_channel(p.gpio, &unit, &ch), "adc_oneshot_io_to_channel"); !r) {
      return r;
    }
    adc_oneshot_chan_cfg_t cc{};
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (auto r = check(adc_oneshot_config_channel(unit_, ch, &cc), "adc_oneshot_config_channel"); !r) {
      return r;
    }
  }
  return support::ok();
#endif
}

Result<std::uint16_t> EspAdc::read_raw(hal::Pin channel) {
#if CONFIG_NODE_TEST_NO_ADC
  (void)channel;
  return make_error(ErrorCode::IoError, "adc disabled (NODE_TEST_NO_ADC)");
#else
  const config::esp32::PinMap* p = find_pin(pins_, channel);
  if (p == nullptr || unit_ == nullptr) {
    return make_error(ErrorCode::InvalidArgument, "adc channel");
  }
  adc_unit_t unit{};
  adc_channel_t ch{};
  if (adc_oneshot_io_to_channel(p->gpio, &unit, &ch) != ESP_OK) {
    return make_error(ErrorCode::InvalidArgument, "adc channel");
  }
  int raw = 0;
  if (adc_oneshot_read(unit_, ch, &raw) != ESP_OK || raw < 0) {
    return make_error(ErrorCode::IoError, "adc_oneshot_read");
  }
  return static_cast<std::uint16_t>(raw);
#endif
}

// ---------------- PWM（LEDC）

Result<void> EspPwm::configure(hal::Pin channel, std::uint32_t freq_hz, std::uint8_t resolution_bits) {
  const config::esp32::PinMap* p = find_pin(pins_, channel);
  if (p == nullptr) {
    return make_error(ErrorCode::InvalidArgument, "pwm channel");
  }
  ledc_timer_config_t tc{};
  tc.speed_mode = LEDC_LOW_SPEED_MODE;
  tc.duty_resolution = static_cast<ledc_timer_bit_t>(resolution_bits);
  tc.timer_num = static_cast<ledc_timer_t>(channel);  // チャネルごとにタイマーを1つ（#113）
  tc.freq_hz = freq_hz;
  tc.clk_cfg = LEDC_AUTO_CLK;
  if (auto r = check(ledc_timer_config(&tc), "ledc_timer_config"); !r) {
    return r;
  }
  ledc_channel_config_t cc{};
  cc.gpio_num = p->gpio;
  cc.speed_mode = LEDC_LOW_SPEED_MODE;
  cc.channel = static_cast<ledc_channel_t>(channel);
  cc.intr_type = LEDC_INTR_DISABLE;
  cc.timer_sel = static_cast<ledc_timer_t>(channel);
  cc.duty = 0;
  cc.hpoint = 0;
  return check(ledc_channel_config(&cc), "ledc_channel_config");
}

Result<void> EspPwm::set_duty(hal::Pin channel, std::uint32_t duty) {
  const auto ch = static_cast<ledc_channel_t>(channel);
  if (auto r = check(ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty), "ledc_set_duty"); !r) {
    return r;
  }
  return check(ledc_update_duty(LEDC_LOW_SPEED_MODE, ch), "ledc_update_duty");
}

// ---------------- GPIO

int EspGpio::gpio_of(hal::Pin pin) const {
  const config::esp32::PinMap* p = find_pin(pins_, pin);
  return p == nullptr ? -1 : p->gpio;
}

Result<void> EspGpio::configure_input_pullup(hal::Pin pin) {
  const int g = gpio_of(pin);
  if (g < 0) {
    return make_error(ErrorCode::InvalidArgument, "gpio");
  }
  gpio_config_t io{};
  io.pin_bit_mask = std::uint64_t{1} << static_cast<unsigned>(g);
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  return check(gpio_config(&io), "gpio_config");
}

Result<void> EspGpio::configure_output(hal::Pin pin) {
  const int g = gpio_of(pin);
  if (g < 0) {
    return make_error(ErrorCode::InvalidArgument, "gpio");
  }
  gpio_config_t io{};
  io.pin_bit_mask = std::uint64_t{1} << static_cast<unsigned>(g);
  io.mode = GPIO_MODE_OUTPUT;
  return check(gpio_config(&io), "gpio_config");
}

Result<bool> EspGpio::read(hal::Pin pin) {
  const int g = gpio_of(pin);
  if (g < 0) {
    return make_error(ErrorCode::InvalidArgument, "gpio");
  }
  return gpio_get_level(static_cast<gpio_num_t>(g)) != 0;
}

Result<void> EspGpio::write(hal::Pin pin, bool high) {
  const int g = gpio_of(pin);
  if (g < 0) {
    return make_error(ErrorCode::InvalidArgument, "gpio");
  }
  return check(gpio_set_level(static_cast<gpio_num_t>(g), high ? 1U : 0U), "gpio_set_level");
}

// ---------------- ミューテックス・タイマー

bool EspMutex::try_lock_for(std::uint32_t timeout_ms) { return rtos::mutex_take(handle_, ticks_of(timeout_ms)); }

void EspMutex::unlock() { rtos::mutex_give(handle_); }

std::uint64_t EspTimer::now_ms() const { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U; }

void EspTimer::delay_ms(std::uint32_t ms) { vTaskDelay(ticks_of(ms)); }

}  // namespace node::platform::esp32
