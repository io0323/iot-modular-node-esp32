// センサーのハードに触る部分（P6 の決定 #96・#110）。依頼（読む・再初期化）を ISensor に対して行い、結果を返す。
// ホスト版は Node がその場で submit と poll_report を呼ぶ。ターゲット版は sense のタスクが execute を呼ぶ。
#pragma once

#include <cstddef>
#include <span>

#include "domain/device.hpp"
#include "domain/io.hpp"
#include "support/static_vector.hpp"

namespace node::drivers {

/// @brief センサーへの依頼を実行する。submit はその場で実行し、結果を submit した順に poll_report で返す。
class SensorIo final : public domain::ISensorIo {
 public:
  static constexpr std::size_t kMaxPending = 8;               ///< 取り出していない結果の上限
  static constexpr std::uint32_t kAttemptsBeforeRecovery = 3;  ///< 回復を要求するまでの読み取りの回数（#55）

  /// @brief センサーの並びを受け取る。
  /// @param sensors 構成表のセンサー（寿命はプログラム全体）
  explicit SensorIo(std::span<domain::ISensor* const> sensors) : sensors_(sensors) {}

  /// @brief 依頼を実行する（sense のタスクから呼ぶ）。
  /// @param request 依頼
  /// @return 結果
  domain::SensorReport execute(const domain::SensorRequest& request);

  bool submit(const domain::SensorRequest& request) override;
  bool poll_report(domain::SensorReport& out) override;

 private:
  static bool read_once(domain::ISensor& s, domain::SensorReport& out);

  std::span<domain::ISensor* const> sensors_;
  support::StaticVector<domain::SensorReport, kMaxPending> pending_{};
};

}  // namespace node::drivers
