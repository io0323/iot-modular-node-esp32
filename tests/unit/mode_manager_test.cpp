#include <gtest/gtest.h>

#include <array>

#include "app/manual_control.hpp"
#include "app/mode_manager.hpp"
#include "drivers/sim/sim_sensor.hpp"

namespace node::app {
namespace {

using domain::Mode;

TEST(ModeManager, ButtonCyclesAutoManualPowerSave) {
  ModeManager m;
  EXPECT_EQ(m.mode(), Mode::Auto);
  auto c = m.press(0);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->from, Mode::Auto);
  EXPECT_EQ(c->to, Mode::Manual);
  EXPECT_EQ(c->cause, ModeCause::Button);
  EXPECT_EQ(m.press(10)->to, Mode::PowerSave);
  EXPECT_EQ(m.press(20)->to, Mode::Auto);
}

TEST(ModeManager, StateBehaviours) {
  ModeManager m;
  EXPECT_TRUE(m.state().rules_enabled());
  EXPECT_TRUE(m.state().lighting_enabled());
  EXPECT_FALSE(m.state().manual_enabled());
  EXPECT_EQ(m.state().period_factor(), 1U);
  (void)m.press(0);
  EXPECT_TRUE(m.state().manual_enabled());
  EXPECT_FALSE(m.state().rules_enabled());
  (void)m.press(0);
  EXPECT_TRUE(m.state().rules_enabled());
  EXPECT_FALSE(m.state().lighting_enabled());
  EXPECT_EQ(m.state().period_factor(), 5U);
}

TEST(ModeManager, CmdSwitchesAndSameModeDoesNothing) {
  ModeManager m;
  const auto c = m.request(Mode::PowerSave, 0);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->cause, ModeCause::Cmd);
  EXPECT_EQ(m.mode(), Mode::PowerSave);
  EXPECT_FALSE(m.request(Mode::PowerSave, 10).has_value());  // #18
  EXPECT_EQ(to_string(ModeCause::Timeout), "timeout");
}

TEST(ModeManager, ManualTimesOutAtExactly30Minutes) {
  ModeManager m;
  (void)m.request(Mode::Manual, 1000);
  EXPECT_FALSE(m.tick(1000 + ModeManager::kManualTimeoutMs - 1).has_value());  // 手前の1ms では戻らない
  const auto c = m.tick(1000 + ModeManager::kManualTimeoutMs);
  ASSERT_TRUE(c.has_value());
  EXPECT_EQ(c->from, Mode::Manual);
  EXPECT_EQ(c->to, Mode::Auto);
  EXPECT_EQ(c->cause, ModeCause::Timeout);
}

TEST(ModeManager, ManualOperationRestartsTimeout) {
  ModeManager m;
  (void)m.press(0);
  m.touch_manual(600'000);
  EXPECT_FALSE(m.tick(ModeManager::kManualTimeoutMs).has_value());
  EXPECT_FALSE(m.tick(600'000 + ModeManager::kManualTimeoutMs - 1).has_value());
  EXPECT_TRUE(m.tick(600'000 + ModeManager::kManualTimeoutMs).has_value());
}

TEST(ModeManager, OtherModesDoNotTimeOut) {
  ModeManager m;
  EXPECT_FALSE(m.tick(10 * ModeManager::kManualTimeoutMs).has_value());
  (void)m.request(Mode::PowerSave, 0);
  EXPECT_FALSE(m.tick(10 * ModeManager::kManualTimeoutMs).has_value());
}

constexpr domain::ActuatorSpec kFan{"fan_main", 0, 1000, {}};

TEST(ManualControl, AcceptedOnlyInManualModeAndTouchesTimeout) {
  domain::ReadingStore store;
  drivers::SimActuator fan{kFan};
  std::array<domain::ActuatorBinding, 1> list{{{&fan, {}}}};
  ControlArbiter arb(list, store);
  ModeManager modes;
  ManualControl manual(arb, modes);
  EXPECT_EQ(manual.set_manual("fan_main", 500, 0), ManualControl::Outcome::NotManualMode);
  (void)modes.press(0);
  arb.set_stages(modes.state().manual_enabled(), modes.state().rules_enabled());
  EXPECT_EQ(manual.set_manual("heater", 500, 10), ManualControl::Outcome::UnknownTarget);
  EXPECT_EQ(manual.set_manual("fan_main", 500, 100'000), ManualControl::Outcome::Accepted);
  ControlArbiter::InterlockChanges ch{};
  arb.tick(100'000, ch);
  EXPECT_EQ(fan.current(), 500);
  EXPECT_EQ(arb.owner(0), domain::Owner::Local);
  EXPECT_FALSE(modes.tick(ModeManager::kManualTimeoutMs).has_value());  // 手動操作で数え直した
  manual.clear();
  arb.tick(100'010, ch);
  EXPECT_EQ(fan.current(), 0);
  EXPECT_EQ(to_string(ManualControl::Outcome::NotManualMode), "not_manual_mode");
}

}  // namespace
}  // namespace node::app
