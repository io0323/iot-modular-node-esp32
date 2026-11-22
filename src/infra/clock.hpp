#pragma once

#include <cstdint>

namespace node::infra {

/// @brief 時刻の取り出し口。std::chrono の時計を直接呼ぶのは platform だけ（CLAUDE.md）。
class IClock {
 public:
  virtual ~IClock() = default;
  /// @brief 単調時計（ms）。経過時間・リース・周期に使う。
  [[nodiscard]] virtual std::uint64_t monotonic_ms() const = 0;
  /// @brief UTC の現在時刻（1970-01-01 からの ms）。電文の ts と expires_at に使う。
  [[nodiscard]] virtual std::int64_t utc_ms() const = 0;
  /// @brief UTC の時刻が合っているか（P1 のホスト版は常に true。SNTP の扱いは P4）。
  [[nodiscard]] virtual bool synced() const = 0;
};

}  // namespace node::infra
