#pragma once

#include <cstdint>

#include "domain/interlock.hpp"
#include "domain/types.hpp"

namespace node::domain {

/// @brief 連続出力の上限によるインターロック（rule: max_on_s。基本設計書 第6章、共通仕様 v0.2.1 #27）。
///
/// 出力が max_on_s 秒続いたら出力を0にする。再開できるのは「要求がいったん0に戻り
/// （リース切れ・release・level 0）、かつ停止から60秒経った」後だけ。両方がそろった時点を「解除」とする（#17）。
class PumpInterlock final : public IInterlock {
 public:
  static constexpr std::uint32_t kRestartHoldMs = 60U * 1000U;

  /// @brief 遷移。
  enum class Transition : std::uint8_t { None, Activated, Released };

  /// @brief update の結果。
  struct Output {
    Level level{0};  ///< 出してよい出力
    Transition transition{Transition::None};
  };

  /// @brief インターロックを作る。
  /// @param max_on_s 連続出力の上限秒
  explicit PumpInterlock(std::uint32_t max_on_s) : max_on_ms_(max_on_s * 1000U) {}

  /// @brief 要求された出力から、出してよい出力を決める。
  /// @param requested 要求
  /// @param now_ms いまの時刻（単調時計の ms）
  Output update(Level requested, std::uint64_t now_ms);

  [[nodiscard]] std::string_view rule() const override { return "max_on_s"; }
  InterlockVerdict evaluate(const InterlockInput& in) override;
  [[nodiscard]] bool engaged() const override { return active_; }

  /// @brief インターロックが効いているか（停止中で、まだ解除していない）。
  [[nodiscard]] bool active() const { return active_; }
  [[nodiscard]] std::uint32_t max_on_s() const { return max_on_ms_ / 1000U; }

 private:
  std::uint32_t max_on_ms_;
  bool running_{false};
  std::uint64_t on_since_ms_{0};
  bool active_{false};
  std::uint64_t stopped_at_ms_{0};
  bool request_returned_to_zero_{false};
};

}  // namespace node::domain
