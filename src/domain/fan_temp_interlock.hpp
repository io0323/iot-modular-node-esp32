#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/interlock.hpp"

namespace node::domain {

/// @brief 高温でファンを全開にするインターロック（rule: temp_high。P2 の決定 #21）。
///
/// 入力が on_milli 以上で出力を force に固定する。off_milli 未満が hold_ms 続いたら解除する
/// （off_milli 以上に戻ったら数え直す）。入力が good でない間は状態をそのまま持ち、数えはやり直す（#38）。
class FanTempInterlock final : public IInterlock {
 public:
  /// @brief 構成表で決める値（P2 の決定 #21）。
  struct Config {
    std::string_view channel;  ///< 入力（temp_air）
    std::int32_t on_milli;     ///< 効く値（45.0℃ → 45000）
    std::int32_t off_milli;    ///< 解除の判定に使う値（43.0℃ → 43000）
    std::uint32_t hold_ms;     ///< off_milli 未満が続く時間（10秒）
    Level force;               ///< 固定する値（1000）
  };

  /// @brief インターロックを作る。
  /// @param config 構成表の値
  explicit FanTempInterlock(const Config& config) : config_(config) {}

  [[nodiscard]] std::string_view rule() const override { return "temp_high"; }
  InterlockVerdict evaluate(const InterlockInput& in) override;
  [[nodiscard]] bool engaged() const override { return engaged_; }

 private:
  Config config_;
  bool engaged_{false};
  std::optional<std::uint64_t> below_since_ms_;
};

}  // namespace node::domain
