#pragma once

#include <cstdint>

namespace node::infra {

/// @brief 単調時計の取り出し口。std::chrono の時計を直接呼ぶのは platform だけ（CLAUDE.md）。
///
/// UTC は持たない。UTC は「単調時計 ＋ 時刻同期で得た差」（ITimeSync）で求める（open-questions #75）。
class IClock {
 public:
  virtual ~IClock() = default;
  /// @brief 単調時計（ms）。経過時間・リース・周期・イベントの発生時刻に使う。
  [[nodiscard]] virtual std::uint64_t monotonic_ms() const = 0;
};

}  // namespace node::infra
