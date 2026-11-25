// EventService の保持・送り直し・UPSTREAM_LOST／RESTORED（P4 の決定 #8・#65・#70・#71、open-questions #74）。
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "app/event_service.hpp"
#include "app/publisher.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

using test::json_value;

class EventServiceTest : public ::testing::Test {
 protected:
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeRandom rng;
  test::FakeMqttLink link;
  test::CapturingLog log;
  infra::EnvelopeFactory env{"mcu-cpp-01", clock, sync, rng};
  infra::Topics topics{"home", "mcu-cpp-01"};
  Publisher pub{link, env, topics, clock, log};
  EventService ev;

  /// 接続した（リンク・Publisher・EventService）
  void connect() {
    link.set_connected(true);
    pub.set_online(true);
    ev.on_connected(env.now());
  }
  /// 切れた
  void disconnect() {
    link.drop();
    pub.set_online(false);
    ev.on_disconnected(env.now());
  }
  /// PUBACK を EventService に渡す
  void deliver() {
    std::uint32_t t = 0;
    while (link.poll_delivery(t)) ev.on_delivered(t);
  }
  [[nodiscard]] std::vector<std::string> codes() const {
    std::vector<std::string> r;
    for (const auto& e : link.on("event")) r.push_back(json_value(e.payload, "code").value_or(""));
    return r;
  }
};

TEST_F(EventServiceTest, QueuesWhileDisconnectedAndStampsAtOccurrence) {
  ASSERT_TRUE(ev.post("BOOT", Severity::Info, R"({"reset_reason":"power_on"})", env.now()));
  clock.advance(500);
  ASSERT_TRUE(ev.post("MODE_CHANGED", Severity::Info, "", env.now()));
  ev.flush(pub);
  EXPECT_EQ(ev.pending(), 2U);
  EXPECT_TRUE(link.published.empty());

  clock.advance(5000);
  connect();
  ev.flush(pub);
  deliver();
  EXPECT_EQ(ev.pending(), 0U);
  EXPECT_EQ(ev.inflight(), 0U);
  EXPECT_TRUE(ev.boot_sent());
  const auto sent = link.on("event");
  ASSERT_EQ(sent.size(), 2U);
  EXPECT_EQ(json_value(sent[0].payload, "code"), "BOOT");
  EXPECT_EQ(json_value(sent[0].payload, "reset_reason"), "power_on");
  EXPECT_EQ(json_value(sent[0].payload, "ts"), "2026-11-20T06:00:13.345Z");  // 発生時刻
  EXPECT_EQ(json_value(sent[0].payload, "uptime_ms"), "0");
  EXPECT_EQ(json_value(sent[0].payload, "seq"), "0");  // seq は送るときに付ける
  EXPECT_EQ(json_value(sent[1].payload, "ts"), "2026-11-20T06:00:13.845Z");
  EXPECT_EQ(json_value(sent[1].payload, "uptime_ms"), "500");
  EXPECT_EQ(json_value(sent[1].payload, "seq"), "1");
  EXPECT_EQ(sent[1].payload.find("detail"), std::string::npos);
}

TEST_F(EventServiceTest, ChannelIsWrittenWhenGiven) {
  connect();
  ASSERT_TRUE(ev.post("SENSOR_FAULT", Severity::Critical, "", env.now(), "temp_air"));
  ev.flush(pub);
  EXPECT_EQ(test::payload_of(link.on("event").back().payload),
            R"({"code":"SENSOR_FAULT","severity":"critical","channel":"temp_air"})");
  EXPECT_EQ(to_string(Severity::Warning), "warning");
}

TEST_F(EventServiceTest, KeepsThirtyTwoDroppingOldestButNeverBoot) {
  ASSERT_TRUE(ev.post("BOOT", Severity::Info, "", env.now()));
  for (int i = 0; i < 40; ++i) {
    clock.advance(1);
    ASSERT_TRUE(ev.post("X", Severity::Info, R"({"i":)" + std::to_string(i) + "}", env.now()));
  }
  EXPECT_EQ(ev.pending(), EventService::kCapacity);
  EXPECT_EQ(ev.dropped(), 9U);  // BOOT + 31 件を残す
  EXPECT_EQ(ev.dropped_before_first_connect(), 9U);
  connect();
  ev.flush(pub);
  const auto sent = link.on("event");
  ASSERT_EQ(sent.size(), 32U);
  EXPECT_EQ(json_value(sent[0].payload, "code"), "BOOT");
  EXPECT_EQ(json_value(sent[1].payload, "i"), "9");  // 古いものから捨てた
  EXPECT_EQ(json_value(sent[31].payload, "i"), "39");
  // 最初の接続の前に捨てた分は RESTORED に載せない（一度も切れていないので RESTORED もない）
  for (const auto& c : codes()) EXPECT_NE(c, "UPSTREAM_RESTORED");
}

TEST_F(EventServiceTest, LostAndRestoredAroundRetainedEvents) {
  connect();
  ASSERT_TRUE(ev.post("BOOT", Severity::Info, "", env.now()));
  ev.flush(pub);
  deliver();
  clock.advance(1000);
  disconnect();  // 単調時計 2000ms で切れた
  clock.advance(3000);
  ASSERT_TRUE(ev.post("SENSOR_FAULT", Severity::Critical, "", env.now(), "soil"));
  clock.advance(4000);
  connect();  // 9000ms で戻った（down_ms = 7000）
  ev.flush(pub);
  const auto sent = link.on("event");
  ASSERT_EQ(codes(), (std::vector<std::string>{"BOOT", "UPSTREAM_LOST", "SENSOR_FAULT", "UPSTREAM_RESTORED"}));
  EXPECT_EQ(json_value(sent[1].payload, "severity"), "warning");
  EXPECT_EQ(json_value(sent[1].payload, "ts"), "2026-11-20T06:00:14.345Z");  // 切れた時刻
  EXPECT_EQ(json_value(sent[1].payload, "uptime_ms"), "1000");
  EXPECT_EQ(json_value(sent[2].payload, "ts"), "2026-11-20T06:00:17.345Z");  // 発生時刻
  EXPECT_EQ(json_value(sent[3].payload, "severity"), "info");
  EXPECT_EQ(test::payload_of(sent[3].payload),
            R"({"code":"UPSTREAM_RESTORED","severity":"info","detail":{"down_ms":7000,"dropped":0}})");
  // seq は送った順
  for (std::size_t i = 0; i < sent.size(); ++i) EXPECT_EQ(json_value(sent[i].payload, "seq"), std::to_string(i));
}

TEST_F(EventServiceTest, RestoredCountsDropsDuringOutageAndLostRestoredAreNotCounted) {
  connect();
  ASSERT_TRUE(ev.post("BOOT", Severity::Info, "", env.now()));
  ev.flush(pub);
  deliver();
  disconnect();
  for (int i = 0; i < 35; ++i) ASSERT_TRUE(ev.post("X", Severity::Info, "", env.now()));
  EXPECT_EQ(ev.pending(), 32U);
  connect();
  EXPECT_EQ(ev.pending(), 34U);  // LOST・RESTORED は32件に数えない
  ev.flush(pub);
  deliver();
  EXPECT_EQ(json_value(link.on("event").back().payload, "dropped"), "3");
  // 次に切れて戻ったときは 0 から数え直す
  disconnect();
  connect();
  ev.flush(pub);
  EXPECT_EQ(json_value(link.on("event").back().payload, "dropped"), "0");
}

TEST_F(EventServiceTest, UnackedEventsAreResentWithSameMsgIdSeqAndTs) {
  connect();
  link.auto_puback = false;
  ASSERT_TRUE(ev.post("BOOT", Severity::Info, "", env.now()));
  clock.advance(100);
  ASSERT_TRUE(ev.post("SENSOR_FAULT", Severity::Critical, "", env.now(), "soil"));
  ev.flush(pub);
  link.puback_all();
  deliver();  // BOOT と SENSOR_FAULT は届いた
  ASSERT_TRUE(ev.post("MODE_CHANGED", Severity::Info, "", env.now()));
  clock.advance(100);
  ASSERT_TRUE(ev.post("ACTUATOR_LIMIT", Severity::Warning, "", env.now()));
  ev.flush(pub);
  EXPECT_EQ(ev.inflight(), 2U);
  const auto first = link.on("event");
  ASSERT_EQ(first.size(), 4U);

  clock.advance(1000);
  disconnect();  // MODE_CHANGED・ACTUATOR_LIMIT の PUBACK は来なかった
  EXPECT_EQ(ev.inflight(), 0U);
  EXPECT_EQ(ev.pending(), 2U);
  ASSERT_TRUE(ev.post("NEW", Severity::Info, "", env.now()));
  clock.advance(2000);
  link.auto_puback = true;
  connect();
  ev.flush(pub);
  const auto all = link.on("event");
  ASSERT_EQ(codes(), (std::vector<std::string>{"BOOT", "SENSOR_FAULT", "MODE_CHANGED", "ACTUATOR_LIMIT",
                                                "UPSTREAM_LOST", "MODE_CHANGED", "ACTUATOR_LIMIT", "NEW",
                                                "UPSTREAM_RESTORED"}));
  // 送り直しは電文がまったく同じ（msg_id・seq・ts）
  EXPECT_EQ(all[5].payload, first[2].payload);
  EXPECT_EQ(all[6].payload, first[3].payload);
  // LOST は送り直しの後の seq から
  EXPECT_EQ(json_value(all[4].payload, "seq"), "4");
  EXPECT_EQ(json_value(all[7].payload, "seq"), "5");
  EXPECT_EQ(json_value(all[8].payload, "seq"), "6");
}

TEST_F(EventServiceTest, ResentEventIsByteIdenticalEvenAfterTimeOffsetChanges) {
  // P7（#116）：確認待ちの電文は写しを持たず、msg_id・seq・ts と中身の値から作り直す。
  // 送ってから送り直すまでのあいだに時刻の同期で差が変わっても、ts は送ったときの値のまま
  connect();
  link.auto_puback = false;
  ASSERT_TRUE(ev.post("SENSOR_FAULT", Severity::Critical, R"({"error":"timeout","tries":3})", env.now(), "soil"));
  clock.advance(10);
  ASSERT_TRUE(ev.post("MODE_CHANGED", Severity::Info, R"({"from":"auto","to":"manual","cause":"button"})", env.now()));
  ev.flush(pub);
  const auto first = link.on("event");
  ASSERT_EQ(first.size(), 2U);
  disconnect();
  sync.set_offset(test::FakeTimeSync::kSampleTs + 3'600'000);  // 1時間ずれを直した
  clock.advance(5000);
  link.auto_puback = true;
  connect();
  ev.flush(pub);
  const auto all = link.on("event");
  ASSERT_EQ(codes(), (std::vector<std::string>{"SENSOR_FAULT", "MODE_CHANGED", "UPSTREAM_LOST", "SENSOR_FAULT",
                                                "MODE_CHANGED", "UPSTREAM_RESTORED"}));
  EXPECT_EQ(all[3].payload, first[0].payload);  // バイト単位で同じ
  EXPECT_EQ(all[4].payload, first[1].payload);
}

TEST_F(EventServiceTest, LostIsAheadOfResentEvents) {
  // 送り直しの電文は LOST より前に起きているが、LOST を先頭に置く（受け手が切れ目を先に知る、#70）
  connect();
  link.auto_puback = false;
  ASSERT_TRUE(ev.post("X", Severity::Info, "", env.now()));
  ev.flush(pub);
  disconnect();
  link.auto_puback = true;
  connect();
  ev.flush(pub);
  EXPECT_EQ(codes(), (std::vector<std::string>{"X", "UPSTREAM_LOST", "X", "UPSTREAM_RESTORED"}));
}

TEST_F(EventServiceTest, DeferredOverRateKeepsEventsAndSeqContiguous) {
  connect();
  for (int i = 0; i < 60; ++i) ASSERT_TRUE(ev.post("X", Severity::Info, "", env.now()));
  // 32 件しか持てないので 28 件捨て、送れるのはバースト 50 のうち 32 件
  ev.flush(pub);
  EXPECT_EQ(link.on("event").size(), 32U);
  for (int i = 0; i < 60; ++i) ASSERT_TRUE(ev.post("Y", Severity::Info, "", env.now()));
  ev.flush(pub);
  EXPECT_EQ(link.on("event").size(), 50U);  // 上限に当たった
  EXPECT_GT(ev.pending(), 0U);
  EXPECT_GT(pub.counters().event_deferred, 0U);
  clock.advance(1000);
  ev.flush(pub);
  EXPECT_EQ(link.on("event").size(), 60U);  // 1秒で10件分
  const auto sent = link.on("event");
  for (std::size_t i = 0; i < sent.size(); ++i) EXPECT_EQ(json_value(sent[i].payload, "seq"), std::to_string(i));
}

TEST_F(EventServiceTest, InflightFullDefersWithoutLoss) {
  connect();
  link.auto_puback = false;
  for (int i = 0; i < 32; ++i) ASSERT_TRUE(ev.post("X", Severity::Info, "", env.now()));
  ev.flush(pub);
  EXPECT_EQ(ev.inflight(), 32U);
  ASSERT_TRUE(ev.post("Y", Severity::Info, "", env.now()));
  clock.advance(1000);
  ev.flush(pub);
  EXPECT_EQ(ev.pending(), 1U);  // 確認待ちが 32 件なので送らない
  link.puback_all();
  deliver();
  ev.flush(pub);
  EXPECT_EQ(ev.pending(), 0U);
  EXPECT_EQ(json_value(link.on("event").back().payload, "seq"), "32");
}

}  // namespace
}  // namespace node::app
