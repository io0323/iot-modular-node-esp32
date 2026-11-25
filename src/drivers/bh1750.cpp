// 出典：ROHM「BH1750FVI Technical Note」2011.11 - Rev.D。
#include "drivers/bh1750.hpp"

namespace node::drivers {

support::Result<void> Bh1750::command(std::uint8_t opcode) const {
  // 10 ページ 3) Write Format：命令は1つずつ送り、命令ごとに STOP を入れる
  const std::array<std::uint8_t, 1> tx{opcode};
  hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
  return t.write(dev_.addr, tx);
}

support::Result<void> Bh1750::init() {
  // 4 ページ Measurement Procedure：電源投入後は Power Down。Power On → 計測の命令
  initialized_ = false;
  if (auto r = command(kPowerOn); !r) {
    return r;
  }
  if (auto r = command(kContinuousHRes); !r) {
    return r;
  }
  timer_.delay_ms(kMeasureMaxMs);  // 最初の結果まで待つ（7 ページ ex1 ②）
  initialized_ = true;
  return support::ok();
}

support::Result<std::size_t> Bh1750::read(std::span<domain::Reading> out) {
  if (out.empty()) {
    return support::make_error(support::ErrorCode::BufferFull, "reading buffer too small");
  }
  if (!initialized_) {
    if (auto r = init(); !r) {
      return r.error();
    }
  }
  // 10 ページ 4) Read Format：High Byte [15:8]、Low Byte [7:0] の順に読む
  std::array<std::uint8_t, 2> rx{};
  {
    hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
    if (auto r = t.write_read(dev_.addr, {}, rx); !r) {
      return r.error();
    }
  }
  const auto count = static_cast<std::uint16_t>((static_cast<unsigned>(rx[0]) << 8U) | rx[1]);
  out[0] = domain::Reading{channel_[0].channel, bh1750_count_to_milli_lux(count), channel_[0].unit,
                           domain::Quality::Good};
  return std::size_t{1};
}

support::Result<void> Bh1750::request_recovery() {
  initialized_ = false;
  hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
  return t.recover();
}

}  // namespace node::drivers
