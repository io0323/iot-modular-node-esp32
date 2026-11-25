#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/types.hpp"

namespace node::domain {

/// @brief 計測チャネルの仕様（state の channels[] の1要素）。period_ms は通常のモードの周期。
struct ChannelSpec {
  std::string_view channel;  ///< 例："temp_air"
  Unit unit{Unit::DegC};
  std::uint32_t period_ms{1000};
};

/// @brief アクチュエーターの仕様（state の actuators[] の1要素）。安全制限はインターロック（IInterlock）が持つ。
struct ActuatorSpec {
  std::string_view id;  ///< 例："fan_main"
  Level min{0};
  Level max{kLevelMax};
  /// 連続出力の上限秒（state の max_on_s。上限がなければ std::nullopt で、state では null）。
  /// lease_s がこれを超える指令は、受けた時点で limited にする（共通仕様 v0.2.1 #27）
  std::optional<std::uint16_t> max_on_s{};  // NOLINT(readability-redundant-member-init)：構成表の集成体初期化で省いたときに -Wmissing-field-initializers を出さない
  /// 最小オン時間・最小オフ時間（ms）。この間は 0 と 0 より大きい値の切り替えを待たせる（P3 の決定 #47、#59）。
  /// インターロックの強制値・上限はこれより優先する。既定はどの機器もなし
  std::optional<std::uint32_t> min_on_ms{};  // NOLINT(readability-redundant-member-init)：構成表の集成体初期化で省いたときに -Wmissing-field-initializers を出さない
  std::optional<std::uint32_t> min_off_ms{};  // NOLINT(readability-redundant-member-init)：構成表の集成体初期化で省いたときに -Wmissing-field-initializers を出さない
};

/// @brief 計測値。値は 1/1000 単位の固定小数で持つ。
struct Reading {
  std::string_view channel;
  std::int32_t value_milli{0};
  Unit unit{Unit::DegC};
  Quality quality{Quality::Good};
};

}  // namespace node::domain
