#include "config/hw.hpp"

namespace node::config {

HwRig::HwRig(const HwInterfaces& hw)
    :  // 構成表（config/hw_table.hpp）の行の順に作る
#define NODE_SENSOR(type, name, args, period_ms, channels) \
  name##_(NODE_CONFIG_UNPAREN args, name##_channels_, std::chrono::milliseconds{period_ms}),
#define NODE_ACTUATOR(type, name, args, spec, interlocks) name##_(NODE_CONFIG_UNPAREN args, name##_spec_),
#include "config/hw_table.hpp"
#undef NODE_SENSOR
#undef NODE_ACTUATOR
      display_(i2c(hw, kAddrSsd1306)),
      mode_button_(hw.gpio, kGpioButtonMode),
      action_button_(hw.gpio, kGpioButtonAction),
#define NODE_SENSOR(type, name, args, period_ms, channels) &name##_,
#define NODE_ACTUATOR(type, name, args, spec, interlocks)
      sensors_{
#include "config/hw_table.hpp"
      },
#undef NODE_SENSOR
#undef NODE_ACTUATOR
#define NODE_SENSOR(type, name, args, period_ms, channels)
#define NODE_ACTUATOR(type, name, args, spec, interlocks) domain::ActuatorBinding{&name##_, interlocks},
      actuators_{{
#include "config/hw_table.hpp"
      }} {
}
#undef NODE_SENSOR
#undef NODE_ACTUATOR

}  // namespace node::config
