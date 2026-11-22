#include <gtest/gtest.h>

#include <array>
#include <deque>
#include <string>
#include <vector>

#include "app/ui_service.hpp"
#include "drivers/sim/sim_sensor.hpp"

namespace node::app {
namespace {

using domain::ButtonEvent;
using domain::Mode;

/// 判定結果を順に返すボタン
class ScriptedButton final : public domain::IButton {
 public:
  support::Result<void> init() override { return support::ok(); }
  ButtonEvent poll(std::uint64_t /*now_ms*/) override {
    if (events.empty()) return ButtonEvent::None;
    const ButtonEvent e = events.front();
    events.pop_front();
    return e;
  }
  std::deque<ButtonEvent> events;
};

/// 表示した行とオン・オフを覚える表示器
class RecordingDisplay final : public domain::IDisplay {
 public:
  support::Result<void> init() override { return support::ok(); }
  support::Result<void> set_power(bool on) override {
    power_calls.push_back(on);
    return support::ok();
  }
  support::Result<void> show(std::span<const std::string_view> lines) override {
    last.clear();
    for (std::string_view l : lines) last.emplace_back(l);
    ++shows;
    return support::ok();
  }
  std::vector<std::string> last;
  std::vector<bool> power_calls;
  int shows{0};
};

constexpr domain::ActuatorSpec kFan{"fan_main", 0, 1000, {}};
constexpr domain::ActuatorSpec kLed{"led_grow", 0, 1000, {}};
constexpr domain::ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}};
constexpr domain::ActuatorSpec kBuzzer{"buzzer", 0, 1000, {}};

class UiServiceTest : public ::testing::Test {
 protected:
  domain::ReadingStore store;
  drivers::SimActuator fan{kFan}, led{kLed}, pump{kPump}, buzzer{kBuzzer};
  std::array<domain::ActuatorBinding, 4> list{{{&fan, {}}, {&led, {}}, {&pump, {}}, {&buzzer, {}}}};
  ControlArbiter arbiter{list, store};
  ModeManager modes;
  ManualControl manual{arbiter, modes};
  ScriptedButton mode_btn;
  ScriptedButton action_btn;
  RecordingDisplay display;
  UiService ui{UiService::Devices{&mode_btn, &action_btn, &display}, modes, manual, arbiter, store};
  std::uint64_t now{0};
  ControlArbiter::InterlockChanges ch{};

  void SetUp() override {
    ASSERT_TRUE(store.update({"temp_air", 25000, domain::Unit::DegC, domain::Quality::Good}, 0));
    ASSERT_TRUE(store.update({"humidity", 50000, domain::Unit::PctRh, domain::Quality::Good}, 0));
    ASSERT_TRUE(store.update({"lux", 300000, domain::Unit::Lux, domain::Quality::Good}, 0));
    ASSERT_TRUE(store.update({"soil", 40000, domain::Unit::Pct, domain::Quality::Good}, 0));
    ASSERT_TRUE(store.update({"pressure", 1013250, domain::Unit::HPa, domain::Quality::Good}, 0));
  }
  UiService::Actions step(bool connected = true) {
    now += 20;
    return ui.tick(now, connected);
  }
  void enter_manual() {
    (void)modes.request(Mode::Manual, now);
    arbiter.set_stages(true, false);
  }
};

TEST_F(UiServiceTest, ModeShortPressIsReportedToNode) {
  mode_btn.events.push_back(ButtonEvent::Short);
  EXPECT_TRUE(step().mode_pressed);
  EXPECT_FALSE(step().mode_pressed);
  mode_btn.events.push_back(ButtonEvent::Long);  // MODE の長押しは割り当てなし
  EXPECT_FALSE(step().mode_pressed);
}

TEST_F(UiServiceTest, ActionIsIgnoredOutsideManualMode) {
  action_btn.events = {ButtonEvent::Short, ButtonEvent::Long};
  (void)step();
  (void)step();
  EXPECT_EQ(manual.selected(), 0U);
  EXPECT_FALSE(arbiter.manual(0).has_value());
}

TEST_F(UiServiceTest, ActionShortCyclesSelectionInConfigOrder) {
  enter_manual();
  EXPECT_EQ(manual.selected_id(), "fan_main");
  for (std::string_view expected : {"led_grow", "pump", "buzzer", "fan_main"}) {
    action_btn.events.push_back(ButtonEvent::Short);
    (void)step();
    EXPECT_EQ(manual.selected_id(), expected);
  }
}

TEST_F(UiServiceTest, ActionLongTogglesSelectedBetween0And1000) {
  enter_manual();
  action_btn.events = {ButtonEvent::Short, ButtonEvent::Short};  // pump を選ぶ
  (void)step();
  (void)step();
  action_btn.events.push_back(ButtonEvent::Long);
  (void)step();
  arbiter.tick(now, ch);
  EXPECT_EQ(arbiter.manual(2), 1000);
  EXPECT_EQ(pump.current(), 1000);
  action_btn.events.push_back(ButtonEvent::Long);
  (void)step();
  arbiter.tick(now, ch);
  EXPECT_EQ(arbiter.manual(2), 0);
  EXPECT_EQ(pump.current(), 0);
}

TEST_F(UiServiceTest, ComposesEightLines) {
  enter_manual();
  arbiter.set_manual(0, 1000);
  arbiter.tick(now, ch);
  const auto l = ui.compose(now, true);
  EXPECT_EQ(l[0].view(), "MANUAL MQTT:ON");
  EXPECT_EQ(l[1].view(), "T     25.0C");
  EXPECT_EQ(l[2].view(), "H     50.0%");
  EXPECT_EQ(l[3].view(), "L      300lx");
  EXPECT_EQ(l[4].view(), "S     40.0%");
  EXPECT_EQ(l[5].view(), "P   1013.3hPa");
  EXPECT_EQ(l[6].view(), "F1000 L0 P0 B0");
  EXPECT_EQ(l[7].view(), "> fan_main");
  (void)modes.request(Mode::Auto, now);
  const auto a = ui.compose(now, false);
  EXPECT_EQ(a[0].view(), "AUTO   MQTT:OFF");
  EXPECT_EQ(a[7].view(), "");  // 手動モードのときだけ
}

TEST_F(UiServiceTest, FaultShowsDashes) {
  ASSERT_TRUE(store.mark_fault("temp_air", 0));
  const auto l = ui.compose(now, true);
  EXPECT_EQ(l[1].view(), "T       --C");
}

TEST_F(UiServiceTest, RefreshesEverySecondAndTurnsOffInPowerSave) {
  ASSERT_TRUE(ui.init());
  (void)step();
  EXPECT_EQ(display.shows, 1);
  for (int i = 0; i < 48; ++i) (void)step();  // 960ms
  EXPECT_EQ(display.shows, 1);
  for (int i = 0; i < 2; ++i) (void)step();
  EXPECT_EQ(display.shows, 2);  // 1秒ごと
  ASSERT_FALSE(display.last.empty());
  EXPECT_EQ(display.last[0], "AUTO   MQTT:ON");

  (void)modes.request(Mode::PowerSave, now);
  (void)step();
  ASSERT_EQ(display.power_calls.size(), 1U);
  EXPECT_FALSE(display.power_calls[0]);  // すぐに消す
  EXPECT_FALSE(ui.display_on());
  const int shows = display.shows;
  for (int i = 0; i < 100; ++i) (void)step();
  EXPECT_EQ(display.shows, shows);  // 消している間は書かない

  (void)modes.request(Mode::Auto, now);
  (void)step();
  ASSERT_EQ(display.power_calls.size(), 2U);
  EXPECT_TRUE(display.power_calls[1]);
  EXPECT_GT(display.shows, shows);
}

// ---- 最小オン・オフ時間（#47・#59）

TEST(MinOnOffTime, HoldsSwitchingUntilExactTimeAndInterlockWins) {
  static constexpr domain::ActuatorSpec kHeater{"heater", 0, 1000, {}, std::uint32_t{5000}, std::uint32_t{3000}};
  domain::ReadingStore store;
  drivers::SimActuator heater{kHeater};
  ControlArbiter::InterlockChanges ch{};

  struct ForceOff final : domain::IInterlock {
    [[nodiscard]] std::string_view rule() const override { return "test"; }
    domain::InterlockVerdict evaluate(const domain::InterlockInput& /*in*/) override {
      domain::InterlockVerdict v{};
      if (on) v.upper = 0;
      return v;
    }
    [[nodiscard]] bool engaged() const override { return on; }
    bool on{false};
  } force_off;
  std::array<domain::IInterlock*, 1> il{&force_off};
  std::array<domain::ActuatorBinding, 1> list{{{&heater, il}}};
  ControlArbiter arb(list, store);

  arb.set_rule_proposal(0, 600);
  arb.tick(10'000, ch);
  EXPECT_EQ(heater.current(), 600);  // 10秒オフだったのですぐオン
  arb.set_rule_proposal(0, 800);
  arb.tick(10'100, ch);
  EXPECT_EQ(heater.current(), 800);  // オンの間の値の変更はすぐ
  arb.set_rule_proposal(0, std::nullopt);
  arb.tick(14'999, ch);
  EXPECT_EQ(heater.current(), 800);  // 最小オン 5000ms の手前の1ms では切らない
  EXPECT_EQ(arb.owner(0), domain::Owner::None);  // owner は段の結果のまま
  arb.tick(15'000, ch);
  EXPECT_EQ(heater.current(), 0);  // ちょうど
  arb.set_rule_proposal(0, 600);
  arb.tick(17'999, ch);
  EXPECT_EQ(heater.current(), 0);  // 最小オフ 3000ms
  arb.tick(18'000, ch);
  EXPECT_EQ(heater.current(), 600);
  force_off.on = true;
  arb.tick(18'010, ch);
  EXPECT_EQ(heater.current(), 0);  // インターロックは最小オン時間より優先
  EXPECT_EQ(arb.owner(0), domain::Owner::Interlock);
}

}  // namespace
}  // namespace node::app
