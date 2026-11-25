// ホスト結合テスト（--devices fakehw の構成）：ドライバとフェイクの hal・機器、FakeMqttLink、仮想時計でアプリ全体を動かす。
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "app/node.hpp"
#include "config/fakehw.hpp"
#include "drivers/sensor_io.hpp"
#include "drivers/ui_io.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

using test::actuator_in;
using test::json_value;

class NodeFakeHwTest : public ::testing::Test {
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
  [[nodiscard]] std::vector<test::FakeMqttLink::Published> events_with(std::string_view code) const {
    std::vector<test::FakeMqttLink::Published> r;
    for (const auto& e : link.on("event")) {
      if (json_value(e.payload, "code") == code) r.push_back(e);
    }
    return r;
  }
  [[nodiscard]] std::string last_telemetry() const { return link.on("telemetry").back().payload; }
  /// ボタンを ms だけ押して離す（押すと Low）
  void press(hal::Pin pin, std::uint64_t ms) {
    rig.gpio().set_level(pin, false);
    run_for(ms);
    rig.gpio().set_level(pin, true);
    run_for(100);
  }
  [[nodiscard]] std::uint32_t pwm_duty(hal::Pin ch) const { return rig.pwm().channel(ch).duty; }
};

TEST_F(NodeFakeHwTest, ReadsSensorsThroughDriversAndShowsOled) {
  run_for(2000);
  const std::string tel = last_telemetry();
  EXPECT_NE(tel.find(R"("channel":"lux","value":300.0,"unit":"lux","quality":"good")"), std::string::npos);
  EXPECT_EQ(json_value(tel, "quality"), "good");
  ASSERT_TRUE(node->readings().latest("temp_air").has_value());
  EXPECT_NEAR(node->readings().latest("temp_air")->value_milli, 25000, 10);
  EXPECT_EQ(node->readings().latest("soil")->value_milli, 40000);
  // OLED（SSD1306 のフェイクの GDDRAM を読み戻す）
  EXPECT_TRUE(rig.ssd1306_device().display_on());
  EXPECT_EQ(rig.ssd1306_device().row_text(0).view(), "AUTO   MQTT:ON");
  EXPECT_EQ(rig.ssd1306_device().row_text(3).view(), "L      300lx");
  EXPECT_EQ(rig.ssd1306_device().row_text(6).view(), "F0 L0 P0 B0");
}

TEST_F(NodeFakeHwTest, HighTemperatureThroughBme280ForcesFan) {
  rig.bme280_device().set_temperature_c(46.0);
  ASSERT_TRUE(run_until([&] { return pwm_duty(config::kPwmFan) == 1023; }, 2100));  // level 1000 = デューティ 1023
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "fan_main"), std::make_pair(1000, std::string("interlock")));
  const auto active = events_with("INTERLOCK_ACTIVE");
  ASSERT_EQ(active.size(), 1U);
  EXPECT_EQ(json_value(active[0].payload, "rule"), "temp_high");
  rig.bme280_device().set_temperature_c(42.0);
  ASSERT_TRUE(run_until([&] { return !events_with("INTERLOCK_RELEASED").empty(); }, 13'000));
  EXPECT_EQ(pwm_duty(config::kPwmFan), 614U);  // cool-on-high-temp の 600 = round(600 × 1023 / 1000)
}

TEST_F(NodeFakeHwTest, UnresponsiveBme280FaultsRecoversBusAndRecovers) {
  run_for(1000);
  rig.bus().set_timeout(config::kAddrBme280, true);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_FAULT").empty(); }, 2100));
  const auto faults = events_with("SENSOR_FAULT");
  ASSERT_EQ(faults.size(), 3U);  // temp_air・humidity・pressure（#6）
  EXPECT_EQ(rig.bus().recover_count(), 1U);  // 3回続けて失敗したら1回だけ（#48・#55）
  // telemetry は出続け、3チャネルが fault（#52）
  const std::string tel = last_telemetry();
  for (std::string_view ch : {"temp_air", "humidity", "pressure"}) {
    const std::string pat = "\"channel\":\"" + std::string(ch) + "\"";
    const auto k = tel.find(pat);
    ASSERT_NE(k, std::string::npos) << ch;
    EXPECT_EQ(json_value(tel, "quality", k), "fault") << ch;
  }
  run_for(1000);  // OLED は1秒ごとに書き換える
  EXPECT_EQ(rig.ssd1306_device().row_text(1).view(), "T       --C");  // OLED も fault を -- で
  // 30秒後の再初期化までに応答を戻すと、SENSOR_RECOVERED が3件
  run_for(20'000);
  EXPECT_TRUE(events_with("SENSOR_RECOVERED").empty());
  rig.bus().set_timeout(config::kAddrBme280, false);
  ASSERT_TRUE(run_until([&] { return !events_with("SENSOR_RECOVERED").empty(); }, 11'000));
  EXPECT_EQ(events_with("SENSOR_RECOVERED").size(), 3U);
  EXPECT_EQ(rig.bus().recover_count(), 1U);  // 再初期化では回復を要求しない
}

TEST_F(NodeFakeHwTest, ActionLongPressRunsPumpThenInterlockStopsAt30s) {
  press(config::kGpioButtonMode, 200);  // MODE 短押し → 手動
  ASSERT_EQ(node->mode(), domain::Mode::Manual);
  press(config::kGpioButtonAction, 200);  // ACTION 短押し → led_grow
  press(config::kGpioButtonAction, 200);  // → pump
  EXPECT_EQ(node->manual().selected_id(), "pump");
  run_for(1000);
  EXPECT_EQ(rig.ssd1306_device().row_text(7).view(), "> pump");
  rig.gpio().set_level(config::kGpioButtonAction, false);  // ACTION 長押し → pump を 1000 に
  ASSERT_TRUE(run_until([&] { return rig.gpio().level(config::kGpioPumpRelay); }, 1200));
  rig.gpio().set_level(config::kGpioButtonAction, true);
  const std::uint64_t started = clock.monotonic_ms();
  run_for(1000);
  EXPECT_EQ(actuator_in(last_telemetry(), "pump"), std::make_pair(1000, std::string("local")));
  ASSERT_TRUE(run_until([&] { return !rig.gpio().level(config::kGpioPumpRelay); }, 30'000));
  EXPECT_NEAR(static_cast<double>(clock.monotonic_ms() - started), 30'000.0, 20.0);  // 連続30秒で止まる
  const auto active = events_with("INTERLOCK_ACTIVE");
  ASSERT_EQ(active.size(), 1U);
  EXPECT_EQ(json_value(active[0].payload, "rule"), "max_on_s");
}

TEST_F(NodeFakeHwTest, PowerSaveTurnsOledOff) {
  press(config::kGpioButtonMode, 200);
  press(config::kGpioButtonMode, 200);
  ASSERT_EQ(node->mode(), domain::Mode::PowerSave);
  EXPECT_FALSE(rig.ssd1306_device().display_on());
  EXPECT_EQ(rig.ssd1306_device().commands().back(), 0xAE);
}

}  // namespace
}  // namespace node::app
