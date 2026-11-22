#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cmath>

#include "drivers/bme280.hpp"
#include "drivers/fake/fake_devices.hpp"
#include "drivers/fake/fake_hal.hpp"

namespace node::drivers {
namespace {

using namespace std::chrono_literals;
using fake::FakeBme280Device;

constexpr std::array<domain::ChannelSpec, 3> kChannels{{{"temp_air", domain::Unit::DegC, 2000},
                                                        {"humidity", domain::Unit::PctRh, 2000},
                                                        {"pressure", domain::Unit::HPa, 2000}}};

/// BMP280 データシート（BST-BMP280-DS001-26 rev 1.26）3.12 節の計算例の補正係数
Bme280Calibration bmp280_example() {
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
  return k;
}

class Bme280Test : public ::testing::Test {
 protected:
  fake::FakeI2cBus bus;
  fake::FakeMutex mutex;
  fake::FakeTimer timer;
  FakeBme280Device device;
  Bme280 sensor{hal::I2cDevice{&bus, &mutex, 0x76}, timer, kChannels, 2000ms};
  std::array<domain::Reading, 3> out{};

  void SetUp() override { ASSERT_TRUE(bus.attach(0x76, device)); }
};

// ---- 補正式（4.2.3 節の整数の式）をデータシートの計算例と照合する

TEST(Bme280Compensation, MatchesBmp280DatasheetExampleForTemperatureAndPressure) {
  // 3.12 節：UT 519888、UP 415148 → integer result 2508（1/100 °C）、int64 result 25767236（1/256 Pa）、
  // t_fine 128422
  const Bme280Compensated c = compensate_bme280(bmp280_example(), Bme280Raw{519888, 415148, 0});
  EXPECT_EQ(c.t_fine, 128422);
  EXPECT_EQ(c.temperature_centi, 2508);
  // 図の注記 (**)「整数の計算の結果は丸め誤差で図の値から少しずれることがある」。データシートの整数の式
  // （4.2.3 節）を Python で別に計算した値は 25767233 で、図の 25767236 との差は 3/256 Pa（0.012 Pa）
  EXPECT_EQ(c.pressure_q24_8, 25767233U);
  EXPECT_NEAR(static_cast<double>(c.pressure_q24_8), 25767236.0, 4.0);
}

TEST(Bme280Compensation, HumidityMatchesDoubleFormulaWithinTolerance) {
  // BME280 データシートには湿度の計算例がないので、8.1 節の倍精度の式と照合する。
  // 許容差：整数の式は Q22.10（1/1024 %RH）で、途中の切り捨てを含めて 0.05 %RH 以内
  Bme280Calibration k = bmp280_example();
  k.dig_H1 = 75;
  k.dig_H2 = 362;
  k.dig_H3 = 0;
  k.dig_H4 = 313;
  k.dig_H5 = 50;
  k.dig_H6 = 30;
  for (std::int32_t adc_H : {20000, 25000, 30000, 35000, 40000}) {
    const Bme280Compensated c = compensate_bme280(k, Bme280Raw{519888, 415148, adc_H});
    const double expected = FakeBme280Device::humidity_double(k, adc_H, c.t_fine);
    EXPECT_NEAR(static_cast<double>(c.humidity_q22_10) / 1024.0, expected, 0.05) << "adc_H " << adc_H;
  }
}

TEST(Bme280Compensation, PressureMatchesDoubleFormula) {
  // 3.12 節の図の倍精度の結果は 100653.27 Pa。図は t_fine を整数に切り捨てずに計算しており（切り捨てないと
  // 100653.2668）、8.1 節の式（t_fine を BME280_S32_t に切り捨てる）では 100653.2581 になる。許容差 0.02 Pa
  const Bme280Calibration k = bmp280_example();
  std::int32_t t_fine = 0;
  const double t = FakeBme280Device::temperature_double(k, 519888, &t_fine);
  EXPECT_NEAR(t, 25.08, 0.005);
  EXPECT_NEAR(FakeBme280Device::pressure_double(k, 415148, t_fine), 100653.27, 0.02);
  const Bme280Compensated c = compensate_bme280(k, Bme280Raw{519888, 415148, 0});
  EXPECT_NEAR(static_cast<double>(c.pressure_q24_8) / 256.0, 100653.27, 0.05);
}

TEST(Bme280Compensation, ParsesCalibrationIncludingPackedH4H5) {
  // 表16：dig_H4 = 0xE4[7:0] / 0xE5[3:0]、dig_H5 = 0xE5[7:4] / 0xE6[7:0]、12 ビットの符号付き
  std::array<std::uint8_t, 26> c{};
  c[0] = 0x70;  // dig_T1 = 0x6B70 = 27504
  c[1] = 0x6B;
  c[4] = 0x18;  // dig_T3 = 0xFC18 = -1000
  c[5] = 0xFC;
  c[25] = 75;  // dig_H1（0xA1）
  const std::array<std::uint8_t, 7> h{0x6A, 0x01, 0x00, 0x13, 0x29, 0x03, 0x1E};
  const Bme280Calibration k = parse_bme280_calibration(c, h);
  EXPECT_EQ(k.dig_T1, 27504);
  EXPECT_EQ(k.dig_T3, -1000);
  EXPECT_EQ(k.dig_H1, 75);
  EXPECT_EQ(k.dig_H2, 362);
  EXPECT_EQ(k.dig_H4, 0x13 * 16 + 0x9);  // 313
  EXPECT_EQ(k.dig_H5, 0x03 * 16 + 0x2);  // 50
  EXPECT_EQ(k.dig_H6, 30);
  // 負の値
  const std::array<std::uint8_t, 7> neg{0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
  const Bme280Calibration n = parse_bme280_calibration(c, neg);
  EXPECT_EQ(n.dig_H4, -1);
  EXPECT_EQ(n.dig_H5, -1);
  EXPECT_EQ(n.dig_H6, -1);
}

TEST(Bme280Compensation, AssemblesRawValuesFromBurstRead) {
  // 0xF7 press_msb up[19:12]、0xF8 up[11:4]、0xF9[7:4] up[3:0]、0xFA〜0xFC temp、0xFD〜0xFE hum
  const std::array<std::uint8_t, 8> d{0x65, 0x5A, 0xC0, 0x7E, 0xED, 0x00, 0x75, 0x30};
  const Bme280Raw r = parse_bme280_raw(d);
  EXPECT_EQ(r.adc_P, 415148);  // 0x655AC
  EXPECT_EQ(r.adc_T, 519888);  // 0x7EED0
  EXPECT_EQ(r.adc_H, 30000);   // 0x7530
}

// ---- ドライバ（フェイクの I2C 機器で）

TEST_F(Bme280Test, InitWritesRegistersInDatasheetOrder) {
  ASSERT_TRUE(sensor.init());
  const auto w = device.writes();
  ASSERT_EQ(w.size(), 4U);
  EXPECT_EQ(w[0].reg, 0xE0);  // ソフトリセット 0xB6（5.4.2 節）
  EXPECT_EQ(w[0].value, 0xB6);
  EXPECT_EQ(w[1].reg, 0xF2);  // ctrl_hum：osrs_h ×1（ctrl_meas より先。5.4.3 節）
  EXPECT_EQ(w[1].value, 0x01);
  EXPECT_EQ(w[2].reg, 0xF5);  // config：フィルタなし（sleep の間に書く。5.4.6 節）
  EXPECT_EQ(w[2].value, 0x00);
  EXPECT_EQ(w[3].reg, 0xF4);  // ctrl_meas：osrs_t ×1、osrs_p ×1、sleep
  EXPECT_EQ(w[3].value, 0x24);
  EXPECT_GE(timer.total_delay_ms(), 2U);  // 起動時間 2 ms（表1）
  EXPECT_EQ(sensor.calibration().dig_T1, device.calibration().dig_T1);
  EXPECT_EQ(sensor.calibration().dig_H5, device.calibration().dig_H5);
}

TEST_F(Bme280Test, ChipIdMismatchIsAnError) {
  device.set_chip_id(0x58);  // BMP280 の chip_id（BME280 表17）
  const auto r = sensor.init();
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, support::ErrorCode::InvalidArgument);
}

TEST_F(Bme280Test, ReadTriggersForcedModeWaitsForMeasuringBitAndConverts) {
  ASSERT_TRUE(sensor.init());
  device.set_calibration(bmp280_example());
  ASSERT_TRUE(sensor.init());  // 係数を読み直す
  device.set_raw(Bme280Raw{519888, 415148, 30000});
  device.set_measuring_polls(3);
  const auto before = timer.total_delay_ms();
  const auto n = sensor.read(out);
  ASSERT_TRUE(n);
  EXPECT_EQ(n.value(), 3U);
  EXPECT_EQ(device.writes().back().reg, 0xF4);  // 強制モード（mode = 01）
  EXPECT_EQ(device.writes().back().value, 0x25);
  EXPECT_EQ(device.status_reads_while_measuring(), 3U);  // measuring が下りるまで読み直した
  EXPECT_GE(timer.total_delay_ms() - before, 10U);       // 計測時間の最大 9.3 ms（9.1 節）を待つ
  EXPECT_EQ(out[0].channel, "temp_air");
  EXPECT_EQ(out[0].value_milli, 25080);  // 2508 × 10
  EXPECT_EQ(out[2].channel, "pressure");
  EXPECT_EQ(out[2].value_milli, 1006533);  // 25767233 / 256 Pa = 1006.5325 hPa（四捨五入）
  EXPECT_EQ(out[1].channel, "humidity");
  EXPECT_EQ(out[0].quality, domain::Quality::Good);
}

TEST_F(Bme280Test, MeasuringBitThatNeverClearsIsAnError) {
  ASSERT_TRUE(sensor.init());
  device.set_measuring_polls(Bme280::kStatusPolls + 1);
  EXPECT_FALSE(sensor.read(out));
}

TEST_F(Bme280Test, I2cTimeoutIsAnErrorAndUses50msPerTransfer) {
  ASSERT_TRUE(sensor.init());
  EXPECT_EQ(bus.last_timeout_ms(), 50U);      // 1回の通信のタイムアウト（#48）
  EXPECT_EQ(mutex.last_timeout_ms(), 100U);   // ミューテックスの取得（#49）
  bus.set_timeout(0x76, true);
  const auto r = sensor.read(out);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, support::ErrorCode::IoError);
  EXPECT_FALSE(mutex.locked());  // 失敗しても放す（RAII）
}

TEST_F(Bme280Test, BusLockTimeoutFailsTheMeasurement) {
  ASSERT_TRUE(sensor.init());
  mutex.set_always_busy(true);
  const auto r = sensor.read(out);
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, support::ErrorCode::Busy);
}

TEST_F(Bme280Test, RecoveryRecoversTheBusAndReinitializes) {
  ASSERT_TRUE(sensor.init());
  bus.hang();
  EXPECT_FALSE(sensor.read(out));
  ASSERT_TRUE(sensor.request_recovery());
  EXPECT_EQ(bus.recover_count(), 1U);
  EXPECT_TRUE(sensor.read(out));  // 回復の後は init からやり直して読める
}

}  // namespace
}  // namespace node::drivers
