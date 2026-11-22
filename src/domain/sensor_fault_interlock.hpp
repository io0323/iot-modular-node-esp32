#pragma once

#include <cstdint>
#include <string_view>

#include "domain/interlock.hpp"

namespace node::domain {

/// @brief センサーが good でない（fault・stale）間の安全側の制限（rule: sensor_fault。P2 の決定 #22・#23）。
///
/// 対象チャネルの品質が good でない間、出力を固定する（ファン：1000）か、上限をかける（ポンプ：0）。
/// good に戻った時点で解除する。rejects_commands が true の構成では、その間の set_actuator を rejected（unsafe）にする。
class SensorFaultInterlock final : public IInterlock {
 public:
  /// @brief かけ方。
  enum class Action : std::uint8_t { Force, Upper };

  struct Config {
    std::string_view channel;  ///< 見張るチャネル
    Action action;             ///< 固定か上限か
    Level level;               ///< 固定する値、または上限
    bool rejects_commands;     ///< 効いている間の set_actuator を rejected（unsafe）にする
  };

  explicit SensorFaultInterlock(const Config& config) : config_(config) {}

  [[nodiscard]] std::string_view rule() const override { return "sensor_fault"; }
  InterlockVerdict evaluate(const InterlockInput& in) override;
  [[nodiscard]] bool engaged() const override { return engaged_; }
  [[nodiscard]] bool rejects_commands(const ReadingStore& store, std::uint64_t now_ms) const override;

 private:
  Config config_;
  bool engaged_{false};
};

}  // namespace node::domain
