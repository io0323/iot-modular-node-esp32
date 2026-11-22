// ConnectionManager：接続の順序（ネットワーク → 同期 → MQTT）と再接続の間隔（P4 の決定 #64・#69）。
#include <gtest/gtest.h>

#include <vector>

#include "app/connection_manager.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

using MqttState = ConnectionManager::MqttState;

TEST(Backoff, DoublesFromOneSecondUpToSixty) {
  Backoff b;
  std::vector<std::uint32_t> d;
  for (int i = 0; i < 9; ++i) d.push_back(b.next());
  EXPECT_EQ(d, (std::vector<std::uint32_t>{1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000, 60000}));
  b.reset();
  EXPECT_EQ(b.next(), 1000U);
}

class ConnectionManagerTest : public ::testing::Test {
 protected:
  test::FakeNetwork network;
  test::FakeTimeSync sync;
  test::FakeMqttLink link;
  ConnectionManager cm{network, sync, link};
  std::uint64_t now{0};
  infra::ConnectOptions opts{};

  /// 1周進め、MQTT の接続を求められたら接続する
  ConnectionManager::Actions step() {
    const auto a = cm.tick(now);
    if (a.want_mqtt_connect) cm.connect_mqtt(opts, now);
    return a;
  }
  /// ms だけ 10ms 刻みで進める。MQTT の接続を試した時刻を返す
  std::vector<std::uint64_t> run(std::uint64_t ms) {
    std::vector<std::uint64_t> attempts;
    const std::uint64_t end = now + ms;
    for (; now < end; now += 10) {
      const auto before = link.connects.size();
      (void)step();
      if (link.connects.size() != before) attempts.push_back(now);
    }
    return attempts;
  }
};

TEST_F(ConnectionManagerTest, ConnectsNetworkThenWaitsForSyncThenMqtt) {
  sync.set_synced(false);
  (void)step();
  EXPECT_TRUE(cm.network_up());
  EXPECT_EQ(network.connects, 1);
  run(5000);
  EXPECT_TRUE(link.connects.empty());  // 同期前は MQTT に接続しない
  sync.set_synced(true);
  (void)step();
  ASSERT_EQ(link.connects.size(), 1U);
  EXPECT_EQ(cm.mqtt_state(), MqttState::Connecting);
  now += 10;
  const auto a = step();
  EXPECT_TRUE(a.connected);
  EXPECT_TRUE(cm.mqtt_connected());
}

TEST_F(ConnectionManagerTest, NoMqttWhileNetworkIsDown) {
  network.accept_connect = false;
  run(10000);
  EXPECT_TRUE(link.connects.empty());
  EXPECT_FALSE(cm.network_up());
  EXPECT_GE(network.connects, 2);
}

TEST_F(ConnectionManagerTest, MqttBackoffDoublesUpToSixtySeconds) {
  link.accept_connect = false;
  const auto attempts = run(300000);
  ASSERT_GE(attempts.size(), 9U);
  std::vector<std::uint64_t> gaps;
  for (std::size_t i = 1; i < 9; ++i) gaps.push_back(attempts[i] - attempts[i - 1]);
  // 失敗は次の周で分かるので、間隔は待ち時間 + 10ms（1周）
  EXPECT_EQ(gaps, (std::vector<std::uint64_t>{1010, 2010, 4010, 8010, 16010, 32010, 60010, 60010}));
}

TEST_F(ConnectionManagerTest, BackoffResetsAfterSuccessfulConnect) {
  link.accept_connect = false;
  run(8000);  // 1s・2s・4s と失敗
  EXPECT_GT(cm.next_mqtt_attempt_ms(), now);
  link.accept_connect = true;
  run(10000);
  ASSERT_TRUE(cm.mqtt_connected());
  link.drop();
  link.accept_connect = false;
  (void)step();  // 切れたのを知る：1秒後から
  EXPECT_EQ(cm.next_mqtt_attempt_ms(), now + 1000);
}

TEST_F(ConnectionManagerTest, LostIsReportedOnlyAfterConnected) {
  run(100);
  ASSERT_TRUE(cm.mqtt_connected());
  link.drop();
  const auto a = step();
  EXPECT_TRUE(a.lost);
  EXPECT_FALSE(cm.mqtt_connected());
  // 接続できないまま失敗が続いても lost は1回だけ
  link.accept_connect = false;
  for (int i = 0; i < 1000; ++i) {
    now += 10;
    EXPECT_FALSE(step().lost);
  }
}

TEST_F(ConnectionManagerTest, NetworkDownTakesMqttDownAndReconnectsInOrder) {
  run(100);
  ASSERT_TRUE(cm.mqtt_connected());
  network.go_down();
  now += 10;
  const auto a = step();
  EXPECT_TRUE(a.lost);
  EXPECT_FALSE(cm.network_up());
  EXPECT_EQ(cm.mqtt_state(), MqttState::Idle);
  EXPECT_EQ(link.disconnects, 1);  // ネットワークが落ちたら MQTT も切る
  const auto before = link.connects.size();
  run(5000);
  EXPECT_EQ(link.connects.size(), before);  // ネットワークが戻るまで MQTT は試さない
  network.go_up();
  run(5000);
  EXPECT_TRUE(cm.network_up());
  EXPECT_TRUE(cm.mqtt_connected());
}

TEST_F(ConnectionManagerTest, NetworkAndMqttBackoffsAreSeparate) {
  network.accept_connect = false;
  run(8000);  // ネットワークは 1s・2s・4s と失敗
  EXPECT_EQ(cm.mqtt_attempts(), 0U);
  network.go_up();
  run(20000);
  EXPECT_TRUE(cm.mqtt_connected());
  EXPECT_EQ(cm.mqtt_attempts(), 1U);  // MQTT の間隔はネットワークの失敗に引きずられない
}

TEST_F(ConnectionManagerTest, StaleConnectedEventAfterDropIsIgnored) {
  run(100);
  link.events.push_back(infra::LinkEvent::Connected);  // 古い通知
  now += 10;
  EXPECT_FALSE(step().connected);
}

}  // namespace
}  // namespace node::app
