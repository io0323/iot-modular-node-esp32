// 構成表。機器・インターロック・ルールを足すときは、新しいクラスと、ここに1行（＋その仕様）を足す（NFR-01）。
// 仮版：P2 は模擬機器だけ。実機のドライバは P3 で足す。
#include "config/devices.hpp"

#include <algorithm>
#include <chrono>

#include "domain/pump_interlock.hpp"

namespace node::config {
namespace {

using namespace std::chrono_literals;
using domain::ActuatorBinding;
using domain::ActuatorSpec;
using domain::ChannelSpec;
using domain::IInterlock;
using domain::Unit;
using drivers::SimActuator;
using drivers::SimSensor;

// ---- センサー（計測周期は v0.2.1 #26）
constexpr std::array<ChannelSpec, 3> kBme280Channels{{
    {"temp_air", Unit::DegC, 2000},
    {"humidity", Unit::PctRh, 2000},
    {"pressure", Unit::HPa, 2000},
}};
constexpr std::array<std::int32_t, 3> kBme280Values{25000, 50000, 1013250};  // 25.0 degC、50.0 pct_rh、1013.25 hPa
constexpr std::array<ChannelSpec, 1> kBh1750Channels{{{"lux", Unit::Lux, 1000}}};
constexpr std::array<std::int32_t, 1> kBh1750Values{300000};  // 300 lux
constexpr std::array<ChannelSpec, 1> kSoilChannels{{{"soil", Unit::Pct, 10000}}};
constexpr std::array<std::int32_t, 1> kSoilValues{40000};  // 40.0 pct

// ---- アクチュエーター
constexpr ActuatorSpec kFanMain{"fan_main", 0, 1000, {}};
constexpr ActuatorSpec kLedGrow{"led_grow", 0, 1000, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{kPumpMaxOnS}};
constexpr ActuatorSpec kBuzzer{"buzzer", 0, 1000, {}};

/// 模擬機器の一式。実体は sim_rig() の中に1つだけ置く（名前空間に書き換えられる変数を置かない）。
struct SimRig {
  SimSensor bme280{"bme280", kBme280Channels, kBme280Values, 2000ms};
  SimSensor bh1750{"bh1750", kBh1750Channels, kBh1750Values, 1000ms};
  SimSensor soil{"soil_moisture", kSoilChannels, kSoilValues, 10000ms};

  SimActuator fan_main{kFanMain};
  SimActuator led_grow{kLedGrow};
  SimActuator pump{kPump};
  SimActuator buzzer{kBuzzer};

  // ---- インターロック（1行が1つ）
  domain::FanTempInterlock fan_temp_high{kFanTempHigh};
  domain::SensorFaultInterlock fan_on_temp_fault{kFanOnTempFault};
  domain::PumpInterlock pump_max_on{kPumpMaxOnS};
  domain::SensorFaultInterlock pump_off_on_soil_fault{kPumpOffOnSoilFault};

  std::array<IInterlock*, 2> fan_interlocks{&fan_temp_high, &fan_on_temp_fault};
  std::array<IInterlock*, 2> pump_interlocks{&pump_max_on, &pump_off_on_soil_fault};

  // ---- 構成表（1行が1台）
  std::array<domain::ISensor*, 3> sensors{&bme280, &bh1750, &soil};
  std::array<SimSensor*, 3> sim_sensors{&bme280, &bh1750, &soil};
  std::array<ActuatorBinding, 4> actuators{{
      {&fan_main, fan_interlocks},
      {&led_grow, {}},
      {&pump, pump_interlocks},
      {&buzzer, {}},
  }};
};

SimRig& sim_rig() {
  static SimRig rig;
  return rig;
}

}  // namespace

domain::DeviceRegistry devices() { return {sim_rig().sensors, sim_rig().actuators, kRules}; }

drivers::SimSensor* sim_sensor_by_id(std::string_view id) {
  const auto& all = sim_rig().sim_sensors;
  const auto* it = std::ranges::find_if(all, [id](const SimSensor* s) { return s->id() == id; });
  return it == all.end() ? nullptr : *it;
}

drivers::SimSensor* sim_sensor_by_channel(std::string_view channel) {
  const auto has_channel = [channel](const SimSensor* s) {
    return std::ranges::any_of(s->channels(), [channel](const ChannelSpec& ch) { return ch.channel == channel; });
  };
  const auto& all = sim_rig().sim_sensors;
  const auto* it = std::ranges::find_if(all, has_channel);
  return it == all.end() ? nullptr : *it;
}

}  // namespace node::config
