#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>

#include "app/event_service.hpp"
#include "app/publisher.hpp"
#include "app/state_service.hpp"
#include "app/telemetry_service.hpp"
#include "drivers/sim/sim_sensor.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

using namespace std::chrono_literals;
using domain::ActuatorSpec;
using domain::ChannelSpec;
using domain::Unit;
using test::json_value;
using test::payload_of;

constexpr std::array<ChannelSpec, 2> kFastCh{{{"lux", Unit::Lux, 1000}, {"temp_air", Unit::DegC, 1000}}};
constexpr std::array<std::int32_t, 2> kFastVal{300000, 25000};
constexpr std::array<ChannelSpec, 1> kSlowCh{{{"soil", Unit::Pct, 10000}}};
constexpr std::array<std::int32_t, 1> kSlowVal{40000};
constexpr ActuatorSpec kFan{"fan_main", 0, 1000, {}, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}, "soil"};

class ServicesTest : public ::testing::Test {
 protected:
  test::FakeClock clock;
  test::FakeRandom rng;
  test::FakeMqttLink link;
  test::CapturingLog log;
  infra::EnvelopeFactory env{"mcu-cpp-01", clock, rng};
  infra::Topics topics{"home", "mcu-cpp-01"};
  Publisher pub{link, env, topics, clock, log};

  drivers::SimSensor fast{"fast", kFastCh, kFastVal, 1000ms};
  drivers::SimSensor slow{"slow", kSlowCh, kSlowVal, 10000ms};
  std::array<domain::ISensor*, 2> sensors{&fast, &slow};
  drivers::SimActuator fan{kFan};
  drivers::SimActuator pump{kPump};
  std::array<domain::IActuator*, 2> actuators{&fan, &pump};
  ControlArbiter arbiter{actuators};
  domain::ReadingStore store{};

  void SetUp() override {
    link.set_connected(true);
    pub.set_online(true);
  }
};

// ---- Publisher

TEST_F(ServicesTest, PublisherCommitsSeqOnlyWhenSent) {
  link.set_connected(false);
  auto r = pub.publish(infra::Kind::Event, [](infra::JsonWriter& w) { w.begin_object().end_object(); });
  EXPECT_FALSE(r);
  EXPECT_EQ(env.peek_seq(infra::Kind::Event), 0U);
  link.set_connected(true);
  ASSERT_TRUE(pub.publish(infra::Kind::Event, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  EXPECT_EQ(env.peek_seq(infra::Kind::Event), 1U);
  ASSERT_EQ(link.published.size(), 1U);
  EXPECT_EQ(link.published[0].topic, "iot/v1/home/mcu-cpp-01/event");
  EXPECT_EQ(link.published[0].qos, infra::Qos::AtLeastOnce);
  EXPECT_FALSE(link.published[0].retain);
  ASSERT_FALSE(log.lines.empty());
  EXPECT_EQ(log.lines.back().event, "tx");
}

TEST_F(ServicesTest, PublisherDropsTelemetryOverRateButQueuesOthers) {
  for (int i = 0; i < 55; ++i) {
    (void)pub.publish(infra::Kind::Telemetry, [](infra::JsonWriter& w) { w.begin_object().end_object(); });
  }
  EXPECT_EQ(link.on("telemetry").size(), 50U);
  EXPECT_EQ(pub.counters().telemetry_rate_dropped, 5U);
  EXPECT_EQ(env.peek_seq(infra::Kind::Telemetry), 50U);  // 捨てた分は seq を使わない

  for (int i = 0; i < 52; ++i) {
    ASSERT_TRUE(pub.publish(infra::Kind::Event, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  }
  EXPECT_EQ(link.on("event").size(), 50U);
  EXPECT_EQ(pub.retry_pending(), 2U);
  clock.advance(1000);
  pub.flush_retries();
  EXPECT_EQ(link.on("event").size(), 52U);
  EXPECT_EQ(pub.retry_pending(), 0U);
}

TEST_F(ServicesTest, PublisherQueuesWhenInflightIsFull) {
  link.auto_puback = false;
  for (std::size_t i = 0; i < infra::IMqttLink::kMaxInflight; ++i) {
    ASSERT_TRUE(pub.publish(infra::Kind::CmdAck, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  }
  ASSERT_TRUE(pub.publish(infra::Kind::CmdAck, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  EXPECT_EQ(link.on("cmd/ack").size(), 32U);
  EXPECT_EQ(pub.retry_pending(), 1U);
  link.puback_all();
  pub.flush_retries();
  EXPECT_EQ(link.on("cmd/ack").size(), 33U);
  // seq は送った順に連番
  EXPECT_EQ(json_value(link.on("cmd/ack").back().payload, "seq"), "32");
}

TEST_F(ServicesTest, PublisherWaitsUntilNodeMarksOnline) {
  pub.set_online(false);  // リンクは接続中でも、Node が接続を受け付けるまでは送らない
  EXPECT_FALSE(pub.publish(infra::Kind::Telemetry, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  EXPECT_TRUE(link.published.empty());
}

TEST_F(ServicesTest, PublisherUncountedDoesNotAdvanceSeq) {
  ASSERT_TRUE(pub.publish_uncounted(infra::Kind::State, "{}"));
  EXPECT_EQ(env.peek_seq(infra::Kind::State), 0U);
  EXPECT_TRUE(link.published.back().retain);
}

// ---- TelemetryService

TEST_F(ServicesTest, TelemetryIncludesDueChannelsAndAllActuators) {
  TelemetryService t(sensors, arbiter, store);
  t.start(clock.monotonic_ms());
  t.tick(clock.monotonic_ms(), pub);  // 最初はすべて計測する
  auto tel = link.on("telemetry");
  ASSERT_EQ(tel.size(), 1U);
  std::string p = payload_of(tel[0].payload);
  EXPECT_NE(p.find(R"({"channel":"lux","value":300.0,"unit":"lux","quality":"good"})"), std::string::npos);
  EXPECT_NE(p.find(R"("channel":"soil")"), std::string::npos);
  EXPECT_NE(p.find(R"({"id":"fan_main","level":0,"owner":"none"})"), std::string::npos);
  EXPECT_NE(p.find(R"({"id":"pump","level":0,"owner":"none"})"), std::string::npos);

  clock.advance(500);
  t.tick(clock.monotonic_ms(), pub);
  EXPECT_EQ(link.on("telemetry").size(), 1U);  // まだ周期が来ていない

  clock.advance(500);
  t.tick(clock.monotonic_ms(), pub);
  tel = link.on("telemetry");
  ASSERT_EQ(tel.size(), 2U);
  p = payload_of(tel[1].payload);
  EXPECT_NE(p.find(R"("channel":"lux")"), std::string::npos);
  EXPECT_EQ(p.find(R"("channel":"soil")"), std::string::npos);  // 10秒周期はまだ
  EXPECT_NE(p.find(R"("id":"pump")"), std::string::npos);       // アクチュエーターは毎回全部

  for (int i = 0; i < 9; ++i) {
    clock.advance(1000);
    t.tick(clock.monotonic_ms(), pub);
  }
  EXPECT_NE(payload_of(link.on("telemetry").back().payload).find(R"("channel":"soil")"), std::string::npos);
  EXPECT_EQ(tel[0].qos, infra::Qos::AtMostOnce);
}

TEST_F(ServicesTest, TelemetryReflectsOwnerAndUpdatesStore) {
  TelemetryService t(sensors, arbiter, store);
  ControlArbiter::InterlockChanges ch{};
  (void)arbiter.set_command(0, 640, domain::Priority::Schedule, 60, clock.monotonic_ms(), ch);
  slow.set_quality(domain::Quality::Fault);
  t.start(clock.monotonic_ms());
  t.tick(clock.monotonic_ms(), pub);
  const std::string tel = link.on("telemetry").back().payload;
  EXPECT_EQ(test::actuator_in(tel, "fan_main"), std::make_pair(640, std::string("schedule")));
  ASSERT_TRUE(store.latest("soil").has_value());
  EXPECT_EQ(store.latest("soil")->quality, domain::Quality::Fault);
}

TEST_F(ServicesTest, TelemetryNotSentWhileDisconnected) {
  link.set_connected(false);
  TelemetryService t(sensors, arbiter, store);
  t.start(clock.monotonic_ms());
  t.tick(clock.monotonic_ms(), pub);
  EXPECT_TRUE(link.published.empty());
  EXPECT_TRUE(store.latest("lux").has_value());  // 計測は続ける
}

// ---- StateService

TEST_F(ServicesTest, StatePayloadHasChannelsActuatorsAndFw) {
  StateService s(sensors, arbiter, "0.1");
  std::array<char, 1024> buf{};
  infra::JsonWriter w(buf);
  s.write_payload(w, true, domain::Mode::Auto);
  ASSERT_TRUE(w.ok());
  EXPECT_EQ(w.view(),
            R"({"status":"online","mode":"auto","channels":[{"channel":"lux","unit":"lux","period_ms":1000},)"
            R"({"channel":"temp_air","unit":"degC","period_ms":1000},{"channel":"soil","unit":"pct","period_ms":10000}],)"
            R"("actuators":[{"id":"fan_main","min":0,"max":1000,"max_on_s":null},{"id":"pump","min":0,"max":1000,"max_on_s":30}],)"
            R"("fw_version":"0.1"})");
  infra::JsonWriter w2(buf);
  s.write_payload(w2, false, domain::Mode::PowerSave);
  EXPECT_EQ(json_value(w2.view(), "status"), "offline");
  EXPECT_EQ(json_value(w2.view(), "mode"), "power_save");
}

// ---- EventService

TEST_F(ServicesTest, EventsQueueWhileDisconnectedAndFlushInOrder) {
  EventService ev;
  link.set_connected(false);
  const infra::Stamp boot_stamp = env.now();
  ASSERT_TRUE(ev.post("BOOT", Severity::Info, R"({"reset_reason":"power_on"})", boot_stamp));
  clock.advance(500);
  ASSERT_TRUE(ev.post("MODE_CHANGED", Severity::Info, "", env.now()));
  ev.flush(pub);
  EXPECT_EQ(ev.pending(), 2U);
  EXPECT_TRUE(link.published.empty());

  link.set_connected(true);
  clock.advance(5000);
  ev.flush(pub);
  EXPECT_EQ(ev.pending(), 0U);
  const auto sent = link.on("event");
  ASSERT_EQ(sent.size(), 2U);
  EXPECT_EQ(json_value(sent[0].payload, "code"), "BOOT");
  EXPECT_EQ(json_value(sent[0].payload, "severity"), "info");
  EXPECT_EQ(json_value(sent[0].payload, "reset_reason"), "power_on");
  EXPECT_EQ(json_value(sent[0].payload, "ts"), "2026-11-20T06:00:13.345Z");  // 発生時刻
  EXPECT_EQ(json_value(sent[1].payload, "code"), "MODE_CHANGED");
  EXPECT_EQ(sent[1].payload.find("detail"), std::string::npos);
}

TEST_F(ServicesTest, EventsOverflowDropsNewest) {
  EventService ev;
  for (std::size_t i = 0; i < EventService::kCapacity; ++i) {
    ASSERT_TRUE(ev.post("X", Severity::Info, "", env.now()));
  }
  EXPECT_FALSE(ev.post("Y", Severity::Info, "", env.now()));
  EXPECT_EQ(ev.dropped(), 1U);
  EXPECT_EQ(ev.pending(), EventService::kCapacity);
  EXPECT_EQ(to_string(Severity::Warning), "warning");
}

}  // namespace
}  // namespace node::app
