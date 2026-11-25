// Error Framework の仮の代わり。取り込み元が決まったら置き換える（docs/open-questions.md #5）。
#pragma once

#include <type_traits>
#include <utility>

#include "support/error.hpp"

namespace node::support {

/// @brief 値かエラーのどちらかを持つ戻り値。例外の代わりに使う。
/// @tparam T 値の型（既定構築できること）
template <typename T>
class [[nodiscard]] Result {
  static_assert(std::is_default_constructible_v<T>, "Result<T> の T は既定構築できること");

 public:
  /// @brief 値で成功を作る。
  /// @param value 値
  // 値・エラーをそのまま return できるように暗黙の変換にする（std::expected と同じ考え方。docs/quality.md の抑止の表）
  // cppcheck-suppress noExplicitConstructor
  Result(T value) : ok_(true), value_(std::move(value)) {}  // NOLINT(google-explicit-constructor)
  /// @brief エラーで失敗を作る。
  /// @param error エラー
  // cppcheck-suppress noExplicitConstructor
  Result(const Error& error) : ok_(false), error_(error) {}  // NOLINT(google-explicit-constructor)

  /// @brief 成功したか。
  [[nodiscard]] bool has_value() const { return ok_; }
  /// @brief 成功したか。
  explicit operator bool() const { return ok_; }

  /// @brief 値（成功のときだけ呼ぶ）。
  [[nodiscard]] T& value() & { return value_; }
  /// @brief 値（成功のときだけ呼ぶ）。
  [[nodiscard]] const T& value() const& { return value_; }
  /// @brief 値。失敗なら fallback を返す。
  [[nodiscard]] T value_or(T fallback) const { return ok_ ? value_ : fallback; }
  /// @brief エラー（失敗のときだけ意味がある）。
  [[nodiscard]] Error error() const { return error_; }

 private:
  bool ok_;
  T value_{};
  Error error_{};
};

/// @brief 値を持たない Result。
template <>
class [[nodiscard]] Result<void> {
 public:
  /// @brief 成功を作る。
  Result() = default;
  /// @brief エラーで失敗を作る。
  /// @param error エラー
  // cppcheck-suppress noExplicitConstructor
  Result(const Error& error) : ok_(false), error_(error) {}  // NOLINT(google-explicit-constructor)

  /// @brief 成功したか。
  [[nodiscard]] bool has_value() const { return ok_; }
  /// @brief 成功したか。
  explicit operator bool() const { return ok_; }
  /// @brief エラー（失敗のときだけ意味がある）。
  [[nodiscard]] Error error() const { return error_; }

 private:
  bool ok_{true};
  Error error_{};
};

/// @brief 成功した Result<void> を返す。
inline Result<void> ok() { return {}; }

}  // namespace node::support
