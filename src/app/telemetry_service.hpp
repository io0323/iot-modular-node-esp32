#pragma once

#include <span>

#include "app/control_arbiter.hpp"
#include "app/publisher.hpp"
#include "domain/specs.hpp"

namespace node::app {

/// @brief telemetry の送信（基本設計書 FR-02・FR-07）。
///
/// SensorPoller がその tick で読めたチャネルの readings と、全アクチュエーターの level・owner を1件の telemetry にして送る
/// （QoS 0）。計測の周期は SensorPoller が決める（省電力モードは5倍）。未接続の間は送らない（telemetry は保持しない。FR-11）。
class TelemetryService {
 public:
  explicit TelemetryService(const ControlArbiter& arbiter) : arbiter_(arbiter) {}

  /// @brief 読めた計測値があれば telemetry を送る。
  /// @param readings その tick で読めた計測値（空なら送らない）
  /// @param publisher 発行の窓口
  void publish(std::span<const domain::Reading> readings, Publisher& publisher) const;

 private:
  const ControlArbiter& arbiter_;
};

}  // namespace node::app
