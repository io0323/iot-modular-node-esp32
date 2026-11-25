// Bosch Sensortec BME280（温度・湿度・気圧）のドライバ。
//
// 出典：Bosch Sensortec「BME280 – Data sheet」Document number BST-BME280-DS001-24、Document revision 1.24、
// February 2024（各ページのフッターの表記は BST-BME280-DS001-23 Revision_1.23_012022）。
// 節の番号は以下この資料のもの。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "hal/hal.hpp"

namespace node::drivers {

/// @brief 補正係数（4.2.2 節 表16）。
struct Bme280Calibration {
  std::uint16_t dig_T1{0};
  std::int16_t dig_T2{0};
  std::int16_t dig_T3{0};
  std::uint16_t dig_P1{0};
  std::int16_t dig_P2{0};
  std::int16_t dig_P3{0};
  std::int16_t dig_P4{0};
  std::int16_t dig_P5{0};
  std::int16_t dig_P6{0};
  std::int16_t dig_P7{0};
  std::int16_t dig_P8{0};
  std::int16_t dig_P9{0};
  std::uint8_t dig_H1{0};
  std::int16_t dig_H2{0};
  std::uint8_t dig_H3{0};
  std::int16_t dig_H4{0};
  std::int16_t dig_H5{0};
  std::int8_t dig_H6{0};
};

/// @brief 生値（4 章、5.4.7〜5.4.9 節）。
struct Bme280Raw {
  std::int32_t adc_T{0};  ///< 20 ビット
  std::int32_t adc_P{0};  ///< 20 ビット
  std::int32_t adc_H{0};  ///< 16 ビット
};

/// @brief 補正した値（4.2.3 節の整数の式の出力そのまま）。
struct Bme280Compensated {
  std::int32_t temperature_centi{0};  ///< 0.01 DegC（"5123" は 51.23 DegC）
  std::uint32_t pressure_q24_8{0};    ///< Pa の Q24.8（"24674867" は 96386.2 Pa）
  std::uint32_t humidity_q22_10{0};   ///< %RH の Q22.10（"47445" は 46.333 %RH）
  std::int32_t t_fine{0};
};

/// @brief 0x88〜0xA1（26 バイト）と 0xE1〜0xE7（7 バイト）から補正係数を読む（4.2.2 節 表16）。
/// dig_H4・dig_H5 は 12 ビットの符号付き（表16）。上位の 8 ビット（0xE4・0xE6）を符号付きとして広げる。
[[nodiscard]] Bme280Calibration parse_bme280_calibration(std::span<const std::uint8_t, 26> c,
                                                         std::span<const std::uint8_t, 7> h);

/// @brief 0xF7〜0xFE（8 バイト）の burst read から生値を組み立てる（4 章、5.4.7〜5.4.9 節 表29〜31）。
[[nodiscard]] Bme280Raw parse_bme280_raw(std::span<const std::uint8_t, 8> d);

/// @brief 4.2.3 節の整数の補正式（BME280_compensate_T_int32・BME280_compensate_P_int64・
/// bme280_compensate_H_int32、コードの rev.1.1）で補正する。
[[nodiscard]] Bme280Compensated compensate_bme280(const Bme280Calibration& k, const Bme280Raw& raw);

/// @brief BME280 のドライバ。1台で temp_air・humidity・pressure の3チャネルを持つ。
///
/// 設定は 3.5.1 節（Weather monitoring）の推奨：強制モード、温度・気圧・湿度とも ×1、フィルタなし（open-questions #57）。
class Bme280 final : public domain::ISensor {
 public:
  static constexpr std::uint8_t kChipId = 0x60;          ///< 5.4.1 節
  static constexpr std::uint8_t kRegId = 0xD0;           ///< 5.3 節 表18
  static constexpr std::uint8_t kRegReset = 0xE0;        ///< 5.4.2 節
  static constexpr std::uint8_t kResetWord = 0xB6;       ///< 5.4.2 節
  static constexpr std::uint8_t kRegCtrlHum = 0xF2;      ///< 5.4.3 節
  static constexpr std::uint8_t kRegStatus = 0xF3;       ///< 5.4.4 節
  static constexpr std::uint8_t kRegCtrlMeas = 0xF4;     ///< 5.4.5 節
  static constexpr std::uint8_t kRegConfig = 0xF5;       ///< 5.4.6 節
  static constexpr std::uint8_t kRegData = 0xF7;         ///< 4 章（0xF7〜0xFE を burst read）
  static constexpr std::uint8_t kRegCalib0 = 0x88;       ///< 4.2.2 節 表16
  static constexpr std::uint8_t kRegCalib26 = 0xE1;      ///< 4.2.2 節 表16
  static constexpr std::uint8_t kStatusMeasuring = 0x08;  ///< status の bit 3（表21）
  static constexpr std::uint8_t kStatusImUpdate = 0x01;   ///< status の bit 0（表21）
  static constexpr std::uint8_t kCtrlHumX1 = 0x01;        ///< osrs_h = 001（表20）
  static constexpr std::uint8_t kConfigFilterOff = 0x00;  ///< filter = 000（表28）
  /// ctrl_meas：osrs_t = 001、osrs_p = 001（表23・24）、mode = 00（sleep）／01（forced）（表25）
  static constexpr std::uint8_t kCtrlMeasSleep = 0x24;
  static constexpr std::uint8_t kCtrlMeasForced = 0x25;
  /// 起動時間 t_startup 2 ms（1.1 節 表1）
  static constexpr std::uint32_t kStartupMs = 2;
  /// 計測時間の最大（9.1 節の式で ×1・×1・×1：1.25 + 2.3 + 2.875 + 2.875 = 9.3 ms）を切り上げた値
  static constexpr std::uint32_t kMeasureMaxMs = 10;
  /// status のビットを見直す回数と間隔（データシートに定めがないので、P3 で決めた実装の値）
  static constexpr std::uint32_t kStatusPolls = 5;
  static constexpr std::uint32_t kStatusPollIntervalMs = 1;

  /// @brief ドライバを作る。
  /// @param dev I2C の宛先（アドレスは 0x76 か 0x77。6.2 節）
  /// @param timer 待ち時間
  /// @param channels チャネル（temp_air・humidity・pressure の順の3つ）
  /// @param period 計測の周期
  Bme280(hal::I2cDevice dev, hal::ITimer& timer, std::span<const domain::ChannelSpec, 3> channels,
         std::chrono::milliseconds period)
      : dev_(dev), timer_(timer), channels_(channels), period_(period) {}

  [[nodiscard]] std::string_view id() const override { return "bme280"; }
  support::Result<void> init() override;
  support::Result<std::size_t> read(std::span<domain::Reading> out) override;
  [[nodiscard]] std::chrono::milliseconds period() const override { return period_; }
  [[nodiscard]] std::span<const domain::ChannelSpec> channels() const override { return channels_; }
  support::Result<void> request_recovery() override;

  [[nodiscard]] const Bme280Calibration& calibration() const { return cal_; }

 private:
  support::Result<void> write_reg(std::uint8_t reg, std::uint8_t value) const;
  support::Result<void> read_regs(std::uint8_t reg, std::span<std::uint8_t> out) const;
  support::Result<void> wait_status_clear(std::uint8_t mask);

  hal::I2cDevice dev_;
  hal::ITimer& timer_;
  std::span<const domain::ChannelSpec, 3> channels_;
  std::chrono::milliseconds period_;
  Bme280Calibration cal_{};
  bool initialized_{false};
};

}  // namespace node::drivers
