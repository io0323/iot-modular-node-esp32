// 共通仕様（iot-common-spec）の samples/ の cmd を、CmdParser と CommandService が
// valid は受け付け、invalid は必ず拒否または破棄することを確かめる。
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app/command_service.hpp"
#include "drivers/sim/sim_sensor.hpp"
#include "support/fakes.hpp"

namespace node::app {
namespace {

namespace fs = std::filesystem;
using test::json_value;

constexpr domain::ActuatorSpec kFan{"fan_main", 0, 1000, {}};
constexpr domain::ActuatorSpec kLed{"led_grow", 0, 1000, {}};
constexpr domain::ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{30}};
constexpr domain::ActuatorSpec kBuzzer{"buzzer", 0, 1000, {}};

std::vector<fs::path> samples(const char* dir) {
  std::vector<fs::path> out;
  const fs::path root = fs::path(NODE_IOTSPEC_DIR) / "samples" / dir;
  for (const auto& e : fs::directory_iterator(root)) {
    const std::string name = e.path().filename().string();
    if (name.rfind("cmd__", 0) == 0 && e.path().extension() == ".json") out.push_back(e.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct Device {
  drivers::SimActuator fan{kFan}, led{kLed}, pump{kPump}, buzzer{kBuzzer};
  std::array<domain::ActuatorBinding, 4> list{{{&fan, {}}, {&led, {}}, {&pump, {}}, {&buzzer, {}}}};
  domain::ReadingStore store{};
  ControlArbiter arbiter{list, store};
  ModeManager modes{};
  CommandService svc{arbiter, modes};
  // サンプルの ts（2026-11-20T06:00:12.345Z）の時点で受けたことにする
  std::int64_t now_utc{*infra::parse_ts("2026-11-20T06:00:12.345Z")};
};

TEST(SpecSamples, ValidCmdsAreAccepted) {
  const auto files = samples("valid");
  ASSERT_GT(files.size(), 0U) << "samples/valid/cmd__*.json が見つからない";
  for (const auto& f : files) {
    Device d;
    const std::string json = read_file(f);
    ASSERT_TRUE(infra::parse_cmd(json).ok) << f.filename();
    const CommandOutcome o = d.svc.handle(json, 1000, d.now_utc);
    ASSERT_EQ(o.kind, CommandOutcome::Kind::Ack) << f.filename();
    EXPECT_EQ(json_value(o.ack_payload.view(), "status"), "done") << f.filename() << ": " << o.ack_payload.view();
  }
  std::cout << "[samples] valid cmd: " << files.size() << " 件\n";
}

TEST(SpecSamples, InvalidCmdsAreRejectedOrDiscarded) {
  const auto files = samples("invalid");
  ASSERT_GT(files.size(), 0U) << "samples/invalid/cmd__*.json が見つからない";
  for (const auto& f : files) {
    Device d;
    const CommandOutcome o = d.svc.handle(read_file(f), 1000, d.now_utc);
    if (o.kind == CommandOutcome::Kind::Discarded) continue;
    EXPECT_EQ(json_value(o.ack_payload.view(), "status"), "rejected") << f.filename() << ": " << o.ack_payload.view();
    EXPECT_EQ(d.fan.current() + d.led.current() + d.pump.current() + d.buzzer.current(), 0) << f.filename();
  }
  std::cout << "[samples] invalid cmd: " << files.size() << " 件\n";
}

TEST(SpecSamples, InvalidEnvelopesAreDiscarded) {
  // samples/invalid/envelope__*.json は telemetry の電文だが、エンベロープの誤りは cmd でも同じに扱う。
  // schema を iot.v1.cmd に書き換えたうえで、payload 以外の誤りは破棄されることを確かめる
  const fs::path root = fs::path(NODE_IOTSPEC_DIR) / "samples" / "invalid";
  int n = 0;
  for (const auto& e : fs::directory_iterator(root)) {
    const std::string name = e.path().filename().string();
    if (name.rfind("envelope__", 0) != 0) continue;
    std::string json = read_file(e.path());
    const std::size_t k = json.find("iot.v1.telemetry");
    if (k != std::string::npos) json.replace(k, 16, "iot.v1.cmd");
    const auto p = infra::parse_cmd(json);
    EXPECT_FALSE(p.ok) << name;
    EXPECT_EQ(p.discard, infra::DiscardReason::BadEnvelope) << name;
    ++n;
  }
  EXPECT_GT(n, 0);
}

}  // namespace
}  // namespace node::app
