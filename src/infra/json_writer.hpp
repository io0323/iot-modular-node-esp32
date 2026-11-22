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
  static constexpr std::size_t kMaxDepth = 8;

  /// @brief 書き込み先を決める。
  /// @param buf 書き込み先
  explicit JsonWriter(std::span<char> buf) : buf_(buf) {}

  JsonWriter& begin_object();
  JsonWriter& end_object();
  JsonWriter& begin_array();
  JsonWriter& end_array();
  /// @brief オブジェクトのキーを書く（次に値を書く）。
  JsonWriter& key(std::string_view k);
  /// @brief 文字列を書く（エスケープする）。
  JsonWriter& string(std::string_view s);
  JsonWriter& integer(std::int64_t v);
  JsonWriter& uinteger(std::uint64_t v);
  JsonWriter& boolean(bool v);
  JsonWriter& null();
  /// @brief 1/1000 単位の固定小数を、最大3桁の小数で書く（例：1013250 → 1013.25、25000 → 25.0）。
  JsonWriter& fixed_milli(std::int32_t milli);
  /// @brief 組み立て済みの JSON をそのまま値として書く。
  JsonWriter& raw(std::string_view json);

  /// @brief バッファが足りたか、入れ子の数が正しいか。
  [[nodiscard]] bool ok() const { return ok_ && depth_ == 0; }
  /// @brief 書いた内容。
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
