#pragma once

#include <span>
#include <string_view>

#include "app/control_arbiter.hpp"
#include "domain/device.hpp"
#include "domain/types.hpp"
#include "infra/json_writer.hpp"

namespace node::app {

/// @brief state の payload（共通仕様 第3章：status・mode・channels[]・actuators[]・fw_version）。
///
/// online の state は接続したとき・モードを変えたとき・get_status のときに送る（retain、QoS 1）。
/// offline の state は LWT と正常終了のときに使い、seq を進めない（v0.2.1 #29）。
class StateService {
 public:
  /// @brief state の元になる情報を受け取る。
  /// @param sensors センサー（channels[] の元）
  /// @param arbiter 調停（actuators[] の元）
  /// @param fw_version ファームウェアの版
  StateService(std::span<domain::ISensor* const> sensors, const ControlArbiter& arbiter,
               std::string_view fw_version)
      : sensors_(sensors), arbiter_(arbiter), fw_version_(fw_version) {}

  /// @brief payload のオブジェクトを書く。
  /// @param w 書き込み先
  /// @param online online なら true、offline なら false
  /// @param mode いまのモード
  /// @param period_factor 計測周期の倍率（省電力モードは5。channels[].period_ms に掛ける）
  void write_payload(infra::JsonWriter& w, bool online, domain::Mode mode, std::uint32_t period_factor = 1) const;

 private:
  std::span<domain::ISensor* const> sensors_;
  const ControlArbiter& arbiter_;
  std::string_view fw_version_;
};

}  // namespace node::app
