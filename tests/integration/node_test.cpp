// ホスト結合テスト：FakeMqttLink と仮想時計で Node（アプリ全体）を動かす。
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <memory>
#include <string>

#include "app/node.hpp"
#include "drivers/sim/sim_sensor.hpp"
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
constexpr ActuatorSpec kFan{"fan_main", 0, 1000, {}, {}};
constexpr ActuatorSpec kLed{"led_grow", 0, 1000, {}, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}, "soil"};
constexpr ActuatorSpec kBuzzer{"buzzer", 0, 1000, {}, {}};

constexpr std::string_view kCmdTopic = "iot/v1/home/mcu-cpp-01/cmd";

class NodeTest : public ::testing::Test {
 protected:
  test::FakeClock clock;
  test::FakeRandom rng;
  test::FakeMqttLink link;
  test::CapturingLog log;
  drivers::SimSensor bme{"bme280", kBme, kBmeVal, 2000ms};
  drivers::SimSensor lux{"bh1750", kLux, kLuxVal, 1000ms};
  drivers::SimSensor soil{"soil_moisture", kSoil, kSoilVal, 10000ms};
  std::array<domain::ISensor*, 3> sensors{&bme, &lux, &soil};
  drivers::SimActuator fan{kFan}, led{kLed}, pump{kPump}, buzzer{kBuzzer};
  std::array<domain::IActuator*, 4> actuators{&fan, &led, &pump, &buzzer};
  std::unique_ptr<Node> node;

  void SetUp() override {
    NodeConfig cfg{};
    cfg.password = "secret";
    node = std::make_unique<Node>(cfg, domain::DeviceRegistry(sensors, actuators), link, clock, rng, log);
    ASSERT_TRUE(node->start());
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

}  // namespace
}  // namespace node::app
