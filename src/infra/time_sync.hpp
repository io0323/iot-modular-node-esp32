#pragma once

#include <cstdint>

namespace node::infra {

/// @brief 時刻同期（SNTP）の取り出し口（P4 の決定 #64・#65・#67）。ESP-IDF の実装は P6。
///
/// UTC は「単調時計 ＋ utc_offset_ms()」で求める。一度同期したら、以後は同期済みとして扱う（#67）。
class ITimeSync {
 public:
  virtual ~ITimeSync() = default;
  /// @brief 同期済みか。
  /// @return 同期済みなら true（一度同期したら以後も true。#67）
  [[nodiscard]] virtual bool synced() const = 0;
  /// @brief 同期で得た「UTC（1970-01-01 からの ms）− 単調時計（ms）」の差（最新の値）。同期前は意味を持たない。
  /// @return 差（ms）
  [[nodiscard]] virtual std::int64_t utc_offset_ms() const = 0;
};

}  // namespace node::infra
