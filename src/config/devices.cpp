// 構成表。機器を足すときは、新しいクラスと、ここに1行（＋その機器の仕様）を足す（NFR-01）。
// 仮版：P1 は模擬機器だけ。実機のドライバは P3 で足す。
#include "config/devices.hpp"

#include <array>
#include <chrono>
#include <cstdint>

#include "drivers/sim/sim_sensor.hpp"

namespace node::config {
namespace {

using namespace std::chrono_literals;
using domain::ActuatorSpec;
using domain::ChannelSpec;
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

// ---- アクチュエーター（ポンプは連続30秒で停止。土壌水分センサーの故障中は禁止）
constexpr ActuatorSpec kFanMain{"fan_main", 0, 1000, {}, {}};
constexpr ActuatorSpec kLedGrow{"led_grow", 0, 1000, {}, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}, "soil"};
constexpr ActuatorSpec kBuzzer{"buzzer", 0, 1000, {}, {}};

SimSensor g_bme280{"bme280", kBme280Channels, kBme280Values, 2000ms};
SimSensor g_bh1750{"bh1750", kBh1750Channels, kBh1750Values, 1000ms};
SimSensor g_soil{"soil_moisture", kSoilChannels, kSoilValues, 10000ms};

SimActuator g_fan_main{kFanMain};
SimActuator g_led_grow{kLedGrow};
SimActuator g_pump{kPump};
SimActuator g_buzzer{kBuzzer};

// ---- 構成表（1行が1台）
std::array<domain::ISensor*, 3> g_sensors{&g_bme280, &g_bh1750, &g_soil};
std::array<domain::IActuator*, 4> g_actuators{&g_fan_main, &g_led_grow, &g_pump, &g_buzzer};

}  // namespace

domain::DeviceRegistry devices() { return domain::DeviceRegistry(g_sensors, g_actuators); }

}  // namespace node::config
