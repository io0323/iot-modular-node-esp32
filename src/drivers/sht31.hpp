// Sensirion SHT31（SHT3x-DIS。温度・相対湿度）のドライバ（P8a。NFR-01 の実証、open-questions #139〜#141）。
//
// 出典：Sensirion「Datasheet SHT3x-DIS」December 2022 - Version 7。節・表の番号は以下この資料のもの。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "hal/hal.hpp"

namespace node::drivers {

/// @brief SHT3x-DIS の CRC-8（4.12 節 表20：多項式 0x31、初期値 0xFF、入出力の反転なし、最後の XOR 0x00）。
/// @param msb データの上位バイト
/// @param lsb データの下位バイト
/// @return CRC（2バイトのデータだけから計算する）
[[nodiscard]] constexpr std::uint8_t sht31_crc8(std::uint8_t msb, std::uint8_t lsb) {
  unsigned crc = 0xFFU;
  for (const unsigned byte : {unsigned{msb}, unsigned{lsb}}) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) != 0U ? ((crc << 1U) ^ 0x31U) : (crc << 1U);
      crc &= 0xFFU;
    }
  }
  return static_cast<std::uint8_t>(crc);
}
static_assert(sht31_crc8(0xBE, 0xEF) == 0x92, "4.12 節 表20 の例 CRC(0xBEEF) = 0x92");

/// @brief 温度の生値を ℃ の 1/1000 単位にする（4.13 節：T = −45 + 175 × S_T ÷ (2^16 − 1)）。四捨五入する。
/// @param raw 生値 S_T
/// @return 温度（m℃。−45000〜130000）
[[nodiscard]] constexpr std::int32_t sht31_raw_to_milli_c(std::uint16_t raw) {
  return static_cast<std::int32_t>((std::int64_t{175000} * raw + 32767) / 65535) - 45000;
}

/// @brief 相対湿度の生値を %RH の 1/1000 単位にする（4.13 節：RH = 100 × S_RH ÷ (2^16 − 1)）。四捨五入する。
/// @param raw 生値 S_RH
/// @return 相対湿度（m%RH。0〜100000）
[[nodiscard]] constexpr std::int32_t sht31_raw_to_milli_rh(std::uint16_t raw) {
  return static_cast<std::int32_t>((std::int64_t{100000} * raw + 32767) / 65535);
}

/// @brief SHT3x-DIS のドライバ。単発計測・高い再現性・クロックストレッチなしで読む（#141）。
///
/// 読み方（4.3 節・4.4 節）：計測の命令を送り、計測時間だけ待ってから 6 バイト（温度 2・CRC・湿度 2・CRC）を読む。
/// CRC が合わなければその計測を失敗にする（SensorPoller の読み直しと故障の扱いに乗る。#141）。
class Sht31 final : public domain::ISensor {
 public:
  static constexpr std::uint8_t kAddrA = 0x44;  ///< ADDR を logic low（既定。3.4 節 表8）
  static constexpr std::uint8_t kAddrB = 0x45;  ///< ADDR を logic high（同）
  /// 単発計測・高い再現性・クロックストレッチなし（4.3 節 表9：MSB 0x24、LSB 0x00）
  static constexpr std::array<std::uint8_t, 2> kMeasureHighNoStretch{0x24, 0x00};
  /// ソフトリセット（4.9 節 表14：0x30A2）
  static constexpr std::array<std::uint8_t, 2> kSoftReset{0x30, 0xA2};
  /// 高い再現性の計測時間 tMEAS,h の最大 15 ms（2.2 節 表4。VDD 2.4〜5.5 V）
  static constexpr std::uint32_t kMeasureMaxMs = 15;
  /// ソフトリセットから idle までの tSR の最大 1.5 ms（2.2 節 表4）を ms に切り上げた値
  static constexpr std::uint32_t kSoftResetMaxMs = 2;

  /// @brief ドライバを作る。
  /// @param dev I2C の宛先（アドレスは 0x44 か 0x45）
  /// @param timer 待ち時間
  /// @param channels チャネル（温度・相対湿度の順）
  /// @param period 計測の周期
  Sht31(hal::I2cDevice dev, hal::ITimer& timer, std::span<const domain::ChannelSpec, 2> channels,
        std::chrono::milliseconds period)
      : dev_(dev), timer_(timer), channels_(channels), period_(period) {}

  [[nodiscard]] std::string_view id() const override { return "sht31"; }
  /// @brief ソフトリセットを送り、idle になるまで待つ（4.9 節）。
  /// @return 応答がなければ失敗
  support::Result<void> init() override;
  /// @copydoc domain::ISensor::read
  support::Result<std::size_t> read(std::span<domain::Reading> out) override;
  [[nodiscard]] std::chrono::milliseconds period() const override { return period_; }
  [[nodiscard]] std::span<const domain::ChannelSpec> channels() const override { return channels_; }
  support::Result<void> request_recovery() override;

 private:
  hal::I2cDevice dev_;
  hal::ITimer& timer_;
  std::span<const domain::ChannelSpec, 2> channels_;
  std::chrono::milliseconds period_;
  bool initialized_{false};
};

}  // namespace node::drivers
