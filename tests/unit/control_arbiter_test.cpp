#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>

#include "app/control_arbiter.hpp"
#include "domain/pump_interlock.hpp"

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

/// テストから効き方を決めるインターロック
class FakeInterlock final : public domain::IInterlock {
 public:
  explicit FakeInterlock(std::string_view rule) : rule_(rule) {}
  [[nodiscard]] std::string_view rule() const override { return rule_; }
  domain::InterlockVerdict evaluate(const domain::InterlockInput& in) override {
    last_requested = in.requested;
    domain::InterlockVerdict v{};
    if (on) {
      v.force = force;
      v.upper = upper;
    }
    return v;
  }
  [[nodiscard]] bool engaged() const override { return on; }
  [[nodiscard]] bool rejects_commands(const domain::ReadingStore& /*s*/, std::uint64_t /*now*/) const override {
    return on && rejects;
  }
  bool on{false};
  std::optional<Level> force{};
  std::optional<Level> upper{};
  bool rejects{false};
  Level last_requested{0};

 private:
  std::string_view rule_;
};

constexpr ActuatorSpec kFan{"fan_main", 0, 1000, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}};
constexpr ActuatorSpec kNarrow{"led_grow", 100, 600, {}};

class ControlArbiterTest : public ::testing::Test {
 protected:
  domain::ReadingStore store;
  ::testing::NiceMock<MockActuator> fan{kFan};
  ::testing::NiceMock<MockActuator> pump{kPump};
  ::testing::NiceMock<MockActuator> led{kNarrow};
  FakeInterlock a{"rule_a"};
  FakeInterlock b{"rule_b"};
  std::array<domain::IInterlock*, 2> fan_interlocks{&a, &b};
  domain::PumpInterlock pump_max_on{30};
  std::array<domain::IInterlock*, 1> pump_interlocks{&pump_max_on};
  std::array<domain::ActuatorBinding, 3> list{{{&fan, fan_interlocks}, {&pump, pump_interlocks}, {&led, {}}}};
  ControlArbiter::InterlockChanges changes{};
};

TEST_F(ControlArbiterTest, StartsAllAtZeroWithOwnerNone) {
  EXPECT_CALL(fan, apply(0)).Times(1);
  EXPECT_CALL(pump, apply(0)).Times(1);
  EXPECT_CALL(led, apply(0)).Times(1);
  ControlArbiter arb(list, store);
  ASSERT_EQ(arb.size(), 3U);
  for (std::size_t i = 0; i < arb.size(); ++i) {
    EXPECT_EQ(arb.output(i), 0);
    EXPECT_EQ(arb.owner(i), Owner::None);
  }
  EXPECT_EQ(arb.find("pump"), 1U);
  EXPECT_FALSE(arb.find("nope").has_value());
}

// ---- 5段の優先度：各段の有無 × モード（自動・手動・省電力）
struct StageCase {
  bool lease;
  bool manual;
  bool rule;
  domain::Mode mode;
  Level level;
  Owner owner;
};

constexpr bool manual_stage(domain::Mode m) { return m == domain::Mode::Manual; }
constexpr bool rule_stage(domain::Mode m) { return m != domain::Mode::Manual; }

TEST_F(ControlArbiterTest, FiveStagePriorityTable) {
  using M = domain::Mode;
  // 上位指令 700（manual）、手動 400、ルール 600、既定値 0
  const std::array<StageCase, 24> table{{
      {false, false, false, M::Auto, 0, Owner::None},
      {false, false, true, M::Auto, 600, Owner::Local},
      {false, true, false, M::Auto, 0, Owner::None},  // 手動は手動モードだけ
      {false, true, true, M::Auto, 600, Owner::Local},
      {true, false, false, M::Auto, 700, Owner::Manual},
      {true, false, true, M::Auto, 700, Owner::Manual},
      {true, true, false, M::Auto, 700, Owner::Manual},
      {true, true, true, M::Auto, 700, Owner::Manual},
      {false, false, false, M::Manual, 0, Owner::None},
      {false, false, true, M::Manual, 0, Owner::None},  // ルールは手動モードでは無効
      {false, true, false, M::Manual, 400, Owner::Local},
      {false, true, true, M::Manual, 400, Owner::Local},
      {true, false, false, M::Manual, 700, Owner::Manual},
      {true, false, true, M::Manual, 700, Owner::Manual},
      {true, true, false, M::Manual, 700, Owner::Manual},  // 上位指令は手動操作より強い
      {true, true, true, M::Manual, 700, Owner::Manual},
      {false, false, false, M::PowerSave, 0, Owner::None},
      {false, false, true, M::PowerSave, 600, Owner::Local},
      {false, true, false, M::PowerSave, 0, Owner::None},
      {false, true, true, M::PowerSave, 600, Owner::Local},
      {true, false, false, M::PowerSave, 700, Owner::Manual},
      {true, false, true, M::PowerSave, 700, Owner::Manual},
      {true, true, false, M::PowerSave, 700, Owner::Manual},
      {true, true, true, M::PowerSave, 700, Owner::Manual},
  }};
  int row = 0;
  for (const StageCase& c : table) {
    ControlArbiter arb(list, store);
    arb.set_stages(manual_stage(c.mode), rule_stage(c.mode));
    if (c.manual) arb.set_manual(0, 400);
    if (c.rule) arb.set_rule_proposal(0, 600);
    if (c.lease) (void)arb.set_command(0, 700, Priority::Manual, 60, 1000, changes);
    arb.tick(1000, changes);
    EXPECT_EQ(arb.output(0), c.level) << "row " << row;
    EXPECT_EQ(arb.owner(0), c.owner) << "row " << row;
    ++row;
  }
}

TEST_F(ControlArbiterTest, ClampsEveryStageToLimits) {
  ControlArbiter arb(list, store);
  arb.set_rule_proposal(2, 900);
  arb.tick(0, changes);
  EXPECT_EQ(arb.output(2), 600);
  arb.set_stages(true, false);
  arb.set_manual(2, 50);
  arb.tick(0, changes);
  EXPECT_EQ(arb.output(2), 100);
  arb.set_manual(2, std::nullopt);
  arb.tick(0, changes);
  EXPECT_EQ(arb.output(2), 100);  // 既定値 0 も min に収める
}

TEST_F(ControlArbiterTest, ManualIsClearedByClearManual) {
  ControlArbiter arb(list, store);
  arb.set_stages(true, false);
  arb.set_manual(0, 500);
  arb.tick(0, changes);
  EXPECT_EQ(arb.output(0), 500);
  arb.clear_manual();
  arb.tick(10, changes);
  EXPECT_EQ(arb.output(0), 0);
}

TEST_F(ControlArbiterTest, LeaseReplacesAndExpiresBackToLowerStage) {
  ControlArbiter arb(list, store);
  arb.set_rule_proposal(0, 600);
  (void)arb.set_command(0, 800, Priority::Safety, 3, 1000, changes);
  const auto r = arb.set_command(0, 300, Priority::Schedule, 3, 2000, changes);  // priority によらず置き換え
  EXPECT_EQ(r.applied, 300);
  EXPECT_EQ(arb.owner(0), Owner::Schedule);
  arb.tick(4999, changes);
  EXPECT_EQ(arb.output(0), 300);
  arb.tick(5000, changes);  // 受信時刻＋3秒ちょうど
  EXPECT_EQ(arb.output(0), 600);
  EXPECT_EQ(arb.owner(0), Owner::Local);
  (void)arb.set_command(0, 900, Priority::Auto, 60, 6000, changes);
  arb.release(0, 6100, changes);
  EXPECT_EQ(arb.output(0), 600);
}

// ---- インターロック：強制値と上限は各段にかかる

TEST_F(ControlArbiterTest, ForceAppliesOverEveryStage) {
  a.on = true;
  a.force = 1000;
  for (int stage = 0; stage < 4; ++stage) {
    ControlArbiter arb(list, store);
    if (stage == 1) arb.set_rule_proposal(0, 600);
    if (stage == 2) {
      arb.set_stages(true, false);
      arb.set_manual(0, 300);
    }
    if (stage == 3) (void)arb.set_command(0, 200, Priority::Auto, 60, 0, changes);
    arb.tick(0, changes);
    EXPECT_EQ(arb.output(0), 1000) << "stage " << stage;
    EXPECT_EQ(arb.owner(0), Owner::Interlock) << "stage " << stage;
  }
}

TEST_F(ControlArbiterTest, UpperAppliesOverEveryStageAndOwnerOnlyWhenChanged) {
  a.on = true;
  a.upper = 400;
  ControlArbiter arb(list, store);
  arb.set_rule_proposal(0, 600);
  arb.tick(0, changes);
  EXPECT_EQ(arb.output(0), 400);
  EXPECT_EQ(arb.owner(0), Owner::Interlock);
  arb.set_rule_proposal(0, 300);  // 上限より小さければ値は変わらず、owner は段のまま
  arb.tick(10, changes);
  EXPECT_EQ(arb.output(0), 300);
  EXPECT_EQ(arb.owner(0), Owner::Local);
  const auto r = arb.set_command(0, 900, Priority::Auto, 60, 20, changes);
  EXPECT_EQ(r.applied, 400);
  EXPECT_TRUE(r.limited);
  EXPECT_EQ(a.last_requested, 900);  // インターロックには段の結果が渡る
}

TEST_F(ControlArbiterTest, CombinesMultipleInterlocks) {
  a.on = true;
  b.on = true;
  a.upper = 500;
  b.upper = 300;
  ControlArbiter arb(list, store);
  arb.set_rule_proposal(0, 800);
  arb.tick(0, changes);
  EXPECT_EQ(arb.output(0), 300);  // 上限はいちばん小さいもの
  a.force = 700;
  b.force = 900;
  arb.tick(10, changes);
  EXPECT_EQ(arb.output(0), 900);  // 強制値はいちばん大きいもの、上限より優先
}

TEST_F(ControlArbiterTest, LimitedAndAppliedWhenInterlockChangesCommand) {
  a.on = true;
  a.force = 1000;
  ControlArbiter arb(list, store);
  const auto r = arb.set_command(0, 300, Priority::Auto, 60, 0, changes);
  EXPECT_EQ(r.applied, 1000);
  EXPECT_TRUE(r.limited);
  a.on = false;
  const auto ok = arb.set_command(0, 300, Priority::Auto, 60, 10, changes);
  EXPECT_EQ(ok.applied, 300);
  EXPECT_FALSE(ok.limited);
  // 範囲外への丸め、lease_s が max_on_s を超える指令も limited
  EXPECT_TRUE(arb.set_command(2, 900, Priority::Auto, 60, 20, changes).limited);
  const auto pr = arb.set_command(1, 1000, Priority::Auto, 60, 30, changes);
  EXPECT_TRUE(pr.limited);
  EXPECT_EQ(pr.applied, 1000);
  EXPECT_FALSE(arb.set_command(1, 1000, Priority::Auto, 30, 40, changes).limited);
}

// ---- INTERLOCK_ACTIVE／RELEASED（#24・#37）

TEST_F(ControlArbiterTest, AnnouncesOnlyWhenOutputChangesAndAlwaysInPairs) {
  ControlArbiter arb(list, store);
  (void)arb.set_command(0, 1000, Priority::Auto, 600, 0, changes);
  a.on = true;
  a.force = 1000;
  arb.tick(10, changes);
  EXPECT_TRUE(changes.empty());  // 出力が変わらない（1000 → 1000）ので出さない
  a.on = false;
  arb.tick(20, changes);
  EXPECT_TRUE(changes.empty());  // ACTIVE を出していないので RELEASED も出さない

  a.on = true;
  arb.tick(30, changes);
  arb.release(0, 40, changes);  // 段の結果が 0 になり、強制値 1000 で出力を変える
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(changes[0].id, "fan_main");
  EXPECT_EQ(changes[0].rule, "rule_a");
  EXPECT_TRUE(changes[0].active);
  arb.tick(50, changes);
  EXPECT_EQ(changes.size(), 1U);  // 遷移1回につき1件
  a.on = false;
  arb.tick(60, changes);
  ASSERT_EQ(changes.size(), 2U);
  EXPECT_FALSE(changes[1].active);
  EXPECT_EQ(changes[1].rule, "rule_a");
}

TEST_F(ControlArbiterTest, RejectsCommandsWhenAnInterlockSaysSo) {
  ControlArbiter arb(list, store);
  EXPECT_FALSE(arb.rejects_commands(0, 0));
  a.on = true;
  a.rejects = true;
  EXPECT_TRUE(arb.rejects_commands(0, 0));
  EXPECT_FALSE(arb.rejects_commands(2, 0));
}

TEST_F(ControlArbiterTest, PumpInterlockStopsAndReleasesThroughInterface) {
  ControlArbiter arb(list, store);
  (void)arb.set_command(1, 1000, Priority::Auto, 600, 0, changes);
  arb.tick(29'990, changes);
  EXPECT_TRUE(changes.empty());
  EXPECT_CALL(pump, apply(0)).Times(1);
  arb.tick(30'000, changes);
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_EQ(changes[0].rule, "max_on_s");
  EXPECT_TRUE(changes[0].active);
  EXPECT_EQ(arb.owner(1), Owner::Interlock);
  changes.clear();
  const auto r = arb.set_command(1, 1000, Priority::Auto, 20, 40'000, changes);
  EXPECT_EQ(r.applied, 0);
  EXPECT_TRUE(r.limited);
  arb.tick(60'000, changes);  // リースが切れて要求が0に戻る
  EXPECT_TRUE(changes.empty());
  arb.tick(90'000, changes);  // 停止から60秒
  ASSERT_EQ(changes.size(), 1U);
  EXPECT_FALSE(changes[0].active);
  EXPECT_EQ(arb.owner(1), Owner::None);
}

}  // namespace
}  // namespace node::app
