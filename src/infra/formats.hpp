#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "support/fixed_string.hpp"

namespace node::infra {

/// @brief ts の文字列（"2026-11-20T06:00:12.345Z"、24文字）。
using TsString = support::FixedString<24>;

/// @brief ULID か（Crockford Base32 の26文字、先頭は 0–7。common.schema.json の ulid）。
[[nodiscard]] bool is_ulid(std::string_view s);
/// @brief device_id の形か（common.schema.json の device_id）。
[[nodiscard]] bool is_device_id(std::string_view s);
/// @brief チャネル名・アクチュエーター名の形か（snake_case。common.schema.json の channel・actuator_id）。
[[nodiscard]] bool is_snake_id(std::string_view s);
/// @brief boot_id の形か（16進小文字8桁）。
[[nodiscard]] bool is_boot_id(std::string_view s);

/// @brief ts の形の文字列を読む（UTC、ミリ秒3桁、末尾 Z。common.schema.json の ts）。
/// @param s 文字列
/// @return 1970-01-01 からの ms。形が違えば std::nullopt
[[nodiscard]] std::optional<std::int64_t> parse_ts(std::string_view s);

/// @brief UTC の時刻を ts の形にする。
/// @param utc_ms 1970-01-01 からの ms（0 以上）
/// @return "YYYY-MM-DDTHH:MM:SS.mmmZ"
[[nodiscard]] TsString format_ts(std::int64_t utc_ms);

}  // namespace node::infra
