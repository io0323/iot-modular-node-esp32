#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/types.hpp"

namespace node::domain {

/// @brief 入る条件の向き。出る条件は逆向き（Below で入るなら出るのは exit 以上、AtLeast で入るなら exit 未満）。
enum class Comparison : std::uint8_t {
  Below,    ///< 値 < しきい値
  AtLeast,  ///< 値 >= しきい値
};

/// @brief ローカルルール（基本設計書 FR-05、P2 の決定 #29）。値は構成表に置き、ビルド時に固定する。
///
/// pulse_ms が 0 なら「入る条件で level を出し続け、出る条件で止める」（ヒステリシス）。
/// pulse_ms が正なら「入る条件で level を pulse_ms だけ出す」（クールダウンはパルスの開始から数える）。
struct Rule {
  std::string_view id;              ///< 例："light-on-dark"
  std::string_view channel;         ///< 入力
  Comparison enter{Comparison::Below};  ///< 入る条件の比べ方
  std::int32_t enter_milli{0};      ///< 入る条件のしきい値（1/1000 単位）
  std::optional<std::int32_t> exit_milli;  ///< 出る条件のしきい値（パルスのルールにはない）
  std::string_view actuator;        ///< 対象
  Level level{0};                   ///< 出す値
  std::uint32_t pulse_ms{0};        ///< パルスの長さ（0 ならヒステリシスのルール）
  std::uint32_t cooldown_ms{0};     ///< パルスの開始からのクールダウン
  bool lighting{false};             ///< 照明系の印（省電力モードで止める）
};

/// @brief 入る条件が成り立つか。
constexpr bool enters(const Rule& r, std::int32_t v) {
  return r.enter == Comparison::Below ? v < r.enter_milli : v >= r.enter_milli;
}

/// @brief 出る条件が成り立つか（出る条件のないルールは常に false）。
constexpr bool exits(const Rule& r, std::int32_t v) {
  if (!r.exit_milli) {
    return false;
  }
  return r.enter == Comparison::Below ? v >= *r.exit_milli : v < *r.exit_milli;
}

}  // namespace node::domain
