#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace node::infra {

/// @brief 乱数の取り出し口（boot_id と ULID に使う）。
class IRandom {
 public:
  virtual ~IRandom() = default;
  /// @brief 乱数で埋める。
  /// @param out 書き込み先
  virtual void fill(std::span<std::uint8_t> out) = 0;

  /// @brief 32ビットの乱数。
  /// @return 乱数
  std::uint32_t next_u32() {
    std::array<std::uint8_t, 4> b{};
    fill(b);
    return static_cast<std::uint32_t>(b[0]) << 24U | static_cast<std::uint32_t>(b[1]) << 16U |
           static_cast<std::uint32_t>(b[2]) << 8U | static_cast<std::uint32_t>(b[3]);
  }
};

}  // namespace node::infra
