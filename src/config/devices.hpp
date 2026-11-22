#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "domain/fan_temp_interlock.hpp"
#include "domain/rule.hpp"
#include "domain/sensor_fault_interlock.hpp"
#include "drivers/sim/sim_sensor.hpp"

namespace node::config {

// ---- 安全インターロックの値（ビルド時に固定。open-questions #2）

/// ポンプの連続出力の上限（共通仕様 v0.2.1 #27）
inline constexpr std::uint32_t kPumpMaxOnS = 30;
/// ファンの高温インターロック（P2 の決定 #21）
inline constexpr domain::FanTempInterlock::Config kFanTempHigh{"temp_air", 45000, 43000, 10000, 1000};
/// 気温センサーが good でない間はファンを1000に固定（#22）
inline constexpr domain::SensorFaultInterlock::Config kFanOnTempFault{
    "temp_air", domain::SensorFaultInterlock::Action::Force, 1000, false};
/// 土壌水分センサーが good でない間はポンプの上限を0に（set_actuator は unsafe。#23）
inline constexpr domain::SensorFaultInterlock::Config kPumpOffOnSoilFault{
    "soil", domain::SensorFaultInterlock::Action::Upper, 0, true};

// ---- ローカルルール（P2 の決定 #29。1行が1つ）
inline constexpr std::array<domain::Rule, 3> kRules{{
    {"light-on-dark", "lux", domain::Comparison::Below, 200000, 250000, "led_grow", 1000, 0, 0, true},
    {"cool-on-high-temp", "temp_air", domain::Comparison::AtLeast, 30000, 28000, "fan_main", 600, 0, 0, false},
    {"water-on-dry", "soil", domain::Comparison::Below, 30000, std::nullopt, "pump", 1000, 5000, 600000, false},
}};

/// @brief 構成表の機器の一覧（profiles/mcu-cpp-01.yaml と同じ5チャネル・4アクチュエーター、インターロック、ルール）。
/// インスタンスは静的に確保してあり、一覧は参照だけを持つ。
domain::DeviceRegistry devices();

/// @brief 構成表の模擬センサーを ID で探す（ホスト版のデバッグ入力用）。
drivers::SimSensor* sim_sensor_by_id(std::string_view id);
/// @brief 構成表の模擬センサーをチャネル名で探す（ホスト版のデバッグ入力用）。
drivers::SimSensor* sim_sensor_by_channel(std::string_view channel);

}  // namespace node::config
