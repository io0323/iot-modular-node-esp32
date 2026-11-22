#pragma once

#include <optional>
#include <string_view>

#include "domain/specs.hpp"
#include "support/static_vector.hpp"

namespace node::domain {

/// @brief チャネルごとの最新の計測値。
class ReadingStore {
 public:
  static constexpr std::size_t kMaxChannels = 16;

  /// @brief 最新値を更新する（なければ足す）。
  /// @param r 計測値
  /// @return 容量がいっぱいで足せなければ false
  [[nodiscard]] bool update(const Reading& r) {
    for (auto& x : readings_) {
      if (x.channel == r.channel) {
        x = r;
        return true;
      }
    }
    return readings_.push_back(r);
  }

  /// @brief チャネルの最新値。
  /// @param channel チャネル名
  /// @return まだ計測していなければ std::nullopt
  [[nodiscard]] std::optional<Reading> latest(std::string_view channel) const {
    for (const auto& x : readings_) {
      if (x.channel == channel) return x;
    }
    return std::nullopt;
  }

 private:
  support::StaticVector<Reading, kMaxChannels> readings_{};
};

}  // namespace node::domain
