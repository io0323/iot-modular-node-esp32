#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/specs.hpp"
#include "support/static_vector.hpp"

namespace node::domain {

/// @brief チャネルごとの最新の計測値と、本体側の品質の判定（P2 の決定 #25・#36・#39）。
///
/// stale：最後に good で読めた時刻から、いまの計測周期の3倍を超えたら stale。一度も good で読めていなければ stale。
/// 品質の判定はルールとインターロックの入力にだけ使う（telemetry には読めたチャネルの値と品質をそのまま送る）。
class ReadingStore {
 public:
  static constexpr std::size_t kMaxChannels = 16;
  static constexpr std::uint32_t kStaleFactor = 3;

  /// @brief チャネルを登録し、いまの計測周期を決める（省電力モードでは5倍の値。#39）。
  /// @return 容量がいっぱいなら false
  [[nodiscard]] bool set_period(std::string_view channel, std::uint32_t period_ms);

  /// @brief 計測値で更新する（なければ足す）。quality が good なら「最後に good で読めた時刻」も進める。
  /// @param r 計測値
  /// @param now_ms 読んだ時刻（単調時計の ms）
  /// @return 容量がいっぱいで足せなければ false
  [[nodiscard]] bool update(const Reading& r, std::uint64_t now_ms);

  /// @brief 読めなかったことを記録する（値はそのまま、quality を fault にする）。
  /// @return 容量がいっぱいで足せなければ false
  [[nodiscard]] bool mark_fault(std::string_view channel, std::uint64_t now_ms);

  /// @brief チャネルの最新値（品質はセンサーが返したもの。stale の判定は quality() を使う）。
  /// @return まだ一度も記録していなければ std::nullopt
  [[nodiscard]] std::optional<Reading> latest(std::string_view channel) const;

  /// @brief 本体側で判定した品質。fault が最優先、次に stale（一度も good で読めていない、または古い）。
  /// @param channel チャネル名
  /// @param now_ms いまの時刻（単調時計の ms）
  [[nodiscard]] Quality quality(std::string_view channel, std::uint64_t now_ms) const;

  /// @brief 最後に good で読めた値（1/1000 単位）。一度も読めていなければ 0（故障中の telemetry の value。P3 の決定 #52）。
  [[nodiscard]] std::int32_t last_good_value(std::string_view channel) const;

  /// @brief good と判定できるなら値（1/1000 単位）を返す。ルールとインターロックの入力に使う。
  [[nodiscard]] std::optional<std::int32_t> good_value(std::string_view channel, std::uint64_t now_ms) const;

 private:
  struct Entry {
    Reading reading{};
    bool has_reading{false};
    bool has_good{false};
    std::uint64_t last_good_ms{0};
    std::int32_t last_good_value{0};
    std::uint32_t period_ms{1000};
  };
  Entry* find(std::string_view channel);
  [[nodiscard]] const Entry* find(std::string_view channel) const;
  Entry* find_or_add(std::string_view channel);

  support::StaticVector<Entry, kMaxChannels> entries_{};
};

}  // namespace node::domain
