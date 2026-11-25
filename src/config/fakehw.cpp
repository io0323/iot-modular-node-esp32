#include "config/fakehw.hpp"

namespace node::config {

FakeHwRig::FakeHwRig(hal::ITimer& timer, hal::IMutex& i2c_mutex)
    :  // 構成表（config/fakehw_table.hpp）の行の順に作る
#define NODE_FAKE_I2C(type, name, address)
#define NODE_FAKE_I2C_ARGS(type, name, args, address) name##_ args,
#include "config/fakehw_table.hpp"
#undef NODE_FAKE_I2C
#undef NODE_FAKE_I2C_ARGS
      hw_(HwInterfaces{bus_, i2c_mutex, adc_, pwm_, gpio_, timer}) {
#define NODE_FAKE_I2C(type, name, address) (void)bus_.attach(address, name##_);
#define NODE_FAKE_I2C_ARGS(type, name, args, address) NODE_FAKE_I2C(type, name, address)
#include "config/fakehw_table.hpp"
#undef NODE_FAKE_I2C
#undef NODE_FAKE_I2C_ARGS
  // 模擬機器（P2）と同じ既定の環境：25℃前後、300 lx、土壌水分 40%
  bme280_device_.set_temperature_c(25.0);
  bh1750_device_.set_lux(300.0);
  adc_.set_raw(kAdcSoil, 2320);  // (3000 − 2320) / 1700 = 40%
  // ボタンは押していない（内部プルアップで High）
  gpio_.set_level(kGpioButtonMode, true);
  gpio_.set_level(kGpioButtonAction, true);
}

}  // namespace node::config
