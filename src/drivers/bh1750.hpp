// ROHM BH1750FVI（照度）のドライバ。
//
// 出典：ROHM「BH1750FVI Technical Note」（Ambient Light Sensor IC Series, Digital 16bit Serial Output Type）
// 2011.11 - Rev.D。ページ番号は以下この資料のもの。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "hal/hal.hpp"

namespace node::drivers {

/// @brief 16 ビットの計測結果を lux の 1/1000 単位にする。
///
/// H-Resolution Mode の換算は「計測結果 ÷ 1.2」（7 ページの計算例：0x8390 → (2^15+2^9+2^8+2^7+2^4) / 1.2 ≒ 28067 lx。
/// 1.2 は Measurement accuracy（2 ページ・11 ページ）。MTreg は既定の 69 のまま）。四捨五入する。
[[nodiscard]] constexpr std::int32_t bh1750_count_to_milli_lux(std::uint16_t count) {
  return static_cast<std::int32_t>((static_cast<std::int64_t>(count) * 10000 + 6) / 12);
}

/// @brief BH1750FVI のドライバ。連続高分解能モード（Continuously H-Resolution Mode）で動かす。
class Bh1750 final : public domain::ISensor {
 public:
  static constexpr std::uint8_t kAddrLow = 0x23;   ///< ADDR = 'L' のとき "0100011"（10 ページ 2) Slave Address）
  static constexpr std::uint8_t kAddrHigh = 0x5C;  ///< ADDR = 'H' のとき "1011100"（同）
  static constexpr std::uint8_t kPowerOn = 0x01;   ///< Power On 0000_0001（5 ページ Instruction Set Architecture）
  static constexpr std::uint8_t kContinuousHRes = 0x10;  ///< Continuously H-Resolution Mode 0001_0000（同）
  /// H-Resolution Mode の計測時間 tHR の最大 180 ms（2 ページ Electrical Characteristics）。
  /// 最初の結果は計測の命令から最大 180 ms 後（7 ページ ex1 ②）
  static constexpr std::uint32_t kMeasureMaxMs = 180;

  /// @brief ドライバを作る。
  /// @param dev I2C の宛先
  /// @param timer 待ち時間
  /// @param channel チャネル（lux）
  /// @param period 計測の周期（120 ms 以上であること。連続モードの結果は約 120 ms ごとに更新される。7 ページ）
  Bh1750(hal::I2cDevice dev, hal::ITimer& timer, std::span<const domain::ChannelSpec, 1> channel,
         std::chrono::milliseconds period)
      : dev_(dev), timer_(timer), channel_(channel), period_(period) {}

  [[nodiscard]] std::string_view id() const override { return "bh1750"; }
  support::Result<void> init() override;
  support::Result<std::size_t> read(std::span<domain::Reading> out) override;
  [[nodiscard]] std::chrono::milliseconds period() const override { return period_; }
  [[nodiscard]] std::span<const domain::ChannelSpec> channels() const override { return channel_; }
  support::Result<void> request_recovery() override;

 private:
  support::Result<void> command(std::uint8_t opcode);

  hal::I2cDevice dev_;
  hal::ITimer& timer_;
  std::span<const domain::ChannelSpec, 1> channel_;
  std::chrono::milliseconds period_;
  bool initialized_{false};
};

}  // namespace node::drivers
