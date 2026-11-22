#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/control_arbiter.hpp"
#include "app/publisher.hpp"
#include "domain/device.hpp"
#include "domain/reading_store.hpp"
#include "support/static_vector.hpp"

namespace node::app {

/// @brief telemetry の送信（基本設計書 FR-02・FR-07）。
///
/// センサーごとの周期が来たら計測し、その回に計測したチャネルの readings と、全アクチュエーターの level・owner を
/// 1件の telemetry にして送る（QoS 0）。未接続の間は計測だけして送らない（telemetry は保持しない。FR-11）。
/// 仮版：計測もここで行う（P2・P3 で sense タスクに分ける）。
class TelemetryService {
 public:
  static constexpr std::size_t kMaxSensors = 8;
  static constexpr std::size_t kMaxReadings = domain::ReadingStore::kMaxChannels;

  /// @brief 対象を受け取る。
  TelemetryService(std::span<domain::ISensor* const> sensors, const ControlArbiter& arbiter,
                   domain::ReadingStore& store);

  /// @brief 周期の起点を決める（最初の計測は now_ms）。
  void start(std::uint64_t now_ms);

  /// @brief 周期が来たセンサーを計測し、接続中なら telemetry を送る。
  void tick(std::uint64_t now_ms, Publisher& publisher);

  /// @brief 計測に失敗した回数（診断用）。
  [[nodiscard]] std::uint32_t read_errors() const { return read_errors_; }

 private:
  std::span<domain::ISensor* const> sensors_;
  const ControlArbiter& arbiter_;
  domain::ReadingStore& store_;
  std::array<std::uint64_t, kMaxSensors> next_due_ms_{};
  std::uint32_t read_errors_{0};
};

}  // namespace node::app
