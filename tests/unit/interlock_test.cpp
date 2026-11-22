#include <gtest/gtest.h>

#include "config/devices.hpp"
#include "domain/fan_temp_interlock.hpp"
#include "domain/pump_interlock.hpp"
#include "domain/reading_store.hpp"
#include "domain/sensor_fault_interlock.hpp"

namespace node::domain {
namespace {

class InterlockTest : public ::testing::Test {
 protected:
  ReadingStore store;
  void SetUp() override {
    ASSERT_TRUE(store.set_period("temp_air", 2000));
    ASSERT_TRUE(store.set_period("soil", 10000));
  }
  void temp(std::int32_t milli, std::uint64_t now) {
    ASSERT_TRUE(store.update({"temp_air", milli, Unit::DegC, Quality::Good}, now));
  }
  InterlockVerdict eval(IInterlock& il, std::uint64_t now, Level requested = 0) {
    return il.evaluate(InterlockInput{store, requested, now});
  }
};

// ---- FanTempInterlock（temp_high）

TEST_F(InterlockTest, FanTempEngagesAt45ButNotAt44_9) {
  FanTempInterlock il(config::kFanTempHigh);
  EXPECT_EQ(il.rule(), "temp_high");
  temp(44900, 0);
  EXPECT_FALSE(eval(il, 0).force.has_value());
  EXPECT_FALSE(il.engaged());
  temp(45000, 10);
  const auto v = eval(il, 10);
  ASSERT_TRUE(v.force.has_value());
  EXPECT_EQ(*v.force, 1000);
  EXPECT_FALSE(v.upper.has_value());
  EXPECT_TRUE(il.engaged());
}

TEST_F(InterlockTest, FanTempReleasesAfterExactly10sBelow43) {
  ASSERT_TRUE(store.set_period("temp_air", 60000));  // 途中で読み直さなくても stale にしない
  FanTempInterlock il(config::kFanTempHigh);
  temp(46000, 0);
  (void)eval(il, 0);
  temp(42900, 1000);
  (void)eval(il, 1000);  // ここから数える
  EXPECT_TRUE(eval(il, 1000 + 9999).force.has_value());
  EXPECT_TRUE(il.engaged());
  EXPECT_FALSE(eval(il, 1000 + 10000).force.has_value());  // ちょうど10秒で解除
  EXPECT_FALSE(il.engaged());
}

TEST_F(InterlockTest, FanTempRestartsCountWhenBackTo43) {
  ASSERT_TRUE(store.set_period("temp_air", 60000));  // 途中で読み直さなくても stale にしない
  FanTempInterlock il(config::kFanTempHigh);
  temp(46000, 0);
  (void)eval(il, 0);
  temp(42000, 1000);
  (void)eval(il, 1000);
  temp(43000, 6000);  // 43.0℃は「43℃未満」ではない
  (void)eval(il, 6000);
  temp(42000, 7000);
  (void)eval(il, 7000);
  EXPECT_TRUE(eval(il, 16999).force.has_value());
  EXPECT_FALSE(eval(il, 17000).force.has_value());
}

TEST_F(InterlockTest, FanTempHoldsStateAndRestartsCountWhileInputNotGood) {
  ASSERT_TRUE(store.set_period("temp_air", 60000));  // 途中で読み直さなくても stale にしない
  FanTempInterlock il(config::kFanTempHigh);
  temp(46000, 0);
  (void)eval(il, 0);
  temp(42000, 1000);
  (void)eval(il, 1000);
  ASSERT_TRUE(store.mark_fault("temp_air", 5000));
  EXPECT_TRUE(eval(il, 5000).force.has_value());   // 状態はそのまま
  EXPECT_TRUE(eval(il, 12000).force.has_value());  // good でない間は数えない（#38）
  temp(42000, 13000);
  (void)eval(il, 13000);  // good に戻ってから改めて数える
  EXPECT_TRUE(eval(il, 22999).force.has_value());
  EXPECT_FALSE(eval(il, 23000).force.has_value());
}

// ---- SensorFaultInterlock（sensor_fault）

TEST_F(InterlockTest, SensorFaultForcesWhileFaultOrStale) {
  SensorFaultInterlock il(config::kFanOnTempFault);
  EXPECT_EQ(il.rule(), "sensor_fault");
  EXPECT_EQ(eval(il, 0).force, 1000);  // 一度も読めていない → stale（#36）
  temp(25000, 0);
  EXPECT_FALSE(eval(il, 0).force.has_value());
  EXPECT_FALSE(il.engaged());
  EXPECT_FALSE(eval(il, 6000).force.has_value());  // 周期2000の3倍ちょうど
  EXPECT_EQ(eval(il, 6001).force, 1000);           // stale
  temp(25000, 7000);
  EXPECT_FALSE(eval(il, 7000).force.has_value());  // good に戻った時点で解除
  ASSERT_TRUE(store.mark_fault("temp_air", 8000));
  EXPECT_EQ(eval(il, 8000).force, 1000);
  EXPECT_TRUE(il.engaged());
  EXPECT_FALSE(il.rejects_commands(store, 8000));  // ファンの構成は指令を拒まない
}

TEST_F(InterlockTest, SensorFaultUpperZeroAndRejectsForPump) {
  SensorFaultInterlock il(config::kPumpOffOnSoilFault);
  ASSERT_TRUE(store.update({"soil", 40000, Unit::Pct, Quality::Good}, 0));
  EXPECT_FALSE(eval(il, 0, 1000).upper.has_value());
  EXPECT_FALSE(il.rejects_commands(store, 0));
  ASSERT_TRUE(store.mark_fault("soil", 10));
  const auto v = eval(il, 10, 1000);
  EXPECT_EQ(v.upper, 0);
  EXPECT_FALSE(v.force.has_value());
  EXPECT_TRUE(il.rejects_commands(store, 10));
  ASSERT_TRUE(store.update({"soil", 40000, Unit::Pct, Quality::Good}, 100));
  EXPECT_FALSE(il.rejects_commands(store, 100 + 30000));      // 周期10000の3倍ちょうど
  EXPECT_TRUE(il.rejects_commands(store, 100 + 30001));       // stale でも拒む
}

// ---- PumpInterlock を IInterlock として

TEST_F(InterlockTest, PumpAsInterlockGivesUpperZeroWhileActive) {
  PumpInterlock il(30);
  EXPECT_EQ(il.rule(), "max_on_s");
  EXPECT_FALSE(eval(il, 0, 1000).upper.has_value());
  const auto v = eval(il, 30000, 1000);
  EXPECT_EQ(v.upper, 0);
  EXPECT_TRUE(il.engaged());
  EXPECT_EQ(eval(il, 40000, 0).upper, 0);  // 停止中は要求が0でも上限0（解除前）
  EXPECT_FALSE(eval(il, 90000, 0).upper.has_value());
  EXPECT_FALSE(il.engaged());
}

}  // namespace
}  // namespace node::domain
