// Error Framework の仮の代わり。取り込み元が決まったら置き換える（docs/open-questions.md #5）。
#pragma once

#include <cstdint>
#include <string_view>

namespace node::support {

/// @brief エラーの種類。
enum class ErrorCode : std::uint8_t {
  InvalidArgument,
  OutOfRange,
  BufferFull,
  NotFound,
  Busy,
  NotConnected,
  ParseError,
  IoError,
  Internal,
};

/// @brief エラーの種類を短い文字列にする。
/// @param code エラーの種類
/// @return 英小文字の短い名前（例："busy"）
constexpr std::string_view to_string(ErrorCode code) {
  switch (code) {
    case ErrorCode::InvalidArgument: return "invalid_argument";
    case ErrorCode::OutOfRange: return "out_of_range";
    case ErrorCode::BufferFull: return "buffer_full";
    case ErrorCode::NotFound: return "not_found";
    case ErrorCode::Busy: return "busy";
    case ErrorCode::NotConnected: return "not_connected";
    case ErrorCode::ParseError: return "parse_error";
    case ErrorCode::IoError: return "io_error";
    case ErrorCode::Internal: return "internal";
  }
  return "unknown";
}

/// @brief エラー。種類と、静的な領域にある短い説明を持つ。
struct Error {
  ErrorCode code{ErrorCode::Internal};  ///< 種類
  std::string_view what;  ///< 文字列リテラルなど、寿命の長い文字列だけを入れる
};

/// @brief Error を作る。
/// @param code エラーの種類
/// @param what 短い説明（文字列リテラル）
/// @return Error
constexpr Error make_error(ErrorCode code, std::string_view what = {}) { return Error{code, what}; }

}  // namespace node::support
