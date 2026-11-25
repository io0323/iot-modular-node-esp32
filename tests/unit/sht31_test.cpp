// SHT31（SHT3x-DIS）のドライバの単体テスト（P8a）。
// 出典：Sensirion「Datasheet SHT3x-DIS」December 2022 - Version 7。
#include <gtest/gtest.h>

#include <array>
#include <chrono>

#include "drivers/fake/fake_hal.hpp"
#include "drivers/fake/fake_sht31.hpp"
#include "drivers/sht31.hpp"

namespace node::drivers {
namespace {

using namespace std::chrono_literals;

TEST(Sht31Crc, MatchesDatasheetExample) {
  EXPECT_EQ(sht31_crc8(0xBE, 0xEF), 0x92);  // 4.12 節 表20
  EXPECT_EQ(sht31_crc8(0x00, 0x00), 0x81);  // 初期値 0xFF・多項式 0x31 で計算した値
}

TEST(Sht31Conversion, BoundariesOfRawValues) {
  // 4.13 節：T = −45 + 175 × S / 65535、RH = 100 × S / 65535
  EXPECT_EQ(sht31_raw_to_milli_c(0), -45000);
  EXPECT_EQ(sht31_raw_to_milli_c(65535), 130000);
  EXPECT_EQ(sht31_raw_to_milli_rh(0), 0);
  EXPECT_EQ(sht31_raw_to_milli_rh(65535), 100000);
  // 中間：S = 0x6666（26214）→ −45 + 175 × 0.4 = 24.99962… ℃ → 25000 m℃（四捨五入）
  EXPECT_EQ(sht31_raw_to_milli_c(0x6666), 25000);
  // S = 0x8000（32768）→ 50.00076… %RH → 50001
  EXPECT_EQ(sht31_raw_to_milli_rh(0x8000), 50001);
}

class Sht31Test : public ::testing::Test {
 protected:
  static constexpr std::array<domain::ChannelSpec, 2> kChannels{{
      {"temp_aux", domain::Unit::DegC, 2000},
      {"humidity_aux", domain::Unit::PctRh, 2000},
  }};
  fake::FakeI2cBus bus;
  fake::FakeMutex mutex;
  fake::FakeTimer timer;
  fake::FakeSht31Device device{timer};
  Sht31 sensor{hal::I2cDevice{&bus, &mutex, Sht31::kAddrA}, timer, kChannels, 2000ms};
  std::array<domain::Reading, 2> out{};

  void SetUp() override { ASSERT_TRUE(bus.attach(Sht31::kAddrA, device)); }
};

TEST_F(Sht31Test, InitSendsSoftResetAndWaitsResetTime) {
  ASSERT_TRUE(sensor.init());
  ASSERT_EQ(device.commands().size(), 1U);
  EXPECT_EQ(device.commands()[0], 0x30A2);  // 4.9 節 表14
  EXPECT_EQ(timer.total_delay_ms(), 2U);    // tSR の最大 1.5 ms を切り上げ（2.2 節 表4）
}

TEST_F(Sht31Test, ReadsSingleShotHighRepeatabilityWithoutClockStretching) {
  device.set_raw_temperature(0x6666);
  device.set_raw_humidity(0x8000);
  const auto r = sensor.read(out);
  ASSERT_TRUE(r);
  ASSERT_EQ(r.value(), 2U);
  // 初めての読みでは init（ソフトリセット）してから計測の命令 0x2400（4.3 節 表9）
  ASSERT_EQ(device.commands().size(), 2U);
  EXPECT_EQ(device.commands()[0], 0x30A2);
  EXPECT_EQ(device.commands()[1], 0x2400);
  // 計測時間の最大 15 ms だけ待ってから読む（計測が終わる前の読み出しはない）
  EXPECT_EQ(timer.total_delay_ms(), 2U + 15U);
  EXPECT_EQ(device.early_reads(), 0U);
  EXPECT_EQ(out[0].channel, "temp_aux");
  EXPECT_EQ(out[0].value_milli, 25000);
  EXPECT_EQ(out[0].unit, domain::Unit::DegC);
  EXPECT_EQ(out[0].quality, domain::Quality::Good);
  EXPECT_EQ(out[1].channel, "humidity_aux");
  EXPECT_EQ(out[1].value_milli, 50001);
  EXPECT_EQ(out[1].unit, domain::Unit::PctRh);
  // 2回目からは計測の命令だけ
  ASSERT_TRUE(sensor.read(out));
  ASSERT_EQ(device.commands().size(), 3U);
  EXPECT_EQ(device.commands()[2], 0x2400);
}

TEST_F(Sht31Test, RawBoundariesAreConvertedEndToEnd) {
  device.set_raw_temperature(0);
  device.set_raw_humidity(0);
  ASSERT_TRUE(sensor.read(out));
  EXPECT_EQ(out[0].value_milli, -45000);
  EXPECT_EQ(out[1].value_milli, 0);
  device.set_raw_temperature(65535);
  device.set_raw_humidity(65535);
  ASSERT_TRUE(sensor.read(out));
  EXPECT_EQ(out[0].value_milli, 130000);
  EXPECT_EQ(out[1].value_milli, 100000);
}

TEST_F(Sht31Test, CrcMismatchFailsTheMeasurement) {
  ASSERT_TRUE(sensor.read(out));
  device.set_crc_error(true);
  const auto r = sensor.read(out);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, support::ErrorCode::IoError);
  device.set_crc_error(false);
  EXPECT_TRUE(sensor.read(out));
}

TEST_F(Sht31Test, NoResponseIsAnError) {
  bus.set_nack(Sht31::kAddrA, true);
  EXPECT_FALSE(sensor.init());
  EXPECT_FALSE(sensor.read(out));
  bus.set_nack(Sht31::kAddrA, false);
  EXPECT_TRUE(sensor.read(out));
}

TEST_F(Sht31Test, SmallBufferIsAnError) {
  std::array<domain::Reading, 1> small{};
  EXPECT_FALSE(sensor.read(small));
}

TEST_F(Sht31Test, RecoveryRecoversTheBusAndReinitializesOnNextRead) {
  ASSERT_TRUE(sensor.read(out));
  ASSERT_TRUE(sensor.request_recovery());
  EXPECT_EQ(bus.recover_count(), 1U);
  ASSERT_TRUE(sensor.read(out));
  ASSERT_EQ(device.commands().size(), 4U);  // 0x30A2・0x2400・（回復後）0x30A2・0x2400
  EXPECT_EQ(device.commands()[2], 0x30A2);
}

}  // namespace
}  // namespace node::drivers
