#include <gtest/gtest.h>

#include <array>
#include <chrono>

#include "app/sensor_poller.hpp"
#include "drivers/sim/sim_sensor.hpp"

namespace node::app {
namespace {

using namespace std::chrono_literals;
using domain::ChannelSpec;
using domain::Quality;
using domain::Unit;

constexpr std::array<ChannelSpec, 3> kBme{{{"temp_air", Unit::DegC, 2000},
                                           {"humidity", Unit::PctRh, 2000},
                                           {"pressure", Unit::HPa, 2000}}};
constexpr std::array<std::int32_t, 3> kBmeVal{25000, 50000, 1013250};
constexpr std::array<ChannelSpec, 1> kLux{{{"lux", Unit::Lux, 1000}}};
constexpr std::array<std::int32_t, 1> kLuxVal{300000};

class SensorPollerTest : public ::testing::Test {
 protected:
  drivers::SimSensor bme{"bme280", kBme, kBmeVal, 2000ms};
  drivers::SimSensor lux{"bh1750", kLux, kLuxVal, 1000ms};
  std::array<domain::ISensor*, 2> sensors{&bme, &lux};
  domain::ReadingStore store;
  SensorPoller poller{sensors, store};
  SensorPoller::Result r{};

  void SetUp() override { poller.start(0); }
};

TEST_F(SensorPollerTest, ReadsEachSensorAtItsPeriod) {
  poller.tick(0, r);
  EXPECT_EQ(r.readings.size(), 4U);
  EXPECT_EQ(store.quality("temp_air", 0), Quality::Good);
  poller.tick(999, r);
  EXPECT_TRUE(r.readings.empty());
  poller.tick(1000, r);
  ASSERT_EQ(r.readings.size(), 1U);
  EXPECT_EQ(r.readings[0].channel, "lux");
  poller.tick(2000, r);
  EXPECT_EQ(r.readings.size(), 4U);
}

TEST_F(SensorPollerTest, FailureMarksFaultAndEmitsOnePerChannel) {
  poller.tick(0, r);
  bme.set_fault(true);
  poller.tick(2000, r);
  ASSERT_EQ(r.changes.size(), 3U);  // BME280 はチャネルごとに1件（#6）
  EXPECT_EQ(r.changes[0].channel, "temp_air");
  EXPECT_EQ(r.changes[2].channel, "pressure");
  EXPECT_TRUE(r.changes[0].fault);
  EXPECT_EQ(store.quality("temp_air", 2000), Quality::Fault);
  EXPECT_EQ(store.quality("lux", 2000), Quality::Good);
  EXPECT_EQ(poller.read_errors(), 1U);
  poller.tick(4000, r);
  EXPECT_TRUE(r.changes.empty());  // 遷移1回につき1件
}

TEST_F(SensorPollerTest, ReinitEvery30sAndRecovers) {
  poller.tick(0, r);
  bme.set_fault(true);
  poller.tick(2000, r);
  const std::uint32_t inits = bme.init_count();
  poller.tick(2000 + 29'999, r);
  EXPECT_EQ(bme.init_count(), inits);  // 30秒たつまで試さない
  poller.tick(2000 + 30'000, r);
  EXPECT_EQ(bme.init_count(), inits + 1);
  EXPECT_TRUE(r.changes.empty());  // まだ故障中
  bme.set_fault(false);
  poller.tick(2000 + 59'999, r);
  EXPECT_TRUE(r.changes.empty());
  poller.tick(2000 + 60'000, r);
  ASSERT_EQ(r.changes.size(), 3U);
  EXPECT_FALSE(r.changes[0].fault);
  EXPECT_EQ(store.quality("temp_air", 62'000), Quality::Good);
  poller.tick(2000 + 60'000 + 2000, r);
  EXPECT_FALSE(r.readings.empty());  // 通常の周期に戻る
}

TEST_F(SensorPollerTest, PowerSaveMultipliesPeriodByFive) {
  poller.tick(0, r);
  poller.set_period_factor(5);
  poller.tick(1000, r);
  EXPECT_TRUE(r.readings.empty());
  poller.tick(4999, r);
  EXPECT_TRUE(r.readings.empty());
  poller.tick(5000, r);  // lux：最後に計測した時刻（0）＋5000（#40）
  ASSERT_EQ(r.readings.size(), 1U);
  poller.tick(10'000, r);
  EXPECT_EQ(r.readings.size(), 4U);  // lux と BME280（2000×5）
  // stale のしきい値も5倍の周期で数える（#39）
  EXPECT_EQ(store.quality("temp_air", 10'000 + 30'000), Quality::Good);
  poller.set_period_factor(1);
  poller.tick(11'000, r);
  ASSERT_EQ(r.readings.size(), 1U);
}

}  // namespace
}  // namespace node::app
