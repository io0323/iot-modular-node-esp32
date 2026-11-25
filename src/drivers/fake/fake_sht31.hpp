// SHT3x-DIS のフェイク（P8a。単体テストと --devices fakehw）。
//
// 出典：Sensirion「Datasheet SHT3x-DIS」December 2022 - Version 7。データシートの命令と応答（4.3 節・4.4 節・4.9 節）と
// CRC-8（4.12 節）を再現する。ヘッダだけで作る（ビルド定義に行を足さないため）。
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include "drivers/fake/fake_hal.hpp"
#include "drivers/sht31.hpp"
#include "support/static_vector.hpp"

namespace node::drivers::fake {

/// @brief SHT3x-DIS のフェイク。単発計測の命令を受け、計測時間がたった後に 6 バイトの結果を返す。
///
/// 計測の命令から tMEAS,h の typ 12.5 ms（2.2 節 表4）がたつ前の読み出しは early_reads に数え、0xFF を返す
/// （実物は NACK を返す。4.4 節）。set_crc_error で、温度の CRC を誤った値にできる。
class FakeSht31Device final : public FakeI2cDevice {
 public:
  /// @brief フェイクを作る（25 ℃・50 %RH）。
  /// @param timer 計測時間の経過を見る時計
  explicit FakeSht31Device(const hal::ITimer& timer) : timer_(timer) {
    set_temperature_c(25.0);
    set_humidity_pct(50.0);
  }

  void on_write(std::span<const std::uint8_t> data) override {
    if (data.size() != 2) {
      return;  // 命令は 16 ビット（4 章）
    }
    const auto cmd = static_cast<std::uint16_t>((static_cast<unsigned>(data[0]) << 8U) | data[1]);
    (void)commands_.push_back(cmd);
    if (cmd == 0x30A2) {  // ソフトリセット（4.9 節 表14）
      measuring_ = false;
      ready_ = false;
    } else if (cmd == 0x2400) {  // 単発計測・高い再現性・クロックストレッチなし（4.3 節 表9）
      measuring_ = true;
      ready_ = false;
      started_ms_ = timer_.now_ms();
    }
  }

  void on_read(std::span<std::uint8_t> out) override {
    if (measuring_ && timer_.now_ms() - started_ms_ >= kTypicalMeasureMs) {
      measuring_ = false;
      ready_ = true;
    }
    if (!ready_) {
      ++early_reads_;
      std::fill(out.begin(), out.end(), std::uint8_t{0xFF});
      return;
    }
    // 4.4 節：温度 2 バイト・CRC、相対湿度 2 バイト・CRC
    const auto t_msb = static_cast<std::uint8_t>(raw_t_ >> 8U);
    const auto t_lsb = static_cast<std::uint8_t>(raw_t_ & 0xFFU);
    const auto rh_msb = static_cast<std::uint8_t>(raw_rh_ >> 8U);
    const auto rh_lsb = static_cast<std::uint8_t>(raw_rh_ & 0xFFU);
    auto t_crc = sht31_crc8(t_msb, t_lsb);
    if (crc_error_) {
      t_crc = static_cast<std::uint8_t>(t_crc ^ 0xFFU);
    }
    const std::array<std::uint8_t, 6> data{t_msb, t_lsb, t_crc, rh_msb, rh_lsb, sht31_crc8(rh_msb, rh_lsb)};
    std::copy_n(data.begin(), std::min(out.size(), data.size()), out.begin());
    ready_ = false;  // 単発計測の結果は1回だけ読める
  }

  /// @brief 温度の生値を決める。
  /// @param raw S_T
  void set_raw_temperature(std::uint16_t raw) { raw_t_ = raw; }
  /// @brief 相対湿度の生値を決める。
  /// @param raw S_RH
  void set_raw_humidity(std::uint16_t raw) { raw_rh_ = raw; }
  /// @brief 温度（℃）から生値を決める（4.13 節の式の逆。四捨五入して 0〜65535 に収める）。
  /// @param temp_c 温度
  void set_temperature_c(double temp_c) { raw_t_ = to_raw((temp_c + 45.0) / 175.0); }
  /// @brief 相対湿度（%RH）から生値を決める（4.13 節の式の逆）。
  /// @param rh_pct 相対湿度
  void set_humidity_pct(double rh_pct) { raw_rh_ = to_raw(rh_pct / 100.0); }
  /// @brief 温度の CRC を誤った値にするか。
  /// @param on 誤らせるなら true
  void set_crc_error(bool on) { crc_error_ = on; }
  /// @brief 計測時間がたつ前に読まれた回数。
  /// @return 回数
  [[nodiscard]] std::uint32_t early_reads() const { return early_reads_; }
  /// @brief 受けた命令（16 ビット）。
  /// @return 受けた順の並び
  [[nodiscard]] std::span<const std::uint16_t> commands() const { return {commands_.begin(), commands_.size()}; }

 private:
  static constexpr std::uint64_t kTypicalMeasureMs = 13;  // tMEAS,h の typ 12.5 ms を切り上げ（2.2 節 表4）

  static std::uint16_t to_raw(double fraction) {
    const double v = std::round(fraction * 65535.0);
    return static_cast<std::uint16_t>(std::clamp(v, 0.0, 65535.0));
  }

  const hal::ITimer& timer_;
  std::uint16_t raw_t_{0};
  std::uint16_t raw_rh_{0};
  bool measuring_{false};
  bool ready_{false};
  bool crc_error_{false};
  std::uint64_t started_ms_{0};
  std::uint32_t early_reads_{0};
  support::StaticVector<std::uint16_t, 64> commands_{};
};

}  // namespace node::drivers::fake
