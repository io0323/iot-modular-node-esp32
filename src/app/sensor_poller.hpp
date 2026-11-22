#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "domain/reading_store.hpp"
#include "support/static_vector.hpp"

namespace node::app {

/// @brief センサーの周期的な計測と故障の管理（基本設計書 FR-02、第7章「I2C センサー無応答」）。
///
/// read に失敗したらそのセンサーのチャネルを fault にし、SENSOR_FAULT をチャネルごとに1件出す（#6）。
/// 故障の間は30秒ごとに init で再初期化を試み、続けて読めたら SENSOR_RECOVERED をチャネルごとに1件出す。
/// 省電力モードでは周期を5倍にする。切り替えた後の次の計測は「最後に計測した時刻＋新しい周期」（#40）。
class SensorPoller {
 public:
  static constexpr std::size_t kMaxSensors = 8;
  static constexpr std::size_t kMaxReadings = domain::ReadingStore::kMaxChannels;
  static constexpr std::uint64_t kReinitIntervalMs = 30U * 1000U;

  /// @brief センサーの故障・復旧（イベントの元）。
  struct SensorChange {
    std::string_view channel;
    bool fault{false};  ///< true：故障、false：復旧
  };

  /// @brief 1回の tick の結果。
  struct Result {
    support::StaticVector<domain::Reading, kMaxReadings> readings{};  ///< この tick で読めた計測値
    support::StaticVector<SensorChange, kMaxReadings> changes{};
  };

  SensorPoller(std::span<domain::ISensor* const> sensors, domain::ReadingStore& store);

  /// @brief 周期の起点を決め、チャネルの周期を ReadingStore に登録する（最初の計測は now_ms）。
  void start(std::uint64_t now_ms);
  /// @brief 計測周期の倍率を変える（省電力モードは5倍）。
  void set_period_factor(std::uint32_t factor);
  [[nodiscard]] std::uint32_t period_factor() const { return factor_; }

  /// @brief 周期が来たセンサーを計測する。
  void tick(std::uint64_t now_ms, Result& out);

  /// @brief 計測に失敗した回数（診断用）。
  [[nodiscard]] std::uint32_t read_errors() const { return read_errors_; }

 private:
  struct State {
    std::uint64_t next_due_ms{0};
    std::uint64_t last_read_ms{0};
    bool faulted{false};
    std::uint64_t next_reinit_ms{0};
  };

  [[nodiscard]] std::uint64_t period_ms(const domain::ISensor& s) const;
  void register_periods();
  bool read_into(domain::ISensor& s, std::uint64_t now_ms, Result& out);
  void mark_faulted(std::size_t i, std::uint64_t now_ms, Result& out);

  std::span<domain::ISensor* const> sensors_;
  domain::ReadingStore& store_;
  std::array<State, kMaxSensors> states_{};
  std::uint32_t factor_{1};
  std::uint32_t read_errors_{0};
};

}  // namespace node::app
