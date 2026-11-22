#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>

#include "app/control_arbiter.hpp"

namespace node::app {
namespace {

using domain::ActuatorSpec;
using domain::Level;
using domain::Owner;
using domain::Priority;
using ::testing::_;

class MockActuator : public domain::IActuator {
 public:
  explicit MockActuator(const ActuatorSpec& spec) : spec_(spec) {
    ON_CALL(*this, apply(_)).WillByDefault([this](Level l) {
      level_ = l;
      return support::ok();
    });
    ON_CALL(*this, current()).WillByDefault([this] { return level_; });
  }
  [[nodiscard]] std::string_view id() const override { return spec_.id; }
  support::Result<void> init() override { return support::ok(); }
  MOCK_METHOD(support::Result<void>, apply, (Level), (override));
  MOCK_METHOD(Level, current, (), (const, override));
  [[nodiscard]] const ActuatorSpec& spec() const override { return spec_; }

 private:
  const ActuatorSpec& spec_;
  Level level_{0};
};

constexpr ActuatorSpec kFan{"fan_main", 0, 1000, {}, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}, "soil"};
constexpr ActuatorSpec kNarrow{"led_grow", 100, 600, {}, {}};

class ControlArbiterTest : public ::testing::Test {
 protected:
  ::testing::NiceMock<MockActuator> fan{kFan};
  ::testing::NiceMock<MockActuator> pump{kPump};
  ::testing::NiceMock<MockActuator> led{kNarrow};
  std::array<domain::IActuator*, 3> list{&fan, &pump, &led};
  ControlArbiter::InterlockChanges changes{};
};

TEST_F(ControlArbiterTest, StartsAllAtZeroWithOwnerNone) {
  EXPECT_CALL(fan, apply(0)).Times(1);
  EXPECT_CALL(pump, apply(0)).Times(1);
  EXPECT_CALL(led, apply(0)).Times(1);
  ControlArbiter a(list);
  ASSERT_EQ(a.size(), 3U);
  for (std::size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a.output(i), 0);
    EXPECT_EQ(a.owner(i), Owner::None);
  }
}

TEST_F(ControlArbiterTest, LeaseSetsOutputAndOwnerFromPriority) {
  ControlArbiter a(list);
  EXPECT_CALL(fan, apply(800)).Times(1);
  const auto r = a.set_command(0, 800, Priority::Manual, 60, 1000, changes);
  EXPECT_EQ(r.applied, 800);
  EXPECT_FALSE(r.limited);
  EXPECT_EQ(a.output(0), 800);
  EXPECT_EQ(a.owner(0), Owner::Manual);
  EXPECT_EQ(a.find("fan_main"), 0U);
  EXPECT_FALSE(a.find("nope").has_value());
}

TEST_F(ControlArbiterTest, NewCommandReplacesOldRegardlessOfPriority) {
  ControlArbiter a(list);
  (void)a.set_command(0, 800, Priority::Safety, 60, 1000, changes);
  const auto r = a.set_command(0, 300, Priority::Schedule, 60, 2000, changes);
  EXPECT_EQ(r.applied, 300);
  EXPECT_EQ(a.owner(0), Owner::Schedule);
}

TEST_F(ControlArbiterTest, LeaseExpiryFallsBackToDefault) {
  ControlArbiter a(list);
  (void)a.set_command(0, 700, Priority::Auto, 3, 1000, changes);
  a.tick(3999, changes);
  EXPECT_EQ(a.owner(0), Owner::Auto);
  EXPECT_CALL(fan, apply(0)).Times(1);
  a.tick(4000, changes);  // 受信時刻＋3秒ちょうどで切れる
  EXPECT_EQ(a.output(0), 0);
  EXPECT_EQ(a.owner(0), Owner::None);
}

TEST_F(ControlArbiterTest, ReleaseFallsBackImmediately) {
  ControlArbiter a(list);
  (void)a.set_command(0, 700, Priority::Auto, 60, 1000, changes);
  a.release(0, 1500, changes);
  EXPECT_EQ(a.output(0), 0);
  EXPECT_EQ(a.owner(0), Owner::None);
}

TEST_F(ControlArbiterTest, ClampsToLimitsAndReportsLimited) {
  ControlArbiter a(list);
  const auto hi = a.set_command(2, 900, Priority::Auto, 60, 1000, changes);
  EXPECT_EQ(hi.applied, 600);
  EXPECT_TRUE(hi.limited);
  const auto lo = a.set_command(2, 50, Priority::Auto, 60, 1000, changes);
  EXPECT_EQ(lo.applied, 100);
  EXPECT_TRUE(lo.limited);
  const auto mid = a.set_command(2, 300, Priority::Auto, 60, 1000, changes);
  EXPECT_FALSE(mid.limited);
}

TEST_F(ControlArbiterTest, LeaseLongerThanMaxOnIsLimitedAtReceipt) {
  ControlArbiter a(list);
  const auto r = a.set_command(1, 1000, Priority::Auto, 60, 1000, changes);
  EXPECT_TRUE(r.limited);
  EXPECT_EQ(r.applied, 1000);  // その時点の出力
  const auto ok = a.set_command(1, 1000, Priority::Auto, 30, 1000, changes);
  EXPECT_FALSE(ok.limited);
}

TEST_F(ControlArbiterTest, PumpInterlockStopsAndOwnerIsInterlock) {
  ControlArbiter a(list);
  (void)a.set_command(1, 1000, Priority::Auto, 600, 0, changes);
  a.tick(29'990, changes);
  EXPECT_TRUE(changes.empty());
  EXPECT_CALL(pump, apply(0)).Times(1);
  a.tick(30'000, changes);
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(changes[0].id, "pump");
  EXPECT_EQ(changes[0].transition, domain::PumpInterlock::Transition::Activated);
  EXPECT_EQ(a.output(1), 0);
  EXPECT_EQ(a.owner(1), Owner::Interlock);

  // 再開待ちの間の新しい指令は出力0のままで limited
  changes.clear();
  const auto r = a.set_command(1, 1000, Priority::Auto, 20, 40'000, changes);
  EXPECT_EQ(r.applied, 0);
  EXPECT_TRUE(r.limited);

  // 要求が0に戻り（リース切れ）、停止から60秒で解除
  a.tick(60'000, changes);
  EXPECT_TRUE(changes.empty());
  a.tick(90'000, changes);
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(changes[0].transition, domain::PumpInterlock::Transition::Released);
  EXPECT_EQ(a.owner(1), Owner::None);
}

}  // namespace
}  // namespace node::app
