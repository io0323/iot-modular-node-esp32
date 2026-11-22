#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/types.hpp"

namespace node::domain {

/// @brief 計測チャネルの仕様（state の channels[] の1要素）。
struct ChannelSpec {
  std::string_view channel;  ///< 例："temp_air"
  Unit unit{Unit::DegC};
  std::uint32_t period_ms{1000};
};

/// @brief アクチュエーターの仕様（state の actuators[] の1要素と、デバイス内の安全制限）。
struct ActuatorSpec {
  std::string_view id;  ///< 例："fan_main"
  Level min{0};
  Level max{kLevelMax};
  /// 連続出力の上限秒。上限がなければ std::nullopt（state では null）
  std::optional<std::uint16_t> max_on_s{};
  /// このチャネルの quality が fault の間は set_actuator を rejected（unsafe）にする。空なら制限なし
  std::string_view inhibit_on_fault_channel{};
};

/// @brief 計測値。値は 1/1000 単位の固定小数で持つ。
struct Reading {
  std::string_view channel;
  std::int32_t value_milli{0};
  Unit unit{Unit::DegC};
  Quality quality{Quality::Good};
};

}  // namespace node::domain
