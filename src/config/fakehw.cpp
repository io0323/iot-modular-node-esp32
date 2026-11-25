#include "config/fakehw.hpp"

namespace node::config {

using namespace std::chrono_literals;

FakeHwRig::FakeHwRig(hal::ITimer& timer, hal::IMutex& i2c_mutex)
    : bh1750_device_(timer),
      bme280_(hal::I2cDevice{&bus_, &i2c_mutex, kAddrBme280}, timer, kBmeChannels, 2000ms),
      bh1750_(hal::I2cDevice{&bus_, &i2c_mutex, kAddrBh1750}, timer, kLuxChannel, 1000ms),
      soil_(adc_, kAdcSoil, kSoilCalibration, kSoilChannel, 10000ms),
      fan_main_(pwm_, kPwmFanConfig, kFanMain),
      led_grow_(pwm_, kPwmLedConfig, kLedGrow),
      pump_(gpio_, kGpioPumpRelay, kPumpRelayActiveHigh, kPump),
      buzzer_(pwm_, kPwmBuzzerConfig, kBuzzer),
      display_(hal::I2cDevice{&bus_, &i2c_mutex, kAddrSsd1306}),
      mode_button_(gpio_, kGpioButtonMode),
      action_button_(gpio_, kGpioButtonAction),
      sensors_{&bme280_, &bh1750_, &soil_},
      actuators_{{{&fan_main_, fan_interlocks_}, {&led_grow_, {}}, {&pump_, pump_interlocks_}, {&buzzer_, {}}}} {
  (void)bus_.attach(kAddrBme280, bme280_device_);
  (void)bus_.attach(kAddrBh1750, bh1750_device_);
  (void)bus_.attach(kAddrSsd1306, ssd1306_device_);
  // 模擬機器（P2）と同じ既定の環境：25℃前後、300 lx、土壌水分 40%
  bme280_device_.set_temperature_c(25.0);
  bh1750_device_.set_lux(300.0);
  adc_.set_raw(kAdcSoil, 2320);  // (3000 − 2320) / 1700 = 40%
  // ボタンは押していない（内部プルアップで High）
  gpio_.set_level(kGpioButtonMode, true);
  gpio_.set_level(kGpioButtonAction, true);
}

}  // namespace node::config
