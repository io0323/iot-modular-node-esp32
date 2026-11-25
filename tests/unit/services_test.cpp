#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>

#include "app/publisher.hpp"
#include "app/command_service.hpp"
#include "config/devices.hpp"
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
constexpr ActuatorSpec kFan{"fan_main", 0, 1000, {}};
constexpr ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}};

class ServicesTest : public ::testing::Test {
 protected:
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeRandom rng;
  test::FakeMqttLink link;
  test::CapturingLog log;
  infra::EnvelopeFactory env{"mcu-cpp-01", clock, sync, rng};
  infra::Topics topics{"home", "mcu-cpp-01"};
  Publisher pub{link, env, topics, clock, log};

  drivers::SimSensor fast{"fast", kFastCh, kFastVal, 1000ms};
  drivers::SimSensor slow{"slow", kSlowCh, kSlowVal, 10000ms};
  std::array<domain::ISensor*, 2> sensors{&fast, &slow};
  drivers::SimActuator fan{kFan};
  drivers::SimActuator pump{kPump};
  domain::ReadingStore store{};
  std::array<domain::ActuatorBinding, 2> actuators{{{&fan, {}}, {&pump, {}}}};
  ControlArbiter arbiter{actuators, store};

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
    ASSERT_TRUE(pub.publish(infra::Kind::CmdAck, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  }
  EXPECT_EQ(link.on("cmd/ack").size(), 50U);
  EXPECT_EQ(pub.retry_pending(), 2U);
  EXPECT_EQ(env.peek_seq(infra::Kind::CmdAck), 52U);  // 送り直し待ちに入れた分も seq は決まっている
  clock.advance(1000);
  pub.flush_retries();
  EXPECT_EQ(link.on("cmd/ack").size(), 52U);
  EXPECT_EQ(pub.retry_pending(), 0U);
  EXPECT_EQ(json_value(link.on("cmd/ack").back().payload, "seq"), "51");
}

TEST_F(ServicesTest, PublisherDefersEventOverRateWithoutUsingSeq) {
  for (int i = 0; i < 50; ++i) {
    ASSERT_TRUE(pub.publish(infra::Kind::Event, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  }
  const auto r = pub.publish(infra::Kind::Event, [](infra::JsonWriter& w) { w.begin_object().end_object(); });
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, support::ErrorCode::Busy);  // event は EventService の保持に残る（#15）
  EXPECT_EQ(env.peek_seq(infra::Kind::Event), 50U);
  EXPECT_EQ(pub.counters().event_deferred, 1U);
  EXPECT_EQ(pub.retry_pending(), 0U);
}

TEST_F(ServicesTest, PublisherRetryQueueOverflowDropsAndCounts) {
  for (int i = 0; i < 50; ++i) {
    ASSERT_TRUE(pub.publish(infra::Kind::CmdAck, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  }
  for (std::size_t i = 0; i < Publisher::kRetryCapacity - 1; ++i) {
    ASSERT_TRUE(pub.publish(infra::Kind::CmdAck, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  }
  // state も同じ送り直し待ちに並ぶ
  ASSERT_TRUE(pub.publish(infra::Kind::State, [](infra::JsonWriter& w) { w.begin_object().end_object(); }));
  EXPECT_EQ(pub.retry_pending(), Publisher::kRetryCapacity);
  const auto r = pub.publish(infra::Kind::CmdAck, [](infra::JsonWriter& w) { w.begin_object().end_object(); });
  ASSERT_FALSE(r);
  EXPECT_EQ(r.error().code, support::ErrorCode::BufferFull);
  EXPECT_EQ(pub.counters().retry_overflow, 1U);
  EXPECT_EQ(env.peek_seq(infra::Kind::CmdAck), 57U);  // 捨てた分は seq を使わない
}

TEST_F(ServicesTest, PublisherReturnsTokenForQos1AndZeroForQos0) {
  const auto a = pub.publish(infra::Kind::Event, [](infra::JsonWriter& w) { w.begin_object().end_object(); });
  ASSERT_TRUE(a);
  EXPECT_NE(a.value().token, 0U);
  EXPECT_EQ(a.value().seq, 0U);
  const auto t = pub.publish(infra::Kind::Telemetry, [](infra::JsonWriter& w) { w.begin_object().end_object(); });
  ASSERT_TRUE(t);
  EXPECT_EQ(t.value().token, 0U);
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

TEST_F(ServicesTest, RetrySlotFitsWorstCaseStateAndCmdAck) {
  // P7（#116）：送り直し待ちに入るのは cmd_ack と state だけ。どちらも最悪の長さで kRetryMessageMax に収まる
  const domain::DeviceRegistry reg = config::devices();
  ControlArbiter arb(reg.actuators(), store);
  StateService state(reg.sensors(), arb, "0.1");
  test::FakeClock far;
  far.set(1000 + 0xFFFFFFFFULL);  // uptime_ms が10桁
  test::FakeTimeSync late;
  late.set_offset(253402300799000 - static_cast<std::int64_t>(far.monotonic_ms()));  // 9999-12-31T23:59:59.000Z
  infra::EnvelopeFactory e("mcu-cpp-01", far, late, rng);
  std::array<char, Publisher::kMessageMax> buf{};
  for (const auto mode : {domain::Mode::Auto, domain::Mode::Manual, domain::Mode::PowerSave}) {
    for (const bool online : {true, false}) {
      infra::JsonWriter w(buf);
      e.begin(w, infra::Kind::State, 0xFFFFFFFFU, infra::Stamp{1000 + 0xFFFFFFFFULL - 1});
      state.write_payload(w, online, mode, 5);
      w.end_object();
      ASSERT_TRUE(w.ok());
      EXPECT_LE(w.view().size(), Publisher::kRetryMessageMax) << static_cast<int>(mode);
    }
  }
  // cmd_ack：payload は kAckPayloadMax 以下なので、エンベロープの最悪の長さと足して収まる
  infra::JsonWriter w(buf);
  e.begin(w, infra::Kind::CmdAck, 0xFFFFFFFFU, infra::Stamp{1000 + 0xFFFFFFFFULL - 1});
  w.raw("{}");
  w.end_object();
  ASSERT_TRUE(w.ok());
  EXPECT_LE(w.view().size() - 2 + kAckPayloadMax, Publisher::kRetryMessageMax);
}

// ---- TelemetryService

TEST_F(ServicesTest, TelemetryIncludesGivenReadingsAndAllActuators) {
  TelemetryService t(arbiter);
  const std::array<domain::Reading, 2> rd{{{"lux", 300000, Unit::Lux, domain::Quality::Good},
                                           {"soil", 40000, Unit::Pct, domain::Quality::Fault}}};
  t.publish(rd, pub);
  const auto tel = link.on("telemetry");
  ASSERT_EQ(tel.size(), 1U);
  const std::string p = payload_of(tel[0].payload);
  EXPECT_NE(p.find(R"({"channel":"lux","value":300.0,"unit":"lux","quality":"good"})"), std::string::npos);
  EXPECT_NE(p.find(R"({"channel":"soil","value":40.0,"unit":"pct","quality":"fault"})"), std::string::npos);
  EXPECT_NE(p.find(R"({"id":"fan_main","level":0,"owner":"none"})"), std::string::npos);
  EXPECT_NE(p.find(R"({"id":"pump","level":0,"owner":"none"})"), std::string::npos);
  EXPECT_EQ(tel[0].qos, infra::Qos::AtMostOnce);
}

TEST_F(ServicesTest, TelemetryNotSentWithoutReadingsOrWhileDisconnected) {
  TelemetryService t(arbiter);
  t.publish({}, pub);
  EXPECT_TRUE(link.published.empty());
  link.set_connected(false);
  const std::array<domain::Reading, 1> rd{{{"lux", 300000, Unit::Lux, domain::Quality::Good}}};
  t.publish(rd, pub);
  EXPECT_TRUE(link.published.empty());
}

TEST_F(ServicesTest, TelemetryReflectsOwner) {
  TelemetryService t(arbiter);
  ControlArbiter::InterlockChanges ch{};
  (void)arbiter.set_command(0, 640, domain::Priority::Schedule, 60, clock.monotonic_ms(), ch);
  const std::array<domain::Reading, 1> rd{{{"lux", 300000, Unit::Lux, domain::Quality::Good}}};
  t.publish(rd, pub);
  EXPECT_EQ(test::actuator_in(link.on("telemetry").back().payload, "fan_main"),
            std::make_pair(640, std::string("schedule")));
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
  s.write_payload(w2, false, domain::Mode::PowerSave, 5);
  EXPECT_EQ(json_value(w2.view(), "status"), "offline");
  EXPECT_EQ(json_value(w2.view(), "mode"), "power_save");
  // 省電力モードは period_ms も5倍の値
  EXPECT_NE(w2.view().find(R"({"channel":"lux","unit":"lux","period_ms":5000})"), std::string::npos);
  EXPECT_NE(w2.view().find(R"({"channel":"soil","unit":"pct","period_ms":50000})"), std::string::npos);
}

// EventService は event_service_test.cpp

}  // namespace
}  // namespace node::app
