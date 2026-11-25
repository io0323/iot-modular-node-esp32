// 固定容量コンテナ。Error Framework の仮の代わりとあわせて、取り込み元が決まったら見直す（docs/open-questions.md #5）。
#pragma once

#include <array>
#include <cstddef>

namespace node::support {

/// @brief 容量が固定の可変長配列。ヒープを使わない。
/// @tparam T 要素の型（既定構築・コピーできること）
/// @tparam N 容量
template <typename T, std::size_t N>
class StaticVector {
 public:
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;

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
    if (index >= size_) {
      return;
    }
    for (std::size_t i = index + 1; i < size_; ++i) {
      data_[i - 1] = data_[i];
    }
    --size_;
  }

  /// @brief 全部消す。
  void clear() { size_ = 0; }

  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] static constexpr std::size_t capacity() { return N; }
  [[nodiscard]] bool empty() const { return size_ == 0; }
  [[nodiscard]] bool full() const { return size_ == N; }

  T& operator[](std::size_t i) { return data_[i]; }
  const T& operator[](std::size_t i) const { return data_[i]; }
  T& front() { return data_[0]; }
  [[nodiscard]] const T& front() const { return data_[0]; }
  T& back() { return data_[size_ - 1]; }
  [[nodiscard]] const T& back() const { return data_[size_ - 1]; }

  iterator begin() { return data_.data(); }
  iterator end() { return data_.data() + size_; }
  [[nodiscard]] const_iterator begin() const { return data_.data(); }
  [[nodiscard]] const_iterator end() const { return data_.data() + size_; }

 private:
  std::array<T, N> data_{};
  std::size_t size_{0};
};

}  // namespace node::support
