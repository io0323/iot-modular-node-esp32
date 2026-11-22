#include <gtest/gtest.h>

#include <array>
#include <string>

#include "app/command_service.hpp"
#include "config/devices.hpp"
#include "domain/pump_interlock.hpp"
#include "drivers/sim/sim_sensor.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

using domain::ActuatorSpec;
using test::json_value;
using test::make_cmd;
using test::ulid;

constexpr ActuatorSpec kFan{"fan_main", 0, 1000, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}};
constexpr ActuatorSpec kLed{"led_grow", 0, 500, {}};

class CommandServiceTest : public ::testing::Test {
 protected:
  drivers::SimActuator fan{kFan};
  drivers::SimActuator pump{kPump};
  drivers::SimActuator led{kLed};
  domain::ReadingStore store{};
  domain::PumpInterlock pump_max_on{30};
  domain::SensorFaultInterlock pump_soil{config::kPumpOffOnSoilFault};
  std::array<domain::IInterlock*, 2> pump_interlocks{&pump_max_on, &pump_soil};
  std::array<domain::ActuatorBinding, 3> list{{{&fan, {}}, {&pump, pump_interlocks}, {&led, {}}}};
  ControlArbiter arbiter{list, store};
  ModeManager modes{};
  CommandService svc{arbiter, modes};

  void SetUp() override {
    ASSERT_TRUE(store.set_period("soil", 10000));
    ASSERT_TRUE(store.update({"soil", 40000, domain::Unit::Pct, domain::Quality::Good}, now_ms));
  }
  std::uint64_t now_ms{1000};
  std::int64_t now_utc{*infra::parse_ts("2026-11-20T06:00:12.345Z")};

  CommandOutcome run(const std::string& json) { return svc.handle(json, now_ms, now_utc); }

  static std::string status(const CommandOutcome& o) { return json_value(o.ack_payload.view(), "status").value_or("?"); }
  static std::string reason(const CommandOutcome& o) { return json_value(o.ack_payload.view(), "reason").value_or("?"); }
};

TEST_F(CommandServiceTest, SetActuatorDoneWithApplied) {
  const auto o = run(make_cmd(ulid(1), "set_actuator", R"({"id":"fan_main","level":800})"));
  ASSERT_EQ(o.kind, CommandOutcome::Kind::Ack);
  EXPECT_EQ(o.ack_payload.view(),
            "{\"cmd_id\":\"" + ulid(1) + R"(","status":"done","limited":false,"applied":{"level":800},"reason":null})");
  EXPECT_EQ(fan.current(), 800);
  EXPECT_EQ(arbiter.owner(0), domain::Owner::Auto);
}

TEST_F(CommandServiceTest, SetActuatorClampedIsLimited) {
  const auto o = run(make_cmd(ulid(2), "set_actuator", R"({"id":"led_grow","level":900})"));
  EXPECT_EQ(status(o), "done");
  EXPECT_EQ(json_value(o.ack_payload.view(), "limited"), "true");
  EXPECT_EQ(json_value(o.ack_payload.view(), "level"), "500");
}

TEST_F(CommandServiceTest, LeaseOverMaxOnIsLimitedAtReceipt) {
  const auto o = run(make_cmd(ulid(3), "set_actuator", R"({"id":"pump","level":1000})", "auto", R"("lease_s":60)"));
  EXPECT_EQ(status(o), "done");
  EXPECT_EQ(json_value(o.ack_payload.view(), "limited"), "true");
  EXPECT_EQ(json_value(o.ack_payload.view(), "level"), "1000");
  // lease_s 省略（60秒）も max_on_s 30 を超える
  const auto o2 = run(make_cmd(ulid(4), "set_actuator", R"({"id":"pump","level":1000})"));
  EXPECT_EQ(json_value(o2.ack_payload.view(), "limited"), "true");
  const auto o3 = run(make_cmd(ulid(5), "set_actuator", R"({"id":"pump","level":1000})", "auto", R"("lease_s":30)"));
  EXPECT_EQ(json_value(o3.ack_payload.view(), "limited"), "false");
}

TEST_F(CommandServiceTest, PumpRejectedUnsafeWhileSoilStale) {
  now_ms += 30'001;  // 周期10000の3倍を超えた
  const auto o = run(make_cmd(ulid(43), "set_actuator", R"({"id":"pump","level":500})", "auto", R"("lease_s":10)"));
  EXPECT_EQ(reason(o), "unsafe");
}

TEST_F(CommandServiceTest, PumpRejectedUnsafeWhileSoilFault) {
  ASSERT_TRUE(store.mark_fault("soil", now_ms));
  const auto o = run(make_cmd(ulid(6), "set_actuator", R"({"id":"pump","level":500})", "auto", R"("lease_s":10)"));
  EXPECT_EQ(status(o), "rejected");
  EXPECT_EQ(reason(o), "unsafe");
  EXPECT_EQ(pump.current(), 0);
  EXPECT_EQ(arbiter.owner(1), domain::Owner::None);  // リースも登録しない
  // ほかのアクチュエーターは影響を受けない
  EXPECT_EQ(status(run(make_cmd(ulid(7), "set_actuator", R"({"id":"fan_main","level":1})"))), "done");
}

TEST_F(CommandServiceTest, ReleaseDone) {
  (void)run(make_cmd(ulid(8), "set_actuator", R"({"id":"fan_main","level":800})"));
  const auto o = run(make_cmd(ulid(9), "release", R"({"id":"fan_main"})", "auto", R"("lease_s":9999)"));
  EXPECT_EQ(o.ack_payload.view(), "{\"cmd_id\":\"" + ulid(9) + R"(","status":"done","reason":null})");
  EXPECT_EQ(fan.current(), 0);
  EXPECT_EQ(arbiter.owner(0), domain::Owner::None);
}

TEST_F(CommandServiceTest, SetModeDoneAndRequestsStateAndEvent) {
  const auto o = run(make_cmd(ulid(10), "set_mode", R"({"mode":"power_save"})"));
  EXPECT_EQ(status(o), "done");
  ASSERT_TRUE(o.mode_change.has_value());
  EXPECT_EQ(o.mode_change->from, domain::Mode::Auto);
  EXPECT_EQ(o.mode_change->to, domain::Mode::PowerSave);
  EXPECT_EQ(o.mode_change->cause, ModeCause::Cmd);
  EXPECT_EQ(modes.mode(), domain::Mode::PowerSave);
  // 同じモードへは done だけ（open-questions #18）
  const auto same = run(make_cmd(ulid(11), "set_mode", R"({"mode":"power_save"})"));
  EXPECT_EQ(status(same), "done");
  EXPECT_FALSE(same.mode_change.has_value());
  EXPECT_FALSE(same.republish_state);
}

TEST_F(CommandServiceTest, SetModeUnknownValueIsInvalidArg) {
  const auto o = run(make_cmd(ulid(12), "set_mode", R"({"mode":"eco"})"));
  EXPECT_EQ(status(o), "rejected");
  EXPECT_EQ(reason(o), "invalid_arg");
  EXPECT_EQ(modes.mode(), domain::Mode::Auto);
}

TEST_F(CommandServiceTest, GetStatusRepublishesState) {
  const auto o = run(make_cmd(ulid(13), "get_status", ""));
  EXPECT_EQ(status(o), "done");
  EXPECT_TRUE(o.republish_state);
  EXPECT_FALSE(o.mode_change.has_value());
}

TEST_F(CommandServiceTest, SetParamAndUnknownOpAreUnknownOp) {
  const auto a = run(make_cmd(ulid(14), "set_param", R"({"name":"x","value":1})"));
  EXPECT_EQ(status(a), "rejected");
  EXPECT_EQ(reason(a), "unknown_op");
  const auto b = run(make_cmd(ulid(15), "toggle", R"({"id":"fan_main"})"));
  EXPECT_EQ(reason(b), "unknown_op");
}

TEST_F(CommandServiceTest, ExpiredIsRejected) {
  const auto o = run(make_cmd(ulid(16), "set_actuator", R"({"id":"fan_main","level":1})", "auto",
                              R"("expires_at":"2026-11-20T06:00:12.344Z")"));
  EXPECT_EQ(reason(o), "expired");
  EXPECT_EQ(fan.current(), 0);
  // ちょうどの時刻はまだ過ぎていない
  const auto just = run(make_cmd(ulid(17), "set_actuator", R"({"id":"fan_main","level":1})", "auto",
                                 R"("expires_at":"2026-11-20T06:00:13.345Z")"));
  EXPECT_EQ(status(just), "done");
}

TEST_F(CommandServiceTest, UnknownTarget) {
  EXPECT_EQ(reason(run(make_cmd(ulid(18), "set_actuator", R"({"id":"heater","level":1})"))), "unknown_target");
  EXPECT_EQ(reason(run(make_cmd(ulid(19), "release", R"({"id":"heater"})"))), "unknown_target");
}

TEST_F(CommandServiceTest, InvalidArgCases) {
  const std::array<std::string, 11> cases{
      make_cmd(ulid(20), "set_actuator", R"({"id":"fan_main","level":1001})"),
      make_cmd(ulid(21), "set_actuator", R"({"id":"fan_main","level":-1})"),
      make_cmd(ulid(22), "set_actuator", R"({"id":"fan_main","level":"1"})"),
      make_cmd(ulid(23), "set_actuator", R"({"level":1})"),
      make_cmd(ulid(24), "set_actuator", ""),
      make_cmd(ulid(25), "set_actuator", R"({"id":"fan_main","level":1})", "auto", R"("lease_s":0)"),
      make_cmd(ulid(26), "set_actuator", R"({"id":"fan_main","level":1})", "auto", R"("lease_s":601)"),
      make_cmd(ulid(27), "set_actuator", R"({"id":"fan_main","level":1})", "urgent"),
      make_cmd(ulid(28), "set_actuator", R"({"id":"fan_main","level":1})", "auto", R"("expires_at":"soon")"),
      make_cmd(ulid(29), "release", "{}"),
      make_cmd(ulid(30), "set_mode", "{}"),
  };
  for (const auto& c : cases) {
    const auto o = run(c);
    EXPECT_EQ(reason(o), "invalid_arg") << c;
  }
  EXPECT_EQ(fan.current(), 0);
}

TEST_F(CommandServiceTest, LeaseIgnoredForOtherOps) {
  EXPECT_EQ(status(run(make_cmd(ulid(31), "get_status", "", "auto", R"("lease_s":"bad")"))), "done");
  EXPECT_EQ(status(run(make_cmd(ulid(32), "set_mode", R"({"mode":"manual"})", "auto", R"("lease_s":0)"))), "done");
}

TEST_F(CommandServiceTest, DuplicateReturnsPreviousPayloadWithoutReExecuting) {
  const auto first = run(make_cmd(ulid(40), "set_actuator", R"({"id":"fan_main","level":600})", "auto", R"("lease_s":30)"));
  (void)run(make_cmd(ulid(41), "set_actuator", R"({"id":"fan_main","level":300})", "auto", R"("lease_s":30)"));
  const auto dup = run(make_cmd(ulid(40), "set_actuator", R"({"id":"fan_main","level":600})", "auto", R"("lease_s":30)"));
  EXPECT_TRUE(dup.duplicate);
  EXPECT_EQ(dup.ack_payload.view(), first.ack_payload.view());
  EXPECT_EQ(fan.current(), 300);  // 実行し直さない
}

TEST_F(CommandServiceTest, DuplicateOfRejectedIsRememberedToo) {
  const auto first = run(make_cmd(ulid(42), "set_param", ""));
  const auto dup = run(make_cmd(ulid(42), "set_param", ""));
  EXPECT_TRUE(dup.duplicate);
  EXPECT_EQ(dup.ack_payload.view(), first.ack_payload.view());
}

TEST_F(CommandServiceTest, UnreadableMessagesAreDiscarded) {
  EXPECT_EQ(run("garbage").kind, CommandOutcome::Kind::Discarded);
  EXPECT_EQ(run("garbage").discard, infra::DiscardReason::NotJson);
  EXPECT_EQ(run(make_cmd("bad", "get_status", "")).discard, infra::DiscardReason::NoCmdId);
}

}  // namespace
}  // namespace node::app
