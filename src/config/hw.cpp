#include "config/hw.hpp"

namespace node::config {

using namespace std::chrono_literals;

HwRig::HwRig(const HwInterfaces& hw)
    : bme280_(hal::I2cDevice{&hw.i2c, &hw.i2c_mutex, kAddrBme280}, hw.timer, kBmeChannels, 2000ms),
      bh1750_(hal::I2cDevice{&hw.i2c, &hw.i2c_mutex, kAddrBh1750}, hw.timer, kLuxChannel, 1000ms),
      soil_(hw.adc, kAdcSoil, kSoilCalibration, kSoilChannel, 10000ms),
      fan_main_(hw.pwm, kPwmFanConfig, kFanMain),
      led_grow_(hw.pwm, kPwmLedConfig, kLedGrow),
      pump_(hw.gpio, kGpioPumpRelay, kPumpRelayActiveHigh, kPump),
      buzzer_(hw.pwm, kPwmBuzzerConfig, kBuzzer),
      display_(hal::I2cDevice{&hw.i2c, &hw.i2c_mutex, kAddrSsd1306}),
      mode_button_(hw.gpio, kGpioButtonMode),
      action_button_(hw.gpio, kGpioButtonAction),
      sensors_{&bme280_, &bh1750_, &soil_},
      actuators_{{{&fan_main_, fan_interlocks_}, {&led_grow_, {}}, {&pump_, pump_interlocks_}, {&buzzer_, {}}}} {}

}  // namespace node::config
