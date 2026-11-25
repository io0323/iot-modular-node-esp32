#pragma once

#include <cstdint>
#include <optional>

namespace node::domain {

/// @brief 上位指令の有効期限（共通仕様 第4章「リース」、v0.2.1 #4・#13・#15）。
///
/// 起点はデバイスの受信時刻（単調時計の ms）で、「起点＋lease_s×1000」ちょうどで期限切れになる。
class Lease {
 public:
  static constexpr std::uint32_t kDefaultS = 60;  ///< lease_s を省いたときの秒数
  static constexpr std::uint32_t kMaxS = 600;     ///< lease_s の上限

  /// @brief cmd の lease_s から実際の秒数を決める。
  /// @param lease_s cmd の lease_s（省略なら std::nullopt）
  /// @return 省略なら60。1〜600 の外なら std::nullopt（invalid_arg）
  static std::optional<std::uint32_t> resolve(std::optional<std::int64_t> lease_s) {
    if (!lease_s) {
      return kDefaultS;
    }
    if (*lease_s < 1 || *lease_s > static_cast<std::int64_t>(kMaxS)) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(*lease_s);
  }

  Lease() = default;
  /// @brief リースを作る。
  /// @param start_ms 受信時刻（単調時計の ms）
  /// @param lease_s 秒数（resolve で決めた値）
  Lease(std::uint64_t start_ms, std::uint32_t lease_s) : start_ms_(start_ms), lease_s_(lease_s) {}

  /// @brief 期限切れの時刻（単調時計の ms）。
  /// @return 単調時計の ms
  [[nodiscard]] std::uint64_t deadline_ms() const {
    return start_ms_ + static_cast<std::uint64_t>(lease_s_) * 1000U;
  }
  /// @brief 期限が切れたか（期限ちょうどで true）。
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 切れていれば true
  [[nodiscard]] bool expired(std::uint64_t now_ms) const { return now_ms >= deadline_ms(); }
  /// @brief リースの秒数。
  /// @return 秒数
  [[nodiscard]] std::uint32_t lease_s() const { return lease_s_; }
  /// @brief 受信時刻。
  /// @return 単調時計の ms
  [[nodiscard]] std::uint64_t start_ms() const { return start_ms_; }

 private:
  std::uint64_t start_ms_{0};
  std::uint32_t lease_s_{kDefaultS};
};

}  // namespace node::domain
