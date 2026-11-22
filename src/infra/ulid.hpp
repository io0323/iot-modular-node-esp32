#pragma once

#include <array>
#include <cstdint>

#include "infra/random.hpp"
#include "support/fixed_string.hpp"

namespace node::infra {

/// @brief ULID の文字列（26文字）。
using UlidString = support::FixedString<26>;

/// @brief ULID を作る（48ビットの ms 時刻＋80ビットの乱数、Crockford Base32 の26文字）。
/// @param utc_ms 時刻部（1970-01-01 からの ms。48ビットに収まること）
/// @param random 乱数部の 10 バイト
/// @return ULID
[[nodiscard]] UlidString encode_ulid(std::uint64_t utc_ms, const std::array<std::uint8_t, 10>& random);

/// @brief ULID を作る。乱数部は rng から取る。
[[nodiscard]] UlidString make_ulid(std::int64_t utc_ms, IRandom& rng);

}  // namespace node::infra
