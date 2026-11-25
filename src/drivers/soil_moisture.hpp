// 静電容量式の土壌水分センサー（アナログ出力を ADC で読む）のドライバ。
//
// 出典：データシートのある部品ではない（汎用の静電容量式モジュール）。換算は P3 の決定（open-questions #43）：
// ADC の生値を「乾いた値＝0%、湿った値＝100%」で直線に換算し、0〜100 に収める。
// 既定の校正値（乾 3000・湿 1300、12 ビット）は**仮の値**で、実機フェーズで校正する。
#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "hal/hal.hpp"

namespace node::drivers {

/// @brief 校正値。
struct SoilCalibration {
  std::uint16_t dry_raw{3000};  ///< 乾いた土（0%）の生値。仮の値（実機フェーズで校正）
  std::uint16_t wet_raw{1300};  ///< 湿った土（100%）の生値。仮の値（実機フェーズで校正）
};

/// @brief 生値を % の 1/1000 単位にする（0〜100000 に収める。四捨五入）。
[[nodiscard]] constexpr std::int32_t soil_raw_to_milli_pct(std::uint16_t raw, SoilCalibration cal) {
  const std::int64_t dry = cal.dry_raw;
  const std::int64_t wet = cal.wet_raw;
  if (dry == wet) {
    return 0;
  }
  const std::int64_t num = (dry - static_cast<std::int64_t>(raw)) * 100000;
  const std::int64_t den = dry - wet;
  // 符号を合わせて四捨五入する
  std::int64_t v = (num >= 0) == (den > 0) ? (num + den / 2) / den : (num - den / 2) / den;
  if (v < 0) {
    v = 0;
  }
  if (v > 100000) {
    v = 100000;
  }
  return static_cast<std::int32_t>(v);
}

/// @brief 土壌水分センサーのドライバ。
class SoilMoisture final : public domain::ISensor {
 public:
  SoilMoisture(hal::IAdc& adc, hal::Pin adc_channel, SoilCalibration cal,
               std::span<const domain::ChannelSpec, 1> channel, std::chrono::milliseconds period)
      : adc_(adc), adc_channel_(adc_channel), cal_(cal), channel_(channel), period_(period) {}

  [[nodiscard]] std::string_view id() const override { return "soil_moisture"; }
  support::Result<void> init() override { return support::ok(); }
  support::Result<std::size_t> read(std::span<domain::Reading> out) override {
    if (out.empty()) {
      return support::make_error(support::ErrorCode::BufferFull, "reading buffer too small");
    }
    const auto raw = adc_.read_raw(adc_channel_);
    if (!raw) {
      return raw.error();
    }
    out[0] = domain::Reading{channel_[0].channel, soil_raw_to_milli_pct(raw.value(), cal_), channel_[0].unit,
                             domain::Quality::Good};
    return std::size_t{1};
  }
  [[nodiscard]] std::chrono::milliseconds period() const override { return period_; }
  [[nodiscard]] std::span<const domain::ChannelSpec> channels() const override { return channel_; }

 private:
  hal::IAdc& adc_;
  hal::Pin adc_channel_;
  SoilCalibration cal_;
  std::span<const domain::ChannelSpec, 1> channel_;
  std::chrono::milliseconds period_;
};

}  // namespace node::drivers
