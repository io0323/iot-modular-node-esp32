// ホスト結合テスト：FakeMqttLink と仮想時計で Node（アプリ全体）を動かす。
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "app/node.hpp"
#include "infra/formats.hpp"
#include "config/devices.hpp"
#include "domain/pump_interlock.hpp"
#include "drivers/sensor_io.hpp"
#include "drivers/sim/sim_sensor.hpp"
#include "drivers/ui_io.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

using namespace std::chrono_literals;
using domain::ActuatorSpec;
using domain::ChannelSpec;
using domain::Unit;
using test::actuator_in;
using test::json_value;
using test::make_cmd;
using test::ulid;

constexpr std::array<ChannelSpec, 3> kBme{{{"temp_air", Unit::DegC, 2000},
                                           {"humidity", Unit::PctRh, 2000},
                                           {"pressure", Unit::HPa, 2000}}};
constexpr std::array<std::int32_t, 3> kBmeVal{25000, 50000, 1013250};
constexpr std::array<ChannelSpec, 1> kLux{{{"lux", Unit::Lux, 1000}}};
constexpr std::array<std::int32_t, 1> kLuxVal{300000};
constexpr std::array<ChannelSpec, 1> kSoil{{{"soil", Unit::Pct, 10000}}};
constexpr std::array<std::int32_t, 1> kSoilVal{40000};
constexpr ActuatorSpec kFan{"fan_main", 0, 1000, {}};
constexpr ActuatorSpec kLed{"led_grow", 0, 1000, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}};
constexpr ActuatorSpec kBuzzer{"buzzer", 0, 1000, {}};

constexpr std::string_view kCmdTopic = "iot/v1/home/mcu-cpp-01/cmd";

class NodeTest : public ::testing::Test {
 protected:
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeNetwork network;
  test::FakeRandom rng;
  test::FakeMqttLink link;
  test::CapturingLog log;
  drivers::SimSensor bme{"bme280", kBme, kBmeVal, 2000ms};
  drivers::SimSensor lux{"bh1750", kLux, kLuxVal, 1000ms};
  drivers::SimSensor soil{"soil_moisture", kSoil, kSoilVal, 10000ms};
  std::array<domain::ISensor*, 3> sensors{&bme, &lux, &soil};
  drivers::SimActuator fan{kFan}, led{kLed}, pump{kPump}, buzzer{kBuzzer};
  // 構成表（src/config/devices.cpp）と同じインターロックとルール。状態を持つのでテストごとに作る
  domain::FanTempInterlock fan_temp_high{config::kFanTempHigh};
  domain::SensorFaultInterlock fan_on_temp_fault{config::kFanOnTempFault};
  domain::PumpInterlock pump_max_on{config::kPumpMaxOnS};
  domain::SensorFaultInterlock pump_off_on_soil_fault{config::kPumpOffOnSoilFault};
  std::array<domain::IInterlock*, 2> fan_interlocks{&fan_temp_high, &fan_on_temp_fault};
  std::array<domain::IInterlock*, 2> pump_interlocks{&pump_max_on, &pump_off_on_soil_fault};
  std::array<domain::ActuatorBinding, 4> actuators{
      {{&fan, fan_interlocks}, {&led, {}}, {&pump, pump_interlocks}, {&buzzer, {}}}};
  domain::DeviceRegistry registry{sensors, actuators, config::kRules};
  drivers::SensorIo sensor_io{registry.sensors()};
  drivers::UiIo ui_io{registry.ui()};
  std::unique_ptr<Node> node;

  void SetUp() override {
    NodeConfig cfg{};
    cfg.password = "secret";
    node = std::make_unique<Node>(cfg, registry, sensor_io, ui_io, link, network, sync, clock, rng, log);
    ASSERT_TRUE(node->start());
  }

  /// 条件が成り立つまで 10ms 刻みで進める（最大 limit_ms）
  template <typename Pred>
  bool run_until(Pred pred, std::uint64_t limit_ms) {
    for (std::uint64_t t = 0; t < limit_ms; t += 10) {
      clock.advance(10);
      node->tick();
      if (pred()) return true;
    }
    return false;
  }
  [[nodiscard]] std::vector<test::FakeMqttLink::Published> events_with(std::string_view code) const {
    std::vector<test::FakeMqttLink::Published> r;
    for (const auto& e : link.on("event")) {
      if (json_value(e.payload, "code") == code) r.push_back(e);
    }
    return r;
  }

  /// 10ms 刻みで ms だけ進める
  void run_for(std::uint64_t ms) {
    for (std::uint64_t t = 0; t < ms; t += 10) {
      clock.advance(10);
      node->tick();
    }
  }
  void send(const std::string& json) { link.inbound.emplace_back(std::string(kCmdTopic), json); }

  [[nodiscard]] std::string last_ack() const { return link.on("cmd/ack").back().payload; }
  [[nodiscard]] std::string last_telemetry() const { return link.on("telemetry").back().payload; }
};

TEST(NodeStart, SensorInitFailureDoesNotStopStartAndBecomesSensorFault) {
  // #130：センサーの初期化に失敗しても起動を止めず、BOOT を入れ、最初の計測で SENSOR_FAULT にする
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeNetwork network;
  test::FakeRandom rng;
  test::FakeMqttLink link;
  test::CapturingLog log;
  drivers::SimSensor soil{"soil_moisture", kSoil, kSoilVal, 10000ms};
  std::array<domain::ISensor*, 1> sensors{&soil};
  drivers::SimActuator pump{kPump};
  std::array<domain::ActuatorBinding, 1> actuators{{{&pump, {}}}};
  const domain::DeviceRegistry registry(sensors, actuators);
  drivers::SensorIo sensor_io{registry.sensors()};
  drivers::UiIo ui_io{registry.ui()};
  soil.set_fault(true);  // init も read も失敗する
  Node node(NodeConfig{}, registry, sensor_io, ui_io, link, network, sync, clock, rng, log);
  ASSERT_TRUE(node.start());
  EXPECT_EQ(node.pending_events(), 1U);  // BOOT
  bool logged = false;
  for (const auto& l : log.lines) logged |= l.event == "init_failed" && l.fields.find("soil_moisture") != std::string::npos;
  EXPECT_TRUE(logged);
  for (int i = 0; i < 20; ++i) {
    clock.advance(10);
    node.tick();
  }
  const auto events = link.on("event");
  ASSERT_GE(events.size(), 2U);
  EXPECT_EQ(json_value(events[0].payload, "code"), "BOOT");
  EXPECT_EQ(json_value(events[1].payload, "code"), "SENSOR_FAULT");
  EXPECT_EQ(json_value(events[1].payload, "channel"), "soil");
}

TEST_F(NodeTest, ConnectsWithLwtThenSendsOnlineStateThenBoot) {
  node->tick();
  ASSERT_EQ(link.connects.size(), 1U);
  const auto& c = link.connects[0];
  EXPECT_EQ(c.client_id, "mcu-cpp-01");
  EXPECT_EQ(c.username, "mcu-cpp-01");
  EXPECT_EQ(c.password, "secret");
  EXPECT_EQ(c.keepalive_s, 10);
  EXPECT_EQ(c.will_topic, "iot/v1/home/mcu-cpp-01/state");
  EXPECT_EQ(c.will_qos, infra::Qos::AtLeastOnce);
  EXPECT_TRUE(c.will_retain);
  EXPECT_EQ(json_value(c.will_payload, "status"), "offline");
  EXPECT_EQ(json_value(c.will_payload, "seq"), "0");
  EXPECT_EQ(json_value(c.will_payload, "boot_id"), std::string(node->boot_id()));
  EXPECT_EQ(json_value(c.will_payload, "ts"), "2026-11-20T06:00:13.345Z");  // 接続時刻

  node->tick();  // Connected を受けて購読・state・BOOT
  ASSERT_EQ(link.subscriptions.size(), 1U);
  EXPECT_EQ(link.subscriptions[0], kCmdTopic);
  ASSERT_GE(link.published.size(), 2U);
  EXPECT_EQ(link.published[0].topic, "iot/v1/home/mcu-cpp-01/state");
  EXPECT_TRUE(link.published[0].retain);
  EXPECT_EQ(json_value(link.published[0].payload, "status"), "online");
  EXPECT_EQ(json_value(link.published[0].payload, "seq"), "0");
  EXPECT_EQ(link.published[1].topic, "iot/v1/home/mcu-cpp-01/event");
  EXPECT_EQ(json_value(link.published[1].payload, "code"), "BOOT");
  EXPECT_EQ(json_value(link.published[1].payload, "reset_reason"), "power_on");
  EXPECT_EQ(json_value(link.published[1].payload, "platform_reason"), "HOST");
}

TEST_F(NodeTest, LwtIsRebuiltOnReconnectWithNextSeq) {
  run_for(100);
  link.drop();
  run_for(1100);  // 1秒後に接続し直す
  ASSERT_EQ(link.connects.size(), 2U);
  const auto& c = link.connects[1];
  EXPECT_EQ(json_value(c.will_payload, "seq"), "1");  // online の state を1件送った後
  EXPECT_EQ(json_value(c.will_payload, "boot_id"), std::string(node->boot_id()));
  // 切断を 1110ms の tick で受け、その1秒後（2110ms）に接続し直す。ts は接続し直した時刻
  EXPECT_EQ(json_value(c.will_payload, "ts"), "2026-11-20T06:00:14.455Z");
}

TEST_F(NodeTest, SetActuatorAckTelemetryOwnerAndLeaseExpiry) {
  run_for(100);
  send(make_cmd(ulid(1), "set_actuator", R"({"id":"fan_main","level":700})", "auto", R"("lease_s":3)"));
  run_for(10);
  ASSERT_EQ(link.on("cmd/ack").size(), 1U);
  EXPECT_EQ(json_value(last_ack(), "cmd_id"), ulid(1));
  EXPECT_EQ(json_value(last_ack(), "status"), "done");
  EXPECT_EQ(json_value(last_ack(), "level"), "700");
  EXPECT_EQ(fan.current(), 700);

  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(700, std::string("auto")));
  run_for(3000);  // リースが切れる
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(0, std::string("none")));
  EXPECT_EQ(fan.current(), 0);
}

TEST_F(NodeTest, DuplicateCmdIdGetsSamePayloadWithNewEnvelope) {
  run_for(100);
  send(make_cmd(ulid(2), "set_actuator", R"({"id":"fan_main","level":600})", "auto", R"("lease_s":30)"));
  run_for(10);
  send(make_cmd(ulid(3), "set_actuator", R"({"id":"fan_main","level":300})", "auto", R"("lease_s":30)"));
  run_for(10);
  send(make_cmd(ulid(2), "set_actuator", R"({"id":"fan_main","level":600})", "auto", R"("lease_s":30)"));
  run_for(10);
  const auto acks = link.on("cmd/ack");
  ASSERT_EQ(acks.size(), 3U);
  EXPECT_EQ(test::payload_of(acks[2].payload), test::payload_of(acks[0].payload));
  EXPECT_NE(json_value(acks[2].payload, "msg_id"), json_value(acks[0].payload, "msg_id"));
  EXPECT_EQ(json_value(acks[0].payload, "seq"), "0");
  EXPECT_EQ(json_value(acks[2].payload, "seq"), "2");
  EXPECT_EQ(fan.current(), 300);  // 実行し直さない
}

TEST_F(NodeTest, PumpLimitedThenStoppedAt30sWithInterlockEvent) {
  run_for(100);
  send(make_cmd(ulid(4), "set_actuator", R"({"id":"pump","level":1000})", "auto", R"("lease_s":60)"));
  run_for(10);
  EXPECT_EQ(json_value(last_ack(), "limited"), "true");
  EXPECT_EQ(json_value(last_ack(), "level"), "1000");
  EXPECT_EQ(pump.current(), 1000);

  run_for(29'900);
  EXPECT_EQ(pump.current(), 1000);
  EXPECT_TRUE(link.on("event").size() == 1U);  // BOOT だけ
  run_for(200);
  EXPECT_EQ(pump.current(), 0);
  const auto events = link.on("event");
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(json_value(events[1].payload, "code"), "INTERLOCK_ACTIVE");
  EXPECT_EQ(json_value(events[1].payload, "severity"), "warning");
  EXPECT_EQ(json_value(events[1].payload, "id"), "pump");
  EXPECT_EQ(json_value(events[1].payload, "rule"), "max_on_s");
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "pump"), std::make_pair(0, std::string("interlock")));

  // 上位がリースを更新し続けても再開しない
  send(make_cmd(ulid(5), "set_actuator", R"({"id":"pump","level":1000})", "auto", R"("lease_s":20)"));
  run_for(10);
  EXPECT_EQ(json_value(last_ack(), "limited"), "true");
  EXPECT_EQ(json_value(last_ack(), "level"), "0");
  EXPECT_EQ(pump.current(), 0);

  // release で要求が0に戻り、停止から60秒で解除（INTERLOCK_RELEASED を1件）
  send(make_cmd(ulid(6), "release", R"({"id":"pump"})"));
  run_for(60'000);
  const auto after = link.on("event");
  ASSERT_EQ(after.size(), 3U);
  EXPECT_EQ(json_value(after[2].payload, "code"), "INTERLOCK_RELEASED");
  EXPECT_EQ(json_value(after[2].payload, "severity"), "info");
  EXPECT_EQ(actuator_in(last_telemetry(), "pump"), std::make_pair(0, std::string("none")));
}

TEST_F(NodeTest, SetModeSendsModeChangedAndState) {
  run_for(100);
  const auto states_before = link.on("state").size();
  send(make_cmd(ulid(7), "set_mode", R"({"mode":"manual"})"));
  run_for(10);
  EXPECT_EQ(node->mode(), domain::Mode::Manual);
  const auto states = link.on("state");
  ASSERT_EQ(states.size(), states_before + 1);
  EXPECT_EQ(json_value(states.back().payload, "mode"), "manual");
  EXPECT_EQ(json_value(link.on("event").back().payload, "code"), "MODE_CHANGED");

  send(make_cmd(ulid(8), "get_status", ""));
  run_for(10);
  EXPECT_EQ(link.on("state").size(), states_before + 2);
}

TEST_F(NodeTest, DiscardsUnreadableAndCountsByReason) {
  run_for(100);
  send("not json");
  send(make_cmd("bad", "get_status", ""));
  link.inbound.emplace_back("iot/v1/home/other/cmd", make_cmd(ulid(9), "get_status", ""));
  run_for(10);
  EXPECT_TRUE(link.on("cmd/ack").empty());
  EXPECT_EQ(node->discarded(infra::DiscardReason::NotJson), 1U);
  EXPECT_EQ(node->discarded(infra::DiscardReason::NoCmdId), 1U);
}

TEST_F(NodeTest, EventsWaitUntilConnected) {
  link.accept_connect = false;
  run_for(2000);
  EXPECT_TRUE(link.published.empty());
  EXPECT_EQ(node->pending_events(), 1U);  // BOOT
  link.accept_connect = true;
  run_for(1100);
  ASSERT_FALSE(link.on("event").empty());
  EXPECT_EQ(json_value(link.on("event")[0].payload, "code"), "BOOT");
}

TEST_F(NodeTest, ShutdownSendsOfflineStateWithoutAdvancingSeq) {
  run_for(100);
  const std::string online = link.on("state").back().payload;
  node->shutdown();
  const auto states = link.on("state");
  const std::string offline = states.back().payload;
  EXPECT_EQ(json_value(offline, "status"), "offline");
  EXPECT_TRUE(states.back().retain);
  EXPECT_EQ(json_value(offline, "seq"), "1");  // 次に使う値。seq は進めない
  // その後の online の state も同じ seq を使う
  send(make_cmd(ulid(10), "get_status", ""));
  run_for(10);
  EXPECT_EQ(json_value(link.on("state").back().payload, "seq"), "1");
  EXPECT_EQ(json_value(online, "seq"), "0");
}

TEST_F(NodeTest, TelemetryCadenceFollowsChannelPeriods) {
  run_for(100);
  const auto before = link.on("telemetry").size();
  run_for(10'000);
  const auto tel = link.on("telemetry");
  EXPECT_EQ(tel.size() - before, 10U);  // lux の 1000ms ごと
  int with_temp = 0;
  int with_soil = 0;
  for (std::size_t i = before; i < tel.size(); ++i) {
    with_temp += tel[i].payload.find("\"temp_air\"") != std::string::npos ? 1 : 0;
    with_soil += tel[i].payload.find("\"soil\"") != std::string::npos ? 1 : 0;
  }
  EXPECT_EQ(with_temp, 5);  // 2000ms ごと
  EXPECT_EQ(with_soil, 1);  // 10000ms ごと
}

// ---- P2 のシナリオ

TEST_F(NodeTest, Scenario1_HighTempForcesFanThenReleasesBackToRule) {
  run_for(100);
  bme.set_value(0, 46000);
  ASSERT_TRUE(run_until([&] { return fan.current() == 1000; }, 2100));
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(1000, std::string("interlock")));
  auto active = events_with("INTERLOCK_ACTIVE");
  ASSERT_EQ(active.size(), 1U);
  EXPECT_EQ(json_value(active[0].payload, "id"), "fan_main");
  EXPECT_EQ(json_value(active[0].payload, "rule"), "temp_high");
  EXPECT_EQ(json_value(active[0].payload, "severity"), "warning");

  bme.set_value(0, 42000);
  run_for(9000);
  EXPECT_EQ(fan.current(), 1000);  // 43℃未満がまだ10秒続いていない
  EXPECT_TRUE(events_with("INTERLOCK_RELEASED").empty());
  ASSERT_TRUE(run_until([&] { return fan.current() != 1000; }, 4000));
  const auto released = events_with("INTERLOCK_RELEASED");
  ASSERT_EQ(released.size(), 1U);
  EXPECT_EQ(json_value(released[0].payload, "rule"), "temp_high");
  EXPECT_EQ(events_with("INTERLOCK_ACTIVE").size(), 1U);
  // ルールの判定に戻る：cool-on-high-temp は 46℃で入り、42℃（28℃以上）では入ったまま
  EXPECT_EQ(fan.current(), 600);
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(600, std::string("local")));
}

TEST_F(NodeTest, Scenario2_DarkTurnsLedOnThenPowerSaveTurnsItOff) {
  run_for(100);
  lux.set_value(0, 150000);
  ASSERT_TRUE(run_until([&] { return led.current() == 1000; }, 1100));
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "led_grow"), std::make_pair(1000, std::string("local")));
  EXPECT_TRUE(events_with("INTERLOCK_ACTIVE").empty());  // ルールの発火ではイベントを出さない

  send(make_cmd(ulid(20), "set_mode", R"({"mode":"power_save"})"));
  run_for(10);
  EXPECT_EQ(node->mode(), domain::Mode::PowerSave);
  EXPECT_EQ(led.current(), 0);
  const std::string state = link.on("state").back().payload;
  EXPECT_EQ(json_value(state, "mode"), "power_save");
  EXPECT_NE(state.find(R"({"channel":"lux","unit":"lux","period_ms":5000})"), std::string::npos);
  EXPECT_NE(state.find(R"({"channel":"temp_air","unit":"degC","period_ms":10000})"), std::string::npos);
  const auto mc = events_with("MODE_CHANGED");
  ASSERT_EQ(mc.size(), 1U);
  EXPECT_EQ(json_value(mc[0].payload, "from"), "auto");
  EXPECT_EQ(json_value(mc[0].payload, "to"), "power_save");
  EXPECT_EQ(json_value(mc[0].payload, "cause"), "cmd");
  // 省電力モードの telemetry は5秒ごと。ファンは stale にならない（しきい値も5倍。#39）
  const auto before = link.on("telemetry").size();
  run_for(20'000);
  EXPECT_EQ(link.on("telemetry").size() - before, 4U);
  EXPECT_EQ(fan.current(), 0);
  EXPECT_EQ(actuator_in(last_telemetry(), "led_grow"), std::make_pair(0, std::string("none")));
}

TEST_F(NodeTest, Scenario3_DrySoilPulsesPumpThenSensorFaultMakesPumpUnsafe) {
  run_for(100);
  soil.set_value(0, 25000);
  ASSERT_TRUE(run_until([&] { return pump.current() == 1000; }, 10'100));
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "pump"), std::make_pair(1000, std::string("local")));
  ASSERT_TRUE(run_until([&] { return pump.current() == 0; }, 5000));
  // クールダウン：パルスの開始から10分は動かない
  EXPECT_FALSE(run_until([&] { return pump.current() != 0; }, 590'000));
  EXPECT_TRUE(events_with("INTERLOCK_ACTIVE").empty());

  soil.set_fault(true);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_FAULT").empty(); }, 10'100));
  const auto faults = events_with("SENSOR_FAULT");
  ASSERT_EQ(faults.size(), 1U);
  EXPECT_EQ(json_value(faults[0].payload, "channel"), "soil");
  EXPECT_EQ(json_value(faults[0].payload, "severity"), "critical");
  // 故障中も telemetry は出続け、そのチャネルは fault（値は最後に good で読めた値。#52）
  const std::string tel_at_fault = last_telemetry();
  EXPECT_NE(tel_at_fault.find(R"({"channel":"soil","value":25.0,"unit":"pct","quality":"fault"})"), std::string::npos);
  const auto tel_count = link.on("telemetry").size();
  ASSERT_TRUE(run_until([&] { return link.on("telemetry").size() > tel_count + 10; }, 12'000));
  bool soil_fault_again = false;
  for (std::size_t i = tel_count; i < link.on("telemetry").size(); ++i) {
    soil_fault_again |= link.on("telemetry")[i].payload.find(R"("channel":"soil","value":25.0,"unit":"pct","quality":"fault")") !=
                        std::string::npos;
  }
  EXPECT_TRUE(soil_fault_again);  // 周期（10秒）ごとに fault で入る
  send(make_cmd(ulid(21), "set_actuator", R"({"id":"pump","level":1000})", "auto", R"("lease_s":10)"));
  run_for(10);
  EXPECT_EQ(json_value(last_ack(), "status"), "rejected");
  EXPECT_EQ(json_value(last_ack(), "reason"), "unsafe");
  EXPECT_EQ(pump.current(), 0);
  EXPECT_TRUE(events_with("INTERLOCK_ACTIVE").empty());  // 止まっていたポンプには出さない

  soil.set_fault(false);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_RECOVERED").empty(); }, 31'000));
  EXPECT_EQ(json_value(events_with("SENSOR_RECOVERED")[0].payload, "channel"), "soil");
}

TEST_F(NodeTest, Scenario4_ManualThenUpperCommandThenLeaseExpiryThenTimeout) {
  run_for(100);
  node->press_button();
  EXPECT_EQ(node->mode(), domain::Mode::Manual);
  EXPECT_EQ(node->set_manual("fan_main", 500), ManualControl::Outcome::Accepted);
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(500, std::string("local")));

  send(make_cmd(ulid(22), "set_actuator", R"({"id":"fan_main","level":800})", "auto", R"("lease_s":3)"));
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(800, std::string("auto")));
  run_for(3000);  // リースが切れると手動の値に戻る
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(500, std::string("local")));

  // 最後の手動操作から30分で自動に戻る
  ASSERT_TRUE(run_until([&] { return node->mode() == domain::Mode::Auto; }, 30 * 60 * 1000));
  const auto mc = events_with("MODE_CHANGED");
  ASSERT_EQ(mc.size(), 2U);
  EXPECT_EQ(json_value(mc[0].payload, "cause"), "button");
  EXPECT_EQ(json_value(mc[1].payload, "from"), "manual");
  EXPECT_EQ(json_value(mc[1].payload, "to"), "auto");
  EXPECT_EQ(json_value(mc[1].payload, "cause"), "timeout");
  run_for(10);
  EXPECT_EQ(fan.current(), 0);  // 手動の設定は消える
  EXPECT_EQ(node->set_manual("fan_main", 500), ManualControl::Outcome::NotManualMode);
}

TEST_F(NodeTest, AllSensorsFaultedStillSendTelemetryWithActuators) {
  run_for(100);
  bme.set_fault(true);
  lux.set_fault(true);
  soil.set_fault(true);
  run_for(10'000);
  const auto before = link.on("telemetry").size();
  run_for(5'000);
  ASSERT_GE(link.on("telemetry").size() - before, 5U);  // lux の周期（1秒）ごとに出る
  const std::string tel = last_telemetry();
  EXPECT_NE(tel.find(R"("channel":"lux","value":300.0,"unit":"lux","quality":"fault")"), std::string::npos);
  EXPECT_TRUE(actuator_in(tel, "fan_main").has_value());
  EXPECT_EQ(actuator_in(tel, "fan_main")->second, "interlock");  // 気温が読めないのでファンは1000（#22）
}

TEST_F(NodeTest, ButtonCyclesModesAndRepublishesStateEachTime) {
  run_for(100);
  const auto states = link.on("state").size();
  node->press_button();
  node->press_button();
  node->press_button();
  run_for(10);
  EXPECT_EQ(node->mode(), domain::Mode::Auto);
  EXPECT_EQ(link.on("state").size(), states + 3);
  EXPECT_EQ(events_with("MODE_CHANGED").size(), 3U);
}

TEST_F(NodeTest, TempSensorFaultForcesFanOn) {
  run_for(100);
  bme.set_fault(true);
  ASSERT_TRUE(run_until([&] { return fan.current() == 1000; }, 2100));
  const auto active = events_with("INTERLOCK_ACTIVE");
  ASSERT_EQ(active.size(), 1U);
  EXPECT_EQ(json_value(active[0].payload, "rule"), "sensor_fault");
  EXPECT_EQ(events_with("SENSOR_FAULT").size(), 3U);  // temp_air・humidity・pressure（#6）
  bme.set_fault(false);
  ASSERT_TRUE(run_until([&] { return fan.current() == 0; }, 31'000));
  EXPECT_EQ(events_with("INTERLOCK_RELEASED").size(), 1U);
  EXPECT_EQ(events_with("SENSOR_RECOVERED").size(), 3U);
}

// ---- P4：通信の作り込み

/// ts（ISO 8601）を UTC の ms に
std::int64_t ts_ms(const std::string& message) { return infra::parse_ts(json_value(message, "ts").value_or("")).value_or(-1); }

TEST_F(NodeTest, Nfr02_OutputAndAckFinishWithinTheTickThatReceivedCmd) {
  run_for(100);
  const auto acks = link.on("cmd/ack").size();
  send(make_cmd(ulid(20), "set_actuator", R"({"id":"fan_main","level":800})", "auto", R"("lease_s":30)"));
  node->tick();  // 時計を進めずに1周だけ
  EXPECT_EQ(fan.current(), 800);
  ASSERT_EQ(link.on("cmd/ack").size(), acks + 1);
  EXPECT_EQ(json_value(last_ack(), "status"), "done");
}

TEST_F(NodeTest, CmdAckIsNotResentAfterReconnect) {
  run_for(100);
  link.auto_puback = false;
  send(make_cmd(ulid(21), "set_actuator", R"({"id":"fan_main","level":500})", "auto", R"("lease_s":30)"));
  run_for(10);
  ASSERT_EQ(link.on("cmd/ack").size(), 1U);
  link.drop();
  link.auto_puback = true;
  run_for(1500);
  ASSERT_EQ(link.connects.size(), 2U);
  EXPECT_EQ(link.on("cmd/ack").size(), 1U);
}

TEST_F(NodeTest, NoLostOrRestoredBeforeFirstConnect) {
  link.accept_connect = false;
  run_for(5000);
  link.accept_connect = true;
  run_for(20000);
  EXPECT_FALSE(events_with("BOOT").empty());
  EXPECT_TRUE(events_with("UPSTREAM_LOST").empty());
  EXPECT_TRUE(events_with("UPSTREAM_RESTORED").empty());
}

TEST_F(NodeTest, P4Scenario1_SntpDelayHoldsEverythingThenSendsWithOccurrenceTs) {
  // 同期前は「同期の差」が分からない（ここでは 0）。10秒後に同期して差が分かる
  sync.set_synced(false);
  sync.set_offset(0);
  run_for(3000);
  soil.set_fault(true);  // 起動から3秒で土壌水分センサーが故障
  run_for(7000);
  EXPECT_TRUE(link.connects.empty());  // 同期まで MQTT に接続しない
  EXPECT_TRUE(link.published.empty());
  sync.set_offset(test::FakeTimeSync::kSampleTs);
  sync.set_synced(true);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_FAULT").empty(); }, 15000));

  ASSERT_GE(link.published.size(), 3U);
  EXPECT_EQ(json_value(link.published[0].payload, "status"), "online");  // state が最初
  const auto boot = events_with("BOOT");
  const auto fault = events_with("SENSOR_FAULT");
  ASSERT_EQ(boot.size(), 1U);
  EXPECT_EQ(json_value(boot[0].payload, "ts"), "2026-11-20T06:00:13.345Z");  // 起動時刻（単調時計 1000ms）
  EXPECT_EQ(json_value(boot[0].payload, "uptime_ms"), "0");
  EXPECT_EQ(json_value(boot[0].payload, "seq"), "0");
  EXPECT_EQ(json_value(fault[0].payload, "channel"), "soil");
  EXPECT_EQ(json_value(fault[0].payload, "seq"), "1");
  // SENSOR_FAULT の ts は「同期の差＋発生時の単調時計」、uptime_ms は発生時の値
  const std::uint64_t up = std::stoull(json_value(fault[0].payload, "uptime_ms").value_or("0"));
  EXPECT_GE(up, 3000U);
  EXPECT_EQ(ts_ms(fault[0].payload), test::FakeTimeSync::kSampleTs + 1000 + static_cast<std::int64_t>(up));
  EXPECT_TRUE(events_with("UPSTREAM_LOST").empty());
}

TEST_F(NodeTest, P4Scenario2_FortyEventsDuringOutageKeepThirtyTwoAndRestoredCountsEight) {
  run_for(100);
  ASSERT_EQ(events_with("BOOT").size(), 1U);
  link.accept_connect = false;
  link.drop();
  run_for(10);  // 切断を受ける（ここが切れた時刻）
  const std::uint64_t lost_mono = clock.monotonic_ms();
  const std::size_t before = link.published.size();
  for (int i = 0; i < 40; ++i) {
    node->press_button();  // MODE_CHANGED を1件
    run_for(10);
  }
  run_for(3000);
  link.accept_connect = true;
  ASSERT_TRUE(run_until([&] { return !events_with("UPSTREAM_RESTORED").empty(); }, 70000));

  std::vector<test::FakeMqttLink::Published> after(link.published.begin() + static_cast<std::ptrdiff_t>(before),
                                                   link.published.end());
  std::vector<std::string> kinds;
  for (const auto& p : after) {
    if (p.topic.ends_with("/state")) kinds.push_back("state");
    if (p.topic.ends_with("/event")) kinds.push_back(json_value(p.payload, "code").value_or(""));
  }
  ASSERT_GE(kinds.size(), 35U);
  EXPECT_EQ(kinds[0], "state");
  EXPECT_EQ(json_value(after[0].payload, "status"), "online");
  EXPECT_EQ(kinds[1], "UPSTREAM_LOST");
  for (std::size_t i = 2; i < 34; ++i) EXPECT_EQ(kinds[i], "MODE_CHANGED") << i;
  EXPECT_EQ(kinds[34], "UPSTREAM_RESTORED");
  const auto lost = events_with("UPSTREAM_LOST");
  EXPECT_EQ(ts_ms(lost[0].payload), test::FakeTimeSync::kSampleTs + static_cast<std::int64_t>(lost_mono));
  // 古いものから8件捨てた：残りの最初は9件目（10ms 刻みで押した）
  const auto modes = events_with("MODE_CHANGED");
  ASSERT_EQ(modes.size(), 32U);
  EXPECT_EQ(ts_ms(modes[0].payload) - ts_ms(lost[0].payload), 8 * 10);
  const auto restored = events_with("UPSTREAM_RESTORED");
  EXPECT_EQ(json_value(restored[0].payload, "dropped"), "8");
  EXPECT_EQ(json_value(restored[0].payload, "down_ms"),
            std::to_string(static_cast<std::uint64_t>(ts_ms(restored[0].payload) - ts_ms(lost[0].payload))));
  EXPECT_EQ(json_value(restored[0].payload, "severity"), "info");
  EXPECT_EQ(json_value(lost[0].payload, "severity"), "warning");
}

TEST_F(NodeTest, P4Scenario3_UnackedEventIsResentWithSameMsgIdAfterReconnect) {
  run_for(100);
  link.auto_puback = false;
  node->press_button();
  run_for(10);
  const auto sent = events_with("MODE_CHANGED");
  ASSERT_EQ(sent.size(), 1U);
  link.drop();  // PUBACK が来る前に切れた
  link.auto_puback = true;
  ASSERT_TRUE(run_until([&] { return !events_with("UPSTREAM_RESTORED").empty(); }, 5000));
  const auto again = events_with("MODE_CHANGED");
  ASSERT_EQ(again.size(), 2U);
  EXPECT_EQ(again[1].payload, sent[0].payload);  // msg_id・seq・ts も同じ電文
  EXPECT_EQ(json_value(again[1].payload, "msg_id"), json_value(sent[0].payload, "msg_id"));
}

TEST_F(NodeTest, NetworkDownDisconnectsMqttAndReconnectsAfterNetworkReturns) {
  run_for(100);
  network.go_down();
  run_for(10);
  EXPECT_EQ(link.disconnects, 1);
  run_for(5000);
  EXPECT_EQ(link.connects.size(), 1U);  // ネットワークが戻るまで MQTT は試さない
  network.go_up();
  ASSERT_TRUE(run_until([&] { return !events_with("UPSTREAM_RESTORED").empty(); }, 70000));
  EXPECT_EQ(link.connects.size(), 2U);
}

}  // namespace
}  // namespace node::app
