#include "config/fakehw.hpp"

namespace node::config {

FakeHwRig::FakeHwRig(hal::ITimer& timer, hal::IMutex& i2c_mutex)
    : bh1750_device_(timer), hw_(HwInterfaces{bus_, i2c_mutex, adc_, pwm_, gpio_, timer}) {
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
