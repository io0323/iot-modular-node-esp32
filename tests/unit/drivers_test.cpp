// BH1750・土壌水分・PWM・リレー・SSD1306・ボタンのドライバのテスト。
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string_view>

#include "drivers/actuators.hpp"
#include "drivers/bh1750.hpp"
#include "drivers/button.hpp"
#include "drivers/fake/fake_devices.hpp"
#include "drivers/fake/fake_hal.hpp"
#include "drivers/soil_moisture.hpp"
#include "drivers/ssd1306.hpp"

namespace node::drivers {
namespace {

using namespace std::chrono_literals;

// ---- BH1750FVI

TEST(Bh1750, CountToLuxUsesDivideBy1_2) {
  EXPECT_EQ(bh1750_count_to_milli_lux(0), 0);
  EXPECT_EQ(bh1750_count_to_milli_lux(1), 833);           // 0.833 lx
  EXPECT_EQ(bh1750_count_to_milli_lux(65535), 54612500);  // 54612.5 lx
  // 7 ページの計算例："1000_0011_1001_0000" → (2^15+2^9+2^8+2^7+2^4) / 1.2 ≒ 28067 lx
  EXPECT_EQ(bh1750_count_to_milli_lux(0x8390), 28066667);
}

class Bh1750Test : public ::testing::Test {
 protected:
  static constexpr std::array<domain::ChannelSpec, 1> kLux{{{"lux", domain::Unit::Lux, 1000}}};
  fake::FakeI2cBus bus;
  fake::FakeMutex mutex;
  fake::FakeTimer timer;
  fake::FakeBh1750Device device{timer};
  Bh1750 sensor{hal::I2cDevice{&bus, &mutex, Bh1750::kAddrLow}, timer, kLux, 1000ms};
  std::array<domain::Reading, 1> out{};
  void SetUp() override { ASSERT_TRUE(bus.attach(Bh1750::kAddrLow, device)); }
};

TEST_F(Bh1750Test, InitPowersOnSetsContinuousHModeAndWaitsMeasurementTime) {
  ASSERT_TRUE(sensor.init());
  const auto cmds = device.commands();
  ASSERT_EQ(cmds.size(), 2U);
  EXPECT_EQ(cmds[0], 0x01);  // Power On
  EXPECT_EQ(cmds[1], 0x10);  // Continuously H-Resolution Mode
  EXPECT_EQ(timer.total_delay_ms(), 180U);  // tHR の最大
  device.set_count(0x8390);
  ASSERT_TRUE(sensor.read(out));
  EXPECT_EQ(device.early_reads(), 0U);  // 計測時間を待ってから読んだ
  EXPECT_EQ(out[0].value_milli, 28066667);
  EXPECT_EQ(out[0].channel, "lux");
}

TEST_F(Bh1750Test, I2cFailureIsAnError) {
  ASSERT_TRUE(sensor.init());
  bus.set_nack(Bh1750::kAddrLow, true);
  EXPECT_FALSE(sensor.read(out));
}

// ---- 土壌水分（#43：乾 3000 = 0%、湿 1300 = 100%、仮の値）

TEST(SoilMoisture, LinearBetweenDryAndWetAndClamped) {
  const SoilCalibration cal{3000, 1300};
  EXPECT_EQ(soil_raw_to_milli_pct(3000, cal), 0);
  EXPECT_EQ(soil_raw_to_milli_pct(1300, cal), 100000);
  EXPECT_EQ(soil_raw_to_milli_pct(2150, cal), 50000);  // 真ん中
  EXPECT_EQ(soil_raw_to_milli_pct(3500, cal), 0);       // 乾より乾いた側は 0
  EXPECT_EQ(soil_raw_to_milli_pct(1000, cal), 100000);  // 湿より湿った側は 100
  EXPECT_EQ(soil_raw_to_milli_pct(4095, cal), 0);
  EXPECT_EQ(soil_raw_to_milli_pct(0, cal), 100000);
  EXPECT_EQ(soil_raw_to_milli_pct(2320, cal), 40000);
}

TEST(SoilMoisture, ReadsAdcAndReportsAdcFailure) {
  static constexpr std::array<domain::ChannelSpec, 1> kSoil{{{"soil", domain::Unit::Pct, 10000}}};
  fake::FakeAdc adc;
  SoilMoisture s(adc, 3, SoilCalibration{}, kSoil, 10000ms);
  adc.set_raw(3, 2150);
  std::array<domain::Reading, 1> out{};
  ASSERT_TRUE(s.read(out));
  EXPECT_EQ(out[0].value_milli, 50000);
  adc.set_fault(true);
  EXPECT_FALSE(s.read(out));
}

// ---- PWM（#44・#58：最大デューティ 2^分解能 − 1）

TEST(PwmActuator, LevelToDutyAt10Bits) {
  EXPECT_EQ(level_to_duty(0, 10), 0U);
  EXPECT_EQ(level_to_duty(1, 10), 1U);
  EXPECT_EQ(level_to_duty(500, 10), 512U);
  EXPECT_EQ(level_to_duty(999, 10), 1022U);
  EXPECT_EQ(level_to_duty(1000, 10), 1023U);
  EXPECT_EQ(level_to_duty(1000, 8), 255U);
}

TEST(PwmActuator, ConfiguresFrequencyAndResolutionAndStartsAtZero) {
  static constexpr domain::ActuatorSpec kFan{"fan_main", 0, 1000, {}};
  fake::FakePwm pwm;
  PwmActuator fan(pwm, PwmConfig{2, 25000, 10}, kFan);
  ASSERT_TRUE(fan.init());
  EXPECT_EQ(pwm.channel(2).freq_hz, 25000U);
  EXPECT_EQ(pwm.channel(2).resolution_bits, 10);
  EXPECT_EQ(pwm.channel(2).duty, 0U);
  ASSERT_TRUE(fan.apply(500));
  EXPECT_EQ(pwm.channel(2).duty, 512U);
  EXPECT_EQ(fan.current(), 500);
  ASSERT_TRUE(fan.apply(0));
  EXPECT_EQ(pwm.channel(2).duty, 0U);  // level 0 は必ず出力 0
}

// ---- リレー（#46）

TEST(RelayActuator, ActiveHighAndInverted) {
  static constexpr domain::ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}};
  fake::FakeGpio gpio;
  RelayActuator high(gpio, 4, true, kPump);
  ASSERT_TRUE(high.init());
  EXPECT_EQ(gpio.mode(4), fake::FakeGpio::Mode::Output);
  EXPECT_FALSE(gpio.level(4));
  ASSERT_TRUE(high.apply(1));
  EXPECT_TRUE(gpio.level(4));  // 0 より大きければオン
  EXPECT_EQ(high.current(), 1000);
  ASSERT_TRUE(high.apply(0));
  EXPECT_FALSE(gpio.level(4));

  RelayActuator low(gpio, 5, false, kPump);
  ASSERT_TRUE(low.init());
  EXPECT_TRUE(gpio.level(5));  // オフ = High
  ASSERT_TRUE(low.apply(1000));
  EXPECT_FALSE(gpio.level(5));  // オン = Low
}

// ---- SSD1306

class Ssd1306Test : public ::testing::Test {
 protected:
  fake::FakeI2cBus bus;
  fake::FakeMutex mutex;
  fake::FakeSsd1306Device device;
  Ssd1306 display{hal::I2cDevice{&bus, &mutex, Ssd1306::kAddrSa0Low}};
  void SetUp() override { ASSERT_TRUE(bus.attach(Ssd1306::kAddrSa0Low, device)); }
};

TEST_F(Ssd1306Test, InitSendsApplicationNoteSequenceThenClearsPages) {
  ASSERT_TRUE(display.init());
  const auto cmds = device.commands();
  ASSERT_GE(cmds.size(), Ssd1306::kInitSequence.size());
  for (std::size_t i = 0; i < Ssd1306::kInitSequence.size(); ++i) {
    EXPECT_EQ(cmds[i], Ssd1306::kInitSequence[i]) << "index " << i;
  }
  // アプリケーションノート 図2 の順：MUX → オフセット → 開始行 → セグメント → COM 走査 → COM ピン → コントラスト
  // → A4 → A6 → 発振 → チャージポンプ → 表示オン
  EXPECT_EQ(cmds[0], 0xA8);
  EXPECT_EQ(cmds[15], 0x8D);
  EXPECT_EQ(cmds[16], 0x14);
  EXPECT_EQ(cmds[17], 0xAF);
  EXPECT_TRUE(device.display_on());
  // その後、8 ページそれぞれに B0+n・00・10 を送ってから書く
  EXPECT_EQ(cmds[18], 0xB0);
  EXPECT_EQ(cmds[19], 0x00);
  EXPECT_EQ(cmds[20], 0x10);
  EXPECT_EQ(cmds[21], 0xB1);
}

TEST_F(Ssd1306Test, WritesTextByPagesAndTurnsDisplayOnOff) {
  ASSERT_TRUE(display.init());
  const std::array<std::string_view, 3> lines{"AUTO   MQTT:ON", "T     25.0C", "Hello, OLED!"};
  ASSERT_TRUE(display.show(lines));
  EXPECT_EQ(device.row_text(0).view(), "AUTO   MQTT:ON");
  EXPECT_EQ(device.row_text(1).view(), "T     25.0C");
  EXPECT_EQ(device.row_text(2).view(), "Hello, OLED!");
  EXPECT_EQ(device.row_text(3).view(), "");
  // フレームバッファとフェイクの GDDRAM が一致する
  EXPECT_EQ(display.framebuffer(), device.gddram());
  ASSERT_TRUE(display.set_power(false));
  EXPECT_FALSE(device.display_on());
  EXPECT_EQ(device.commands().back(), 0xAE);
  ASSERT_TRUE(display.set_power(true));
  EXPECT_TRUE(device.display_on());
}

TEST_F(Ssd1306Test, LongLinesAreCutAt21Columns) {
  ASSERT_TRUE(display.init());
  const std::array<std::string_view, 1> lines{"0123456789012345678901234"};
  ASSERT_TRUE(display.show(lines));
  EXPECT_EQ(device.row_text(0).view(), "012345678901234567890");
}

// ---- ボタン（#41・#42）

class ButtonTest : public ::testing::Test {
 protected:
  fake::FakeGpio gpio;
  Button button{gpio, 1};
  std::uint64_t now{0};
  void SetUp() override { ASSERT_TRUE(button.init()); }

  /// 20ms ごとに ms だけ読む。最初に出た判定を返す
  domain::ButtonEvent run(std::uint64_t ms) {
    domain::ButtonEvent first = domain::ButtonEvent::None;
    for (std::uint64_t t = 0; t < ms; t += 20) {
      now += 20;
      const auto ev = button.poll(now);
      if (first == domain::ButtonEvent::None) first = ev;
    }
    return first;
  }
  void press() { gpio.set_level(1, false); }    // 押すと Low
  void release() { gpio.set_level(1, true); }
};

TEST_F(ButtonTest, ConfiguresPullupInput) { EXPECT_EQ(gpio.mode(1), fake::FakeGpio::Mode::InputPullup); }

TEST_F(ButtonTest, IgnoresChatterShorterThan60ms) {
  press();
  EXPECT_EQ(run(40), domain::ButtonEvent::None);  // 2回だけ Low
  release();
  EXPECT_EQ(run(200), domain::ButtonEvent::None);  // 押したと判定していないので短押しにならない
}

TEST_F(ButtonTest, ShortPressWhenReleasedAt999ms) {
  press();
  (void)run(60);  // 3回続いて押したと判定（ここから数える）
  const std::uint64_t pressed_at = now;
  // 離した判定（3回続いた High）がちょうど 999ms に来るように読む
  ASSERT_EQ(button.poll(pressed_at + 900), domain::ButtonEvent::None);
  release();
  ASSERT_EQ(button.poll(pressed_at + 959), domain::ButtonEvent::None);
  ASSERT_EQ(button.poll(pressed_at + 979), domain::ButtonEvent::None);
  EXPECT_EQ(button.poll(pressed_at + 999), domain::ButtonEvent::Short);
}

TEST_F(ButtonTest, ReleaseDetectedAt1000msIsLongNotShort) {
  press();
  (void)run(60);
  const std::uint64_t pressed_at = now;
  release();
  ASSERT_EQ(button.poll(pressed_at + 960), domain::ButtonEvent::None);
  ASSERT_EQ(button.poll(pressed_at + 980), domain::ButtonEvent::None);
  EXPECT_EQ(button.poll(pressed_at + 1000), domain::ButtonEvent::Long);  // 1,000ms 押していた
}

TEST_F(ButtonTest, LongPressFiresAtExactly1000msWithoutRelease) {
  press();
  (void)run(60);
  const std::uint64_t pressed_at = now;
  domain::ButtonEvent ev = domain::ButtonEvent::None;
  while (ev == domain::ButtonEvent::None) {
    now += 20;
    ev = button.poll(now);
    if (now - pressed_at < 1000) {
      ASSERT_EQ(ev, domain::ButtonEvent::None) << now - pressed_at;
    }
  }
  EXPECT_EQ(ev, domain::ButtonEvent::Long);
  EXPECT_EQ(now - pressed_at, 1000U);  // ちょうど 1,000ms
  EXPECT_EQ(run(500), domain::ButtonEvent::None);  // 押し続けても1回だけ
  release();
  EXPECT_EQ(run(100), domain::ButtonEvent::None);  // 長押しの後に離しても短押しにならない
}

TEST_F(ButtonTest, InvertedLogic) {
  Button high_active(gpio, 2, false);
  ASSERT_TRUE(high_active.init());
  gpio.set_level(2, true);  // 押すと High
  std::uint64_t t = 0;
  for (int i = 0; i < 3; ++i) (void)high_active.poll(t += 20);
  gpio.set_level(2, false);
  domain::ButtonEvent ev = domain::ButtonEvent::None;
  for (int i = 0; i < 3; ++i) ev = high_active.poll(t += 20);
  EXPECT_EQ(ev, domain::ButtonEvent::Short);
}

}  // namespace
}  // namespace node::drivers
