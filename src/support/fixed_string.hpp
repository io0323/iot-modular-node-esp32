// 固定容量の文字列。Error Framework の仮の代わりとあわせて、取り込み元が決まったら見直す（docs/open-questions.md #5）。
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace node::support {

/// @brief 容量が固定の文字列。ヒープを使わず、常に NUL で終わる。
/// @tparam N 最大の文字数（NUL を除く）
template <std::size_t N>
class FixedString {
 public:
  FixedString() = default;

  /// @brief 中身を置き換える。
  /// @param s 新しい中身
  /// @return 容量を超えるなら false（中身は空になる）
  [[nodiscard]] bool assign(std::string_view s) {
    clear();
    return append(s);
  }

  /// @brief 末尾に足す。
  /// @param s 足す文字列
  /// @return 容量を超えるなら false（中身は変えない）
  [[nodiscard]] bool append(std::string_view s) {
    if (s.size() > N - size_) {
      return false;
    }
    for (char const c : s) {
      buf_[size_++] = c;
    }
    buf_[size_] = '\0';
    return true;
  }

  /// @brief 1文字足す。
  /// @param c 足す文字
  /// @return 容量を超えるなら false
  [[nodiscard]] bool push_back(char c) {
    if (size_ >= N) {
      return false;
    }
    buf_[size_++] = c;
    buf_[size_] = '\0';
    return true;
  }

  /// @brief 空にする。
  void clear() {
    size_ = 0;
    buf_[0] = '\0';
  }

  [[nodiscard]] std::string_view view() const { return {buf_.data(), size_}; }
  [[nodiscard]] const char* c_str() const { return buf_.data(); }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] bool empty() const { return size_ == 0; }
  [[nodiscard]] static constexpr std::size_t capacity() { return N; }

  friend bool operator==(const FixedString& a, std::string_view b) { return a.view() == b; }

 private:
  std::array<char, N + 1> buf_{};
  std::size_t size_{0};
};

}  // namespace node::support
