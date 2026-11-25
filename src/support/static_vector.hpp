// 固定容量コンテナ。Error Framework の仮の代わりとあわせて、取り込み元が決まったら見直す（docs/open-questions.md #5）。
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace node::support {

/// @brief 容量が固定の可変長配列。ヒープを使わない。
/// @tparam T 要素の型（既定構築・コピーできること）
/// @tparam N 容量
template <typename T, std::size_t N>
class StaticVector {
 public:
  using value_type = T;            ///< 要素の型
  using iterator = T*;             ///< 反復子
  using const_iterator = const T*;  ///< 読むだけの反復子

  /// @brief 末尾に足す。
  /// @param value 足す値
  /// @return 容量がいっぱいで足せなければ false
  [[nodiscard]] bool push_back(const T& value) {
    if (size_ >= N) {
      return false;
    }
    data_[size_++] = value;
    return true;
  }

  /// @brief 指定した位置の要素を消し、後ろを詰める。
  /// @param index 位置（size() 未満）
  void erase_at(std::size_t index) {
    // size_ は N を超えないが、GCC の -O2 が範囲外の添字を疑わないよう N とも比べる（振る舞いは同じ）
    if (index >= size_ || index >= N) {
      return;
    }
    // 後ろを1つ前に詰める（重なる範囲を前へ写すので std::copy でよい）
    std::copy(data_.data() + index + 1, data_.data() + size_, data_.data() + index);
    --size_;
  }

  /// @brief 全部消す。
  void clear() { size_ = 0; }

  /// @brief 要素の数。
  /// @return 数
  [[nodiscard]] std::size_t size() const { return size_; }
  /// @brief 容量。
  /// @return N
  [[nodiscard]] static constexpr std::size_t capacity() { return N; }
  /// @brief 空か。
  /// @return 空なら true
  [[nodiscard]] bool empty() const { return size_ == 0; }
  /// @brief いっぱいか。
  /// @return いっぱいなら true
  [[nodiscard]] bool full() const { return size_ == N; }

  /// @brief 要素。
  /// @param i 位置（size() 未満）
  /// @return 要素
  T& operator[](std::size_t i) { return data_[i]; }
  /// @brief 要素（読むだけ）。
  /// @param i 位置（size() 未満）
  /// @return 要素
  const T& operator[](std::size_t i) const { return data_[i]; }
  /// @brief 先頭の要素（空でないこと）。
  /// @return 要素
  T& front() { return data_[0]; }
  /// @brief 先頭の要素（読むだけ。空でないこと）。
  /// @return 要素
  [[nodiscard]] const T& front() const { return data_[0]; }
  /// @brief 末尾の要素（空でないこと）。
  /// @return 要素
  T& back() { return data_[size_ - 1]; }
  /// @brief 末尾の要素（読むだけ。空でないこと）。
  /// @return 要素
  [[nodiscard]] const T& back() const { return data_[size_ - 1]; }

  /// @brief 先頭の反復子。
  /// @return 反復子
  iterator begin() { return data_.data(); }
  /// @brief 末尾の次の反復子。
  /// @return 反復子
  iterator end() { return data_.data() + size_; }
  /// @brief 先頭の反復子（読むだけ）。
  /// @return 反復子
  [[nodiscard]] const_iterator begin() const { return data_.data(); }
  /// @brief 末尾の次の反復子（読むだけ）。
  /// @return 反復子
  [[nodiscard]] const_iterator end() const { return data_.data() + size_; }

 private:
  std::array<T, N> data_{};
  std::size_t size_{0};
};

}  // namespace node::support
