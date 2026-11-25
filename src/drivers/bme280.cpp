// 出典：Bosch Sensortec「BME280 – Data sheet」BST-BME280-DS001-24、revision 1.24、February 2024。
#include "drivers/bme280.hpp"

namespace node::drivers {
namespace {

std::uint16_t u16le(std::span<const std::uint8_t> r, std::size_t i) {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(r[i]) | static_cast<std::uint16_t>(r[i + 1] << 8U));
}
std::int16_t s16le(std::span<const std::uint8_t> r, std::size_t i) { return static_cast<std::int16_t>(u16le(r, i)); }

support::Error not_ready() { return support::make_error(support::ErrorCode::IoError, "bme280 status bit stuck"); }

}  // namespace

Bme280Calibration parse_bme280_calibration(std::span<const std::uint8_t, 26> c, std::span<const std::uint8_t, 7> h) {
  // 4.2.2 節 表16：0x88/0x89 dig_T1 … 0x9E/0x9F dig_P9、0xA1 dig_H1、0xE1/0xE2 dig_H2、0xE3 dig_H3、
  // 0xE4 / 0xE5[3:0] dig_H4 [11:4] / [3:0]、0xE5[7:4] / 0xE6 dig_H5 [3:0] / [11:4]、0xE7 dig_H6
  Bme280Calibration k{};
  k.dig_T1 = u16le(c, 0);
  k.dig_T2 = s16le(c, 2);
  k.dig_T3 = s16le(c, 4);
  k.dig_P1 = u16le(c, 6);
  k.dig_P2 = s16le(c, 8);
  k.dig_P3 = s16le(c, 10);
  k.dig_P4 = s16le(c, 12);
  k.dig_P5 = s16le(c, 14);
  k.dig_P6 = s16le(c, 16);
  k.dig_P7 = s16le(c, 18);
  k.dig_P8 = s16le(c, 20);
  k.dig_P9 = s16le(c, 22);
  k.dig_H1 = c[25];  // 0xA1（0xA0 は使わない）
  k.dig_H2 = s16le(h, 0);
  k.dig_H3 = h[2];
  const auto e4 = static_cast<std::int8_t>(h[3]);
  const auto e6 = static_cast<std::int8_t>(h[5]);
  k.dig_H4 = static_cast<std::int16_t>(e4 * 16 + static_cast<int>(h[4] & 0x0FU));
  k.dig_H5 = static_cast<std::int16_t>(e6 * 16 + static_cast<int>(h[4] >> 4U));
  k.dig_H6 = static_cast<std::int8_t>(h[6]);
  return k;
}

Bme280Raw parse_bme280_raw(std::span<const std::uint8_t, 8> d) {
  // 表29〜31：press_msb up[19:12]、press_lsb up[11:4]、press_xlsb[7:4] up[3:0]（temp も同じ）、hum_msb uh[15:8]、hum_lsb uh[7:0]
  const auto u20 = [&](std::size_t i) {
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(d[i]) << 12U) |
                                     (static_cast<std::uint32_t>(d[i + 1]) << 4U) |
                                     (static_cast<std::uint32_t>(d[i + 2]) >> 4U));
  };
  Bme280Raw r{};
  r.adc_P = u20(0);
  r.adc_T = u20(3);
  r.adc_H = static_cast<std::int32_t>((static_cast<std::uint32_t>(d[6]) << 8U) | d[7]);
  return r;
}

Bme280Compensated compensate_bme280(const Bme280Calibration& k, const Bme280Raw& raw) {
  using S32 = std::int32_t;
  using S64 = std::int64_t;
  Bme280Compensated out{};

  // ---- 4.2.3 節 BME280_compensate_T_int32（データシートのコードのとおり）
  {
    const S32 adc_T = raw.adc_T;
    const S32 var1 = ((((adc_T >> 3) - (static_cast<S32>(k.dig_T1) << 1))) * static_cast<S32>(k.dig_T2)) >> 11;
    const S32 var2 = (((((adc_T >> 4) - static_cast<S32>(k.dig_T1)) * ((adc_T >> 4) - static_cast<S32>(k.dig_T1))) >> 12) *
                      static_cast<S32>(k.dig_T3)) >>
                     14;
    out.t_fine = var1 + var2;
    out.temperature_centi = (out.t_fine * 5 + 128) >> 8;
  }
  const S32 t_fine = out.t_fine;

  // ---- 4.2.3 節 BME280_compensate_P_int64（データシートのコードのとおり）
  {
    S64 var1 = static_cast<S64>(t_fine) - 128000;
    S64 var2 = var1 * var1 * static_cast<S64>(k.dig_P6);
    var2 = var2 + ((var1 * static_cast<S64>(k.dig_P5)) << 17);
    var2 = var2 + (static_cast<S64>(k.dig_P4) << 35);
    var1 = ((var1 * var1 * static_cast<S64>(k.dig_P3)) >> 8) + ((var1 * static_cast<S64>(k.dig_P2)) << 12);
    var1 = (((static_cast<S64>(1) << 47) + var1)) * static_cast<S64>(k.dig_P1) >> 33;
    if (var1 == 0) {
      out.pressure_q24_8 = 0;  // avoid exception caused by division by zero
    } else {
      S64 p = 1048576 - raw.adc_P;
      p = (((p << 31) - var2) * 3125) / var1;
      var1 = (static_cast<S64>(k.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
      var2 = (static_cast<S64>(k.dig_P8) * p) >> 19;
      p = ((p + var1 + var2) >> 8) + (static_cast<S64>(k.dig_P7) << 4);
      out.pressure_q24_8 = static_cast<std::uint32_t>(p);
    }
  }

  // ---- 4.2.3 節 bme280_compensate_H_int32（データシートのコードのとおり）
  {
    const S32 adc_H = raw.adc_H;
    S32 v = t_fine - static_cast<S32>(76800);
    v = (((((adc_H << 14) - (static_cast<S32>(k.dig_H4) << 20) - (static_cast<S32>(k.dig_H5) * v)) +
           static_cast<S32>(16384)) >>
          15) *
         (((((((v * static_cast<S32>(k.dig_H6)) >> 10) * (((v * static_cast<S32>(k.dig_H3)) >> 11) + static_cast<S32>(32768))) >>
              10) +
             static_cast<S32>(2097152)) *
                static_cast<S32>(k.dig_H2) +
            8192) >>
           14));
    v = (v - (((((v >> 15) * (v >> 15)) >> 7) * static_cast<S32>(k.dig_H1)) >> 4));
    v = (v < 0 ? 0 : v);
    v = (v > 419430400 ? 419430400 : v);
    out.humidity_q22_10 = static_cast<std::uint32_t>(v >> 12);
  }
  return out;
}

support::Result<void> Bme280::write_reg(std::uint8_t reg, std::uint8_t value) const {
  // 6.2.1 節：レジスタのアドレスと値の組を書く
  const std::array<std::uint8_t, 2> tx{reg, value};
  hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
  return t.write(dev_.addr, tx);
}

support::Result<void> Bme280::read_regs(std::uint8_t reg, std::span<std::uint8_t> out) const {
  // 6.2.2 節：アドレスを書いてから続けて読む（自動で増える）
  const std::array<std::uint8_t, 1> tx{reg};
  hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
  return t.write_read(dev_.addr, tx, out);
}

support::Result<void> Bme280::wait_status_clear(std::uint8_t mask) {
  for (std::uint32_t i = 0; i < kStatusPolls; ++i) {
    std::array<std::uint8_t, 1> status{};
    if (auto r = read_regs(kRegStatus, status); !r) {
      return r;
    }
    if ((status[0] & mask) == 0) {
      return support::ok();
    }
    timer_.delay_ms(kStatusPollIntervalMs);
  }
  return not_ready();
}

support::Result<void> Bme280::init() {
  initialized_ = false;
  std::array<std::uint8_t, 1> chip_id{};
  if (auto r = read_regs(kRegId, chip_id); !r) {
    return r;
  }
  if (chip_id[0] != kChipId) {
    return support::make_error(support::ErrorCode::InvalidArgument, "bme280 chip id mismatch");
  }
  if (auto r = write_reg(kRegReset, kResetWord); !r) {
    return r;
  }
  timer_.delay_ms(kStartupMs);
  if (auto r = wait_status_clear(kStatusImUpdate); !r) {
    return r;  // NVM の写しが終わるまで（表21）
  }

  std::array<std::uint8_t, 26> c{};
  std::array<std::uint8_t, 7> h{};
  if (auto r = read_regs(kRegCalib0, c); !r) {
    return r;
  }
  if (auto r = read_regs(kRegCalib26, h); !r) {
    return r;
  }
  cal_ = parse_bme280_calibration(c, h);

  // ctrl_hum は ctrl_meas を書いた後に効く（5.4.3 節）。config は sleep モードで書く（5.4.6 節）
  if (auto r = write_reg(kRegCtrlHum, kCtrlHumX1); !r) {
    return r;
  }
  if (auto r = write_reg(kRegConfig, kConfigFilterOff); !r) {
    return r;
  }
  if (auto r = write_reg(kRegCtrlMeas, kCtrlMeasSleep); !r) {
    return r;
  }
  initialized_ = true;
  return support::ok();
}

support::Result<std::size_t> Bme280::read(std::span<domain::Reading> out) {
  if (out.size() < 3) {
    return support::make_error(support::ErrorCode::BufferFull, "reading buffer too small");
  }
  if (!initialized_) {
    if (auto r = init(); !r) {
      return r.error();
    }
  }
  // 強制モードで1回計測する（3.3.3 節）。待つ間はバスのミューテックスを放す
  if (auto r = write_reg(kRegCtrlMeas, kCtrlMeasForced); !r) {
    return r.error();
  }
  timer_.delay_ms(kMeasureMaxMs);
  if (auto r = wait_status_clear(kStatusMeasuring); !r) {
    return r.error();
  }
  std::array<std::uint8_t, 8> d{};
  if (auto r = read_regs(kRegData, d); !r) {
    return r.error();  // 0xF7〜0xFE を1回で読む（4 章・4.1 節）
  }

  const Bme280Compensated c = compensate_bme280(cal_, parse_bme280_raw(d));
  // 単位の換算：0.01 degC → 1/1000 degC、Q22.10 %RH → 1/1000 %RH、Q24.8 Pa → 1/1000 hPa（四捨五入）
  const std::int64_t hum_milli = (static_cast<std::int64_t>(c.humidity_q22_10) * 1000 + 512) / 1024;
  const std::int64_t press_milli = (static_cast<std::int64_t>(c.pressure_q24_8) * 10 + 128) / 256;
  out[0] = domain::Reading{channels_[0].channel, c.temperature_centi * 10, channels_[0].unit, domain::Quality::Good};
  out[1] = domain::Reading{channels_[1].channel, static_cast<std::int32_t>(hum_milli), channels_[1].unit,
                           domain::Quality::Good};
  out[2] = domain::Reading{channels_[2].channel, static_cast<std::int32_t>(press_milli), channels_[2].unit,
                           domain::Quality::Good};
  return std::size_t{3};
}

support::Result<void> Bme280::request_recovery() {
  initialized_ = false;  // 回復の後は init からやり直す
  hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
  return t.recover();
}

}  // namespace node::drivers
