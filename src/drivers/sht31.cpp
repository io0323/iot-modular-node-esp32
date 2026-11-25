// 出典：Sensirion「Datasheet SHT3x-DIS」December 2022 - Version 7。
#include "drivers/sht31.hpp"

namespace node::drivers {

support::Result<void> Sht31::init() {
  initialized_ = false;
  {
    hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
    if (auto r = t.write(dev_.addr, kSoftReset); !r) {
      return r;
    }
  }
  // tSR の間は次の命令を受けない（2.2 節 表4）。命令の間の最小の待ち 1 ms（4 章）もこれで満たす
  timer_.delay_ms(kSoftResetMaxMs);
  initialized_ = true;
  return support::ok();
}

support::Result<std::size_t> Sht31::read(std::span<domain::Reading> out) {
  if (out.size() < channels_.size()) {
    return support::make_error(support::ErrorCode::BufferFull, "reading buffer too small");
  }
  if (!initialized_) {
    if (auto r = init(); !r) {
      return r.error();
    }
  }
  // 4.2 節・4.3 節：書き込みのヘッダと 16 ビットの命令で計測が始まる
  {
    hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
    if (auto r = t.write(dev_.addr, kMeasureHighNoStretch); !r) {
      return r.error();
    }
  }
  // クロックストレッチなしでは、計測が終わる前の読み出しのヘッダに NACK が返る（4.4 節）ので、計測時間だけ待つ
  timer_.delay_ms(kMeasureMaxMs);
  // 4.4 節：温度 2 バイト・CRC、相対湿度 2 バイト・CRC の順
  std::array<std::uint8_t, 6> rx{};
  {
    hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
    if (auto r = t.write_read(dev_.addr, {}, rx); !r) {
      return r.error();
    }
  }
  if (sht31_crc8(rx[0], rx[1]) != rx[2] || sht31_crc8(rx[3], rx[4]) != rx[5]) {
    return support::make_error(support::ErrorCode::IoError, "sht31 crc mismatch");
  }
  const auto raw_t = static_cast<std::uint16_t>((static_cast<unsigned>(rx[0]) << 8U) | rx[1]);
  const auto raw_rh = static_cast<std::uint16_t>((static_cast<unsigned>(rx[3]) << 8U) | rx[4]);
  out[0] = domain::Reading{channels_[0].channel, sht31_raw_to_milli_c(raw_t), channels_[0].unit, domain::Quality::Good};
  out[1] = domain::Reading{channels_[1].channel, sht31_raw_to_milli_rh(raw_rh), channels_[1].unit,
                           domain::Quality::Good};
  return std::size_t{2};
}

support::Result<void> Sht31::request_recovery() {
  initialized_ = false;
  hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
  return t.recover();
}

}  // namespace node::drivers
