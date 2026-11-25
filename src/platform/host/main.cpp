// ホスト版の main：mcu_cpp_01 [--broker host:port] [--site <site>] [--log <file.jsonl>] [--devices sim|fakehw]
//                    [デバッグ入力…]
//
// --devices：sim（既定。P2 の模擬機器）か fakehw（ドライバとフェイクの hal・機器。P3 の決定 #51）
//
// デバッグ入力（P2。省略時は P1 と同じ動き）：--sim <channel>=<value>[@<秒>]、--sim-fault <センサーID>@<開始秒>[-<終了秒>]、
// --press-at <秒>、--manual <id>=<level>@<秒>（debug_inputs.hpp）
// 接続のデバッグ入力（P4）：--sntp-delay <秒>（起動から指定秒は同期前）、--net-down <開始秒>-<終了秒>（複数指定可）
//
// ブローカーは --broker、なければ IOTSPEC_MQTT_URL（既定 mqtt://localhost:1883）。
// パスワードは IOTSPEC_MQTT_PASSWORD（既定 iotspec-dev）。BOOT の reset_reason は MCU_CPP_01_RESET_REASON（既定 power_on）。
#include <atomic>
#include <cmath>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "app/node.hpp"
#include "config/devices.hpp"
#include "config/fakehw.hpp"
#include "platform/host/debug_inputs.hpp"
#include "platform/host/host_clock.hpp"
#include "platform/host/host_hal.hpp"
#include "platform/host/host_random.hpp"
#include "platform/host/jsonl_log_sink.hpp"
#include "platform/host/paho_mqtt_link.hpp"

namespace {

using namespace std::chrono_literals;

// シグナルハンドラーから書くので、名前空間の変数で volatile std::sig_atomic_t にする（docs/quality.md の抑止の表）
volatile std::sig_atomic_t g_stop = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void on_signal(int /*sig*/) { g_stop = 1; }

/// 標準エラーに書く（printf 系の可変長引数を使わない）
void print_err(std::string_view s) { (void)std::fwrite(s.data(), 1, s.size(), stderr); }

struct Args {
  std::string broker_uri;
  std::string site{"home"};
  std::string log_path;
  bool fakehw{false};
  std::uint32_t sntp_delay_ms{0};
  std::vector<node::platform::HostNetwork::Window> net_down;
  node::platform::DebugInputs debug;
  bool ok{true};
};

std::string env_or(const char* name, const char* fallback) {
  const char* v = std::getenv(name);
  return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

// "mqtt://host:port" または "host:port" を Paho の "tcp://host:port" にする
std::string to_paho_uri(std::string_view s) {
  for (std::string_view const scheme : {"mqtt://", "tcp://"}) {
    if (s.substr(0, scheme.size()) == scheme) {
      s.remove_prefix(scheme.size());
      break;
    }
  }
  return "tcp://" + std::string(s);
}

void parse_sntp_delay(std::string_view value, Args& a) {
  const double v = std::strtod(std::string(value).c_str(), nullptr);
  if (v < 0) {
    print_err("--sntp-delay <秒>\n");
    a.ok = false;
  }
  a.sntp_delay_ms = static_cast<std::uint32_t>(std::llround(v * 1000.0));
}

void parse_net_down(std::string_view v, Args& a) {
  const std::size_t dash = v.find('-');
  const double b = std::strtod(std::string(v.substr(0, dash)).c_str(), nullptr);
  const double e = dash == std::string_view::npos ? -1 : std::strtod(std::string(v.substr(dash + 1)).c_str(), nullptr);
  if (dash == std::string_view::npos || b < 0 || e <= b) {
    print_err("--net-down <開始秒>-<終了秒>\n");
    a.ok = false;
    return;
  }
  a.net_down.push_back(
      {static_cast<std::uint64_t>(std::llround(b * 1000.0)), static_cast<std::uint64_t>(std::llround(e * 1000.0))});
}

void parse_devices(std::string_view v, Args& a) {
  if (v == "fakehw") {
    a.fakehw = true;
  } else if (v != "sim") {
    print_err("--devices は sim か fakehw\n");
    a.ok = false;
  }
}

/// 接続・構成の引数（値を1つとる）。このどれかなら true
bool parse_node_option(std::string_view arg, std::string_view value, Args& a) {
  if (arg == "--broker") {
    a.broker_uri = to_paho_uri(value);
  } else if (arg == "--site") {
    a.site = value;
  } else if (arg == "--log") {
    a.log_path = value;
  } else if (arg == "--sntp-delay") {
    parse_sntp_delay(value, a);
  } else if (arg == "--net-down") {
    parse_net_down(value, a);
  } else if (arg == "--devices") {
    parse_devices(value, a);
  } else {
    return false;
  }
  return true;
}

void print_usage() {
  print_err(
      "usage: mcu_cpp_01 [--broker host:port] [--site <site>] [--log <file.jsonl>] [--devices sim|fakehw]\n"
      "  [--sim <channel>=<value>[@<sec>]] [--sim-fault <sensor>@<start>[-<end>]]\n"
      "  [--press-at <sec>] [--manual <id>=<level>@<sec>]\n"
      "  [--sntp-delay <sec>] [--net-down <start>-<end>]\n");
}

Args parse_args(int argc, char** argv) {
  Args a;
  a.broker_uri = to_paho_uri(env_or("IOTSPEC_MQTT_URL", "mqtt://localhost:1883"));
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (has_value && parse_node_option(arg, argv[i + 1], a)) {
      ++i;
      continue;
    }
    if (std::string error; has_value && a.debug.parse(arg, argv[i + 1], error)) {
      ++i;
      if (!error.empty()) {
        print_err(error + "\n");
        a.ok = false;
      }
      continue;
    }
    print_usage();
    a.ok = false;
  }
  return a;
}

/// 模擬機器（P2）への入れ先
class SimTarget final : public node::platform::IDebugTarget {
 public:
  bool set_value(std::string_view channel, double value) override {
    node::drivers::SimSensor* s = node::config::sim_sensor_by_channel(channel);
    return s != nullptr && s->set_channel_value(channel, static_cast<std::int32_t>(std::llround(value * 1000.0)));
  }
  bool set_fault(std::string_view sensor_id, bool fault) override {
    node::drivers::SimSensor* s = node::config::sim_sensor_by_id(sensor_id);
    if (s != nullptr) {
      s->set_fault(fault);
    }
    return s != nullptr;
  }
};

/// ドライバとフェイクの組み合わせ（fakehw）への入れ先。値はフェイクの機器のレジスタ・生値に変えて入れる
class FakeHwTarget final : public node::platform::IDebugTarget {
 public:
  explicit FakeHwTarget(node::config::FakeHwRig& rig) : rig_(rig) {}
  bool set_value(std::string_view channel, double value) override {
    if (channel == "temp_air") {
      rig_.bme280_device().set_temperature_c(value);
    } else if (channel == "lux") {
      rig_.bh1750_device().set_lux(value);
    } else if (channel == "soil") {
      // % を ADC の生値に戻す（乾 3000・湿 1300 の直線。#43）
      const auto& c = node::config::kSoilCalibration;
      const double raw = c.dry_raw - value * (c.dry_raw - c.wet_raw) / 100.0;
      rig_.adc().set_raw(node::config::kAdcSoil, static_cast<std::uint16_t>(std::lround(raw < 0 ? 0 : raw)));
    } else {
      return false;
    }
    return true;
  }
  bool set_fault(std::string_view sensor_id, bool fault) override {
    if (sensor_id == "bme280") {
      rig_.bus().set_timeout(node::config::kAddrBme280, fault);
    } else if (sensor_id == "bh1750") {
      rig_.bus().set_timeout(node::config::kAddrBh1750, fault);
    } else if (sensor_id == "soil_moisture") {
      rig_.adc().set_fault(fault);
    } else {
      return false;
    }
    return true;
  }

 private:
  node::config::FakeHwRig& rig_;
};

void run_until_signal(node::app::Node& app, node::platform::PahoMqttLink& link, node::platform::DebugInputs& debug,
                      node::platform::IDebugTarget& target, node::infra::ILogSink& log);
void shutdown_gracefully(node::app::Node& app, node::platform::PahoMqttLink& link);

}  // namespace

int main(int argc, char** argv) {
  Args args = parse_args(argc, argv);
  if (!args.ok) {
    return 2;
  }

  (void)std::signal(SIGTERM, on_signal);
  (void)std::signal(SIGINT, on_signal);

  static node::platform::HostClock const clock;
  static node::platform::HostRandom rng;
  static node::infra::NullLogSink null_log;
  std::unique_ptr<node::platform::JsonlLogSink> file_log;
  node::infra::ILogSink* log = &null_log;
  if (!args.log_path.empty()) {
    file_log = std::make_unique<node::platform::JsonlLogSink>(args.log_path);
    if (!file_log->ok()) {
      print_err("cannot open log file: " + args.log_path + "\n");
      return 2;
    }
    log = file_log.get();
  }

  const std::string password = env_or("IOTSPEC_MQTT_PASSWORD", "iotspec-dev");
  const std::string reset_reason = env_or("MCU_CPP_01_RESET_REASON", "power_on");

  node::app::NodeConfig config{};
  config.site = args.site;
  config.password = password;
  config.reset_reason = reset_reason;

  node::platform::PahoMqttLink link(args.broker_uri, std::string(config.device_id));
  if (!link.ok()) {
    print_err("cannot create MQTT client for " + args.broker_uri + "\n");
    return 1;
  }

  // 機器の構成（既定は P2 の模擬機器、--devices fakehw でドライバとフェイク）
  static node::platform::HostTimer timer;
  static node::platform::HostMutex i2c_mutex;
  static SimTarget sim_target;
  std::unique_ptr<node::config::FakeHwRig> rig;
  std::unique_ptr<FakeHwTarget> fakehw_target;
  node::platform::IDebugTarget* target = &sim_target;
  node::domain::DeviceRegistry registry = node::config::devices();
  if (args.fakehw) {
    rig = std::make_unique<node::config::FakeHwRig>(timer, i2c_mutex);
    fakehw_target = std::make_unique<FakeHwTarget>(*rig);
    target = fakehw_target.get();
    registry = rig->registry();
  }

  // Node は大きい（固定容量のバッファを持つ）のでスタックに置かない
  static node::platform::HostTimeSync const time_sync(args.sntp_delay_ms);
  static node::platform::HostNetwork network(args.net_down);
  static node::app::Node app(config, registry, link, network, time_sync, clock, rng, *log);
  if (auto r = app.start(); !r) {
    print_err("start failed: " + std::string(r.error().what) + "\n");
    return 1;
  }

  run_until_signal(app, link, args.debug, *target, *log);
  shutdown_gracefully(app, link);
  return 0;
}

namespace {

void run_until_signal(node::app::Node& app, node::platform::PahoMqttLink& link, node::platform::DebugInputs& debug,
                      node::platform::IDebugTarget& target, node::infra::ILogSink& log) {
  const auto started = std::chrono::steady_clock::now();
  auto next_tick = started;
  while (g_stop == 0) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    debug.apply_due(static_cast<std::uint64_t>(elapsed.count()), app, target, log);
    app.tick();
    next_tick += 10ms;
    const auto now = std::chrono::steady_clock::now();
    if (next_tick < now) {
      next_tick = now;
    }
    link.wait_for_activity(std::chrono::duration_cast<std::chrono::milliseconds>(next_tick - now));
  }
}

// 正常終了：offline の state を送り、確認を待ってから切断する（切断では LWT は出ない）
void shutdown_gracefully(node::app::Node& app, node::platform::PahoMqttLink& link) {
  app.shutdown();
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (link.connected() && link.inflight() > 0 && std::chrono::steady_clock::now() < deadline) {
    link.wait_for_activity(10ms);
  }
  link.disconnect(1000);
}

}  // namespace
