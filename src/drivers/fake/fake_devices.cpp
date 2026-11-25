#include "drivers/fake/fake_devices.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "drivers/font5x7.hpp"
#include "drivers/ssd1306.hpp"

namespace node::drivers::fake {

// ---------------- BME280（BST-BME280-DS001-24）

FakeBme280Device::FakeBme280Device() {
  reset();
  // 既定：BMP280 データシート 3.12 節の計算例（dig_T1 27504 … dig_P9 6000、UT 519888、UP 415148）
  Bme280Calibration k{};
  k.dig_T1 = 27504;
  k.dig_T2 = 26435;
  k.dig_T3 = -1000;
  k.dig_P1 = 36477;
  k.dig_P2 = -10685;
  k.dig_P3 = 3024;
  k.dig_P4 = 2855;
  k.dig_P5 = 140;
  k.dig_P6 = -7;
  k.dig_P7 = 15500;
  k.dig_P8 = -14600;
  k.dig_P9 = 6000;
  k.dig_H1 = 75;
  k.dig_H2 = 362;
  k.dig_H3 = 0;
  k.dig_H4 = 313;
  k.dig_H5 = 50;
  k.dig_H6 = 30;
  set_calibration(k);
  raw_ = Bme280Raw{519888, 415148, 30000};
}

void FakeBme280Device::reset() {
  // 5.3 節 表18 のリセット状態（補正係数の領域は NVM なので変えない）
  regs_[Bme280::kRegId] = Bme280::kChipId;
  regs_[Bme280::kRegCtrlHum] = 0x00;
  regs_[Bme280::kRegStatus] = 0x00;
  regs_[Bme280::kRegCtrlMeas] = 0x00;
  regs_[Bme280::kRegConfig] = 0x00;
  regs_[0xF7] = 0x80;
  regs_[0xF8] = 0x00;
  regs_[0xF9] = 0x00;
  regs_[0xFA] = 0x80;
  regs_[0xFB] = 0x00;
  regs_[0xFC] = 0x00;
  regs_[0xFD] = 0x80;
  regs_[0xFE] = 0x00;
  measuring_left_ = 0;
}

void FakeBme280Device::set_calibration(const Bme280Calibration& k) {
  cal_ = k;
  const auto put16 = [&](std::size_t a, std::uint16_t v) {
    regs_[a] = static_cast<std::uint8_t>(v & 0xFFU);
    regs_[a + 1] = static_cast<std::uint8_t>(v >> 8U);
  };
  put16(0x88, k.dig_T1);
  put16(0x8A, static_cast<std::uint16_t>(k.dig_T2));
  put16(0x8C, static_cast<std::uint16_t>(k.dig_T3));
  put16(0x8E, k.dig_P1);
  put16(0x90, static_cast<std::uint16_t>(k.dig_P2));
  put16(0x92, static_cast<std::uint16_t>(k.dig_P3));
  put16(0x94, static_cast<std::uint16_t>(k.dig_P4));
  put16(0x96, static_cast<std::uint16_t>(k.dig_P5));
  put16(0x98, static_cast<std::uint16_t>(k.dig_P6));
  put16(0x9A, static_cast<std::uint16_t>(k.dig_P7));
  put16(0x9C, static_cast<std::uint16_t>(k.dig_P8));
  put16(0x9E, static_cast<std::uint16_t>(k.dig_P9));
  regs_[0xA1] = k.dig_H1;
  put16(0xE1, static_cast<std::uint16_t>(k.dig_H2));
  regs_[0xE3] = k.dig_H3;
  // 表16：0xE4 / 0xE5[3:0] = dig_H4 [11:4] / [3:0]、0xE5[7:4] / 0xE6 = dig_H5 [3:0] / [11:4]
  const auto h4 = static_cast<std::uint16_t>(k.dig_H4);
  const auto h5 = static_cast<std::uint16_t>(k.dig_H5);
  regs_[0xE4] = static_cast<std::uint8_t>((h4 >> 4U) & 0xFFU);
  regs_[0xE5] = static_cast<std::uint8_t>(((h5 & 0x0FU) << 4U) | (h4 & 0x0FU));
  regs_[0xE6] = static_cast<std::uint8_t>((h5 >> 4U) & 0xFFU);
  regs_[0xE7] = static_cast<std::uint8_t>(k.dig_H6);
}

double FakeBme280Device::temperature_double(const Bme280Calibration& k, std::int32_t adc_T, std::int32_t* t_fine) {
  // 8.1 節 BME280_compensate_T_double
  const double var1 = (static_cast<double>(adc_T) / 16384.0 - static_cast<double>(k.dig_T1) / 1024.0) *
                      static_cast<double>(k.dig_T2);
  const double d = static_cast<double>(adc_T) / 131072.0 - static_cast<double>(k.dig_T1) / 8192.0;
  const double var2 = d * d * static_cast<double>(k.dig_T3);
  if (t_fine != nullptr) {
    *t_fine = static_cast<std::int32_t>(var1 + var2);
  }
  return (var1 + var2) / 5120.0;
}

double FakeBme280Device::pressure_double(const Bme280Calibration& k, std::int32_t adc_P, std::int32_t t_fine) {
  // 8.1 節 BME280_compensate_P_double
  double var1 = static_cast<double>(t_fine) / 2.0 - 64000.0;
  double var2 = var1 * var1 * static_cast<double>(k.dig_P6) / 32768.0;
  var2 = var2 + var1 * static_cast<double>(k.dig_P5) * 2.0;
  var2 = (var2 / 4.0) + (static_cast<double>(k.dig_P4) * 65536.0);
  var1 = (static_cast<double>(k.dig_P3) * var1 * var1 / 524288.0 + static_cast<double>(k.dig_P2) * var1) / 524288.0;
  var1 = (1.0 + var1 / 32768.0) * static_cast<double>(k.dig_P1);
  if (var1 == 0.0) {
    return 0;
  }
  double p = 1048576.0 - static_cast<double>(adc_P);
  p = (p - (var2 / 4096.0)) * 6250.0 / var1;
  var1 = static_cast<double>(k.dig_P9) * p * p / 2147483648.0;
  var2 = p * static_cast<double>(k.dig_P8) / 32768.0;
  return p + (var1 + var2 + static_cast<double>(k.dig_P7)) / 16.0;
}

double FakeBme280Device::humidity_double(const Bme280Calibration& k, std::int32_t adc_H, std::int32_t t_fine) {
  // 8.1 節 bme280_compensate_H_double
  double var_H = static_cast<double>(t_fine) - 76800.0;
  var_H = (static_cast<double>(adc_H) -
           (static_cast<double>(k.dig_H4) * 64.0 + static_cast<double>(k.dig_H5) / 16384.0 * var_H)) *
          (static_cast<double>(k.dig_H2) / 65536.0 *
           (1.0 + static_cast<double>(k.dig_H6) / 67108864.0 * var_H *
                      (1.0 + static_cast<double>(k.dig_H3) / 67108864.0 * var_H)));
  var_H = var_H * (1.0 - static_cast<double>(k.dig_H1) * var_H / 524288.0);
  if (var_H > 100.0) {
    var_H = 100.0;
  } else if (var_H < 0.0) {
    var_H = 0.0;
  }
  return var_H;
}

void FakeBme280Device::set_temperature_c(double temp_c) {
  // 温度は adc_T について単調に増える（dig_T2 > 0 の係数）ので二分探索で求める
  std::int32_t lo = 0;
  std::int32_t hi = (1 << 20) - 1;
  while (lo < hi) {
    const std::int32_t mid = lo + (hi - lo) / 2;
    if (temperature_double(cal_, mid) < temp_c) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  raw_.adc_T = lo;
}

void FakeBme280Device::complete_measurement() {
  ++measurements_;
  const auto u20 = [&](std::size_t a, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    regs_[a] = static_cast<std::uint8_t>((u >> 12U) & 0xFFU);
    regs_[a + 1] = static_cast<std::uint8_t>((u >> 4U) & 0xFFU);
    regs_[a + 2] = static_cast<std::uint8_t>((u & 0x0FU) << 4U);
  };
  u20(0xF7, raw_.adc_P);
  u20(0xFA, raw_.adc_T);
  regs_[0xFD] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(raw_.adc_H) >> 8U) & 0xFFU);
  regs_[0xFE] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(raw_.adc_H) & 0xFFU);
  regs_[Bme280::kRegCtrlMeas] = static_cast<std::uint8_t>(regs_[Bme280::kRegCtrlMeas] & 0xFCU);  // sleep に戻る（3.3.3 節）
}

void FakeBme280Device::on_write(std::span<const std::uint8_t> data) {
  if (data.empty()) {
    return;
  }
  pointer_ = data[0];
  // 6.2.1 節：アドレスと値の組を続けて書ける
  for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
    const std::uint8_t reg = data[i];
    const std::uint8_t value = data[i + 1];
    (void)writes_.push_back(Write{reg, value});
    if (reg == Bme280::kRegReset) {
      if (value == Bme280::kResetWord) {
        reset();
      }
      continue;
    }
    regs_[reg] = value;
    if (reg == Bme280::kRegCtrlMeas && ((value & 0x03U) == 0x01U || (value & 0x03U) == 0x02U)) {
      measuring_left_ = measuring_polls_;
      if (measuring_left_ == 0) {
        complete_measurement();
      }
    }
  }
}

void FakeBme280Device::on_read(std::span<std::uint8_t> out) {
  for (std::uint8_t& b : out) {
    if (pointer_ == Bme280::kRegStatus) {
      b = measuring_left_ > 0 ? Bme280::kStatusMeasuring : 0x00;
      if (measuring_left_ > 0) {
        ++status_reads_busy_;
        if (--measuring_left_ == 0) {
          complete_measurement();
        }
      }
    } else {
      b = regs_[pointer_];
    }
    pointer_ = static_cast<std::uint8_t>(pointer_ + 1);
  }
}

// ---------------- BH1750FVI（ROHM Technical Note 2011.11 Rev.D）

void FakeBh1750Device::on_write(std::span<const std::uint8_t> data) {
  for (std::uint8_t const op : data) {
    (void)commands_.push_back(op);
    if (op == 0x00) {  // Power Down
      powered_ = false;
      continuous_h_ = false;
    } else if (op == 0x01) {  // Power On
      powered_ = true;
    } else if (op == 0x10) {  // Continuously H-Resolution Mode
      powered_ = true;
      continuous_h_ = true;
      mode_set_ms_ = timer_.now_ms();
    }
  }
}

void FakeBh1750Device::on_read(std::span<std::uint8_t> out) {
  std::uint16_t v = count_;
  if (!continuous_h_ || timer_.now_ms() - mode_set_ms_ < kTypicalMeasureMs) {
    ++early_reads_;
    v = 0;
  }
  if (!out.empty()) {
    out[0] = static_cast<std::uint8_t>(v >> 8U);  // High Byte [15:8]
  }
  if (out.size() > 1) {
    out[1] = static_cast<std::uint8_t>(v & 0xFFU);  // Low Byte [7:0]
  }
}

void FakeBh1750Device::set_lux(double lux) {
  const double c = std::round(lux * 1.2);
  count_ = static_cast<std::uint16_t>(std::clamp(c, 0.0, 65535.0));
}

// ---------------- SSD1306（Rev 1.1）

namespace {
// 引数の数（9 章 表9-1）。ここで使うものだけ
// コマンドの後に続く引数のバイト数（表にないコマンドは 0）
constexpr std::array<std::pair<std::uint8_t, std::uint8_t>, 11> kParamCounts{{
    {0x81, 1}, {0x8D, 1}, {0xA8, 1}, {0xD3, 1}, {0xD5, 1}, {0xD9, 1}, {0xDA, 1}, {0xDB, 1}, {0x20, 1},
    {0x21, 2}, {0x22, 2},
}};

std::uint8_t params_of(std::uint8_t cmd) {
  const auto* it = std::ranges::find_if(kParamCounts, [cmd](const auto& p) { return p.first == cmd; });
  return it == kParamCounts.end() ? 0 : it->second;
}
}  // namespace

void FakeSsd1306Device::command_byte(std::uint8_t b) {
  (void)commands_.push_back(b);
  if (pending_params_ > 0) {
    --pending_params_;
    return;
  }
  pending_params_ = params_of(b);
  if (b == 0xAE) {
    display_on_ = false;
  }
  if (b == 0xAF) {
    display_on_ = true;
  }
  if (b >= 0xB0 && b <= 0xB7) {
    page_ = static_cast<std::uint8_t>(b - 0xB0);
  }
  if (b <= 0x0F) {
    column_ = static_cast<std::uint8_t>((column_ & 0xF0U) | b);
  }
  if (b >= 0x10 && b <= 0x1F) {
    column_ = static_cast<std::uint8_t>((column_ & 0x0FU) | ((b & 0x0FU) << 4U));
  }
}

void FakeSsd1306Device::on_write(std::span<const std::uint8_t> data) {
  if (data.empty()) {
    return;
  }
  const bool is_data = (data[0] & 0x40U) != 0;  // D/C#（8.1.5 節）
  for (std::size_t i = 1; i < data.size(); ++i) {
    if (!is_data) {
      command_byte(data[i]);
      continue;
    }
    gddram_[static_cast<std::size_t>(page_) * 128U + column_] = data[i];
    column_ = static_cast<std::uint8_t>((column_ + 1U) & 0x7FU);  // ページアドレッシング：列だけ進む（10.1.3 節）
  }
}

void FakeSsd1306Device::on_read(std::span<std::uint8_t> out) {
  std::ranges::fill(out, std::uint8_t{0});
}

support::FixedString<21> FakeSsd1306Device::row_text(std::size_t page) const {
  support::FixedString<21> s;
  for (std::size_t cell = 0; cell < 21; ++cell) {
    char found = '?';
    for (char c = kFontFirst; c <= kFontLast; ++c) {
      const auto& g = glyph_of(c);
      bool same = true;
      for (std::size_t x = 0; x < kFontWidth && same; ++x) {
        same = gddram_[page * 128 + cell * Ssd1306::kCellWidth + x] == g[x];
      }
      if (same) {
        found = c;
        break;
      }
    }
    (void)s.push_back(found);
  }
  // 右の空白を落とす
  std::size_t n = s.size();
  while (n > 0 && s.view()[n - 1] == ' ') {
    --n;
  }
  support::FixedString<21> t;
  (void)t.assign(s.view().substr(0, n));
  return t;
}

}  // namespace node::drivers::fake
