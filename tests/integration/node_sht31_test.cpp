// ホスト結合テスト（--devices fakehw の構成に SHT31 を足した後。P8a、NFR-01）：
// state の channels[] に2チャネルが加わり、telemetry に周期どおり出る。無応答で SENSOR_FAULT が2件、戻すと
// SENSOR_RECOVERED が2件。
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "app/node.hpp"
#include "config/fakehw.hpp"
#include "drivers/sensor_io.hpp"
#include "drivers/sht31.hpp"
#include "drivers/ui_io.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

using test::json_value;

class NodeSht31Test : public ::testing::Test {
 protected:
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeNetwork network;
  test::FakeRandom rng;
  test::FakeMqttLink link;
  test::CapturingLog log;
  drivers::fake::FakeTimer timer;
  drivers::fake::FakeMutex mutex;
  config::FakeHwRig rig{timer, mutex};
  domain::DeviceRegistry registry = rig.registry();
  drivers::SensorIo sensor_io{registry.sensors()};
  drivers::UiIo ui_io{registry.ui()};
  std::unique_ptr<Node> node;

  void SetUp() override {
    NodeConfig cfg{};
    node = std::make_unique<Node>(cfg, registry, sensor_io, ui_io, link, network, sync, clock, rng, log);
    ASSERT_TRUE(node->start());
    run_for(100);
  }
  void run_for(std::uint64_t ms) {
    for (std::uint64_t t = 0; t < ms; t += 10) {
      clock.advance(10);
      node->tick();
    }
  }
  template <typename Pred>
  bool run_until(Pred pred, std::uint64_t limit_ms) {
    for (std::uint64_t t = 0; t < limit_ms; t += 10) {
      clock.advance(10);
      node->tick();
      if (pred()) return true;
    }
    return false;
  }
  [[nodiscard]] std::vector<std::string> events_with(std::string_view code) const {
    std::vector<std::string> r;
    for (const auto& e : link.on("event")) {
      if (json_value(e.payload, "code") == code) r.push_back(e.payload);
    }
    return r;
  }
  /// telemetry のうち、channel のチャネルを含むものの数
  [[nodiscard]] std::size_t telemetry_with(std::string_view channel) const {
    const std::string pat = "\"channel\":\"" + std::string(channel) + "\"";
    std::size_t n = 0;
    for (const auto& t : link.on("telemetry")) {
      n += t.payload.find(pat) != std::string::npos ? 1U : 0U;
    }
    return n;
  }
};

TEST_F(NodeSht31Test, StateListsTheTwoNewChannels) {
  ASSERT_FALSE(link.on("state").empty());
  const std::string state = link.on("state").back().payload;
  EXPECT_NE(state.find(R"({"channel":"temp_aux","unit":"degC","period_ms":2000})"), std::string::npos) << state;
  EXPECT_NE(state.find(R"({"channel":"humidity_aux","unit":"pct_rh","period_ms":2000})"), std::string::npos) << state;
  // 既存のチャネルはそのまま（並びは構成表の順で、新しい2チャネルは最後）
  EXPECT_LT(state.find(R"("channel":"soil")"), state.find(R"("channel":"temp_aux")"));
}

TEST_F(NodeSht31Test, TelemetryCarriesTheChannelsEveryPeriod) {
  const std::size_t before = telemetry_with("temp_aux");
  run_for(10'000);
  // 周期 2000 ms：10 秒で 5 回
  EXPECT_EQ(telemetry_with("temp_aux") - before, 5U);
  EXPECT_EQ(telemetry_with("humidity_aux") - before, 5U);
  const std::string tel = link.on("telemetry").back().payload;
  const auto k = tel.find(R"("channel":"temp_aux")");
  ASSERT_NE(k, std::string::npos);
  EXPECT_EQ(json_value(tel, "value", k), "25.0");  // フェイクの既定 25 ℃
  EXPECT_EQ(json_value(tel, "quality", k), "good");
  const auto h = tel.find(R"("channel":"humidity_aux")");
  ASSERT_NE(h, std::string::npos);
  EXPECT_EQ(json_value(tel, "quality", h), "good");
  ASSERT_TRUE(node->readings().latest("humidity_aux").has_value());
  EXPECT_NEAR(node->readings().latest("humidity_aux")->value_milli, 50000, 5);
}

TEST_F(NodeSht31Test, UnresponsiveSht31FaultsTwoChannelsAndRecovers) {
  run_for(1000);
  rig.bus().set_nack(drivers::Sht31::kAddrA, true);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_FAULT").empty(); }, 2100));
  const auto faults = events_with("SENSOR_FAULT");
  ASSERT_EQ(faults.size(), 2U);
  EXPECT_EQ(json_value(faults[0], "channel"), "temp_aux");
  EXPECT_EQ(json_value(faults[1], "channel"), "humidity_aux");
  // ほかのセンサーは故障にならない
  EXPECT_EQ(json_value(link.on("telemetry").back().payload, "quality"), "good");
  // 30 秒ごとの再初期化で、応答が戻ると SENSOR_RECOVERED が2件
  run_for(5000);
  rig.bus().set_nack(drivers::Sht31::kAddrA, false);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_RECOVERED").empty(); }, 31'000));
  const auto recovered = events_with("SENSOR_RECOVERED");
  ASSERT_EQ(recovered.size(), 2U);
  EXPECT_EQ(json_value(recovered[0], "channel"), "temp_aux");
  EXPECT_EQ(json_value(recovered[1], "channel"), "humidity_aux");
  EXPECT_EQ(events_with("SENSOR_FAULT").size(), 2U);
}

TEST_F(NodeSht31Test, CrcErrorsAreRetriedThenBecomeAFault) {
  run_for(1000);
  rig.sht31_device().set_crc_error(true);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_FAULT").empty(); }, 2100));
  EXPECT_EQ(events_with("SENSOR_FAULT").size(), 2U);
  rig.sht31_device().set_crc_error(false);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_RECOVERED").empty(); }, 31'000));
  EXPECT_EQ(events_with("SENSOR_RECOVERED").size(), 2U);
}

}  // namespace
}  // namespace node::app
