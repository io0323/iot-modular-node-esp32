#pragma once

#include <array>
#include <cstdint>

#include "infra/envelope.hpp"

namespace node::infra {

/// @brief トークンバケツ。平均 rate_per_s 件・瞬間 burst 件まで通す。
class TokenBucket {
 public:
  /// @brief バケツを作る（最初は満杯）。
  /// @param rate_per_s 1秒に補充する件数
  /// @param burst 容量
  TokenBucket(std::uint32_t rate_per_s, std::uint32_t burst)
      : rate_per_s_(rate_per_s), capacity_milli_(static_cast<std::uint64_t>(burst) * 1000U),
        tokens_milli_(capacity_milli_) {}

  /// @brief 1件分を取る。
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 取れたら true
  [[nodiscard]] bool try_take(std::uint64_t now_ms) {
    refill(now_ms);
    if (tokens_milli_ < 1000U) return false;
    tokens_milli_ -= 1000U;
    return true;
  }

 private:
  void refill(std::uint64_t now_ms) {
    if (!started_) {
      started_ = true;
      last_ms_ = now_ms;
      return;
    }
    const std::uint64_t elapsed = now_ms - last_ms_;
    last_ms_ = now_ms;
    const std::uint64_t add = elapsed * rate_per_s_;  // ms × 件/s ＝ 1/1000 件
    tokens_milli_ = (tokens_milli_ + add > capacity_milli_) ? capacity_milli_ : tokens_milli_ + add;
  }

  std::uint32_t rate_per_s_;
  std::uint64_t capacity_milli_;
  std::uint64_t tokens_milli_;
  std::uint64_t last_ms_{0};
  bool started_{false};
};

/// @brief kind ごとの送信量の上限（共通仕様 第4章：平均毎秒10件・瞬間50件）。
class RateLimiter {
 public:
  static constexpr std::uint32_t kRatePerS = 10;
  static constexpr std::uint32_t kBurst = 50;

  /// @brief kind の1件分を取る。
  /// @return 上限を超えるなら false
  [[nodiscard]] bool try_take(Kind k, std::uint64_t now_ms) { return buckets_[index_of(k)].try_take(now_ms); }

 private:
  std::array<TokenBucket, kKindCount> buckets_{
      TokenBucket{kRatePerS, kBurst}, TokenBucket{kRatePerS, kBurst}, TokenBucket{kRatePerS, kBurst},
      TokenBucket{kRatePerS, kBurst}};
};

}  // namespace node::infra
