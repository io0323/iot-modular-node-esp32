#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace node::infra {

/// @brief 固定バッファに JSON を書く。区切りのカンマは自動で入れる。
///
/// バッファが足りなくなったら以後の書き込みをやめ、ok() が false になる。
class JsonWriter {
 public:
  static constexpr std::size_t kMaxDepth = 8;  ///< 入れ子の上限

  /// @brief 書き込み先を決める。
  /// @param buf 書き込み先
  explicit JsonWriter(std::span<char> buf) : buf_(buf) {}

  /// @brief "{" を書く。
  /// @return *this
  JsonWriter& begin_object();
  /// @brief "}" を書く。
  /// @return *this
  JsonWriter& end_object();
  /// @brief "[" を書く。
  /// @return *this
  JsonWriter& begin_array();
  /// @brief "]" を書く。
  /// @return *this
  JsonWriter& end_array();
  /// @brief オブジェクトのキーを書く（次に値を書く）。
  /// @param k キー（エスケープする）
  /// @return *this
  JsonWriter& key(std::string_view k);
  /// @brief 文字列を書く（エスケープする）。
  /// @param s 文字列
  /// @return *this
  JsonWriter& string(std::string_view s);
  /// @brief 符号付きの整数を書く。
  /// @param v 値
  /// @return *this
  JsonWriter& integer(std::int64_t v);
  /// @brief 符号なしの整数を書く。
  /// @param v 値
  /// @return *this
  JsonWriter& uinteger(std::uint64_t v);
  /// @brief true か false を書く。
  /// @param v 値
  /// @return *this
  JsonWriter& boolean(bool v);
  /// @brief null を書く。
  /// @return *this
  JsonWriter& null();
  /// @brief 1/1000 単位の固定小数を、最大3桁の小数で書く（例：1013250 → 1013.25、25000 → 25.0）。
  /// @param milli 値（1/1000 単位）
  /// @return *this
  JsonWriter& fixed_milli(std::int32_t milli);
  /// @brief 組み立て済みの JSON をそのまま値として書く。
  /// @param json JSON の値
  /// @return *this
  JsonWriter& raw(std::string_view json);

  /// @brief バッファが足りたか、入れ子の数が正しいか。
  /// @return 正しい JSON を書き終えていれば true
  [[nodiscard]] bool ok() const { return ok_ && depth_ == 0; }
  /// @brief 書いた内容。
  /// @return 書いた文字列（buf の中を指す）
  [[nodiscard]] std::string_view view() const { return {buf_.data(), len_}; }

 private:
  void before_value();
  void open(char c);
  void close(char c);
  void put(char c);
  void put(std::string_view s);
  void put_escaped(std::string_view s);
  void put_uint(std::uint64_t v);

  std::span<char> buf_;
  std::size_t len_{0};
  bool ok_{true};
  std::array<bool, kMaxDepth + 1> first_{};
  std::size_t depth_{0};
  bool after_key_{false};
};

}  // namespace node::infra
