#include "platform/host/debug_inputs.hpp"

#include <cmath>
#include <cstdlib>
#include <optional>

#include "config/devices.hpp"

namespace node::platform {
namespace {

std::optional<double> number(std::string_view s) {
  if (s.empty()) return std::nullopt;
  const std::string str(s);
  char* end = nullptr;
  const double v = std::strtod(str.c_str(), &end);
  if (end != str.c_str() + str.size() || !std::isfinite(v)) return std::nullopt;
  return v;
}

std::optional<std::uint64_t> seconds_to_ms(std::string_view s) {
  const auto v = number(s);
  if (!v || *v < 0) return std::nullopt;
  return static_cast<std::uint64_t>(std::llround(*v * 1000.0));
}

// "<left>@<秒>" を分ける。@ がなければ時刻は 0
bool split_at(std::string_view s, std::string_view& left, std::uint64_t& at_ms, bool at_required) {
  const std::size_t at = s.rfind('@');
  if (at == std::string_view::npos) {
    if (at_required) return false;
    left = s;
    at_ms = 0;
    return true;
  }
  left = s.substr(0, at);
  const auto ms = seconds_to_ms(s.substr(at + 1));
  if (!ms) return false;
  at_ms = *ms;
  return true;
}

void log_debug(infra::ILogSink& log, std::string_view what, std::string_view target, long long value,
               std::string_view result) {
  std::string f = "{\"debug\":\"" + std::string(what) + "\",\"target\":\"" + std::string(target) +
                  "\",\"value\":" + std::to_string(value) + ",\"result\":\"" + std::string(result) + "\"}";
  log.write("input", f);
}

}  // namespace

bool DebugInputs::parse(std::string_view option, std::string_view value, std::string& error) {
  if (option == "--sim") {
    std::string_view left;
    std::uint64_t at = 0;
    const std::size_t eq = value.find('=');
    if (eq == std::string_view::npos || !split_at(value.substr(eq + 1), left, at, false)) {
      error = "--sim <channel>=<value>[@<秒>]";
      return true;
    }
    const std::string channel(value.substr(0, eq));
    const auto v = number(left);
    if (!v || config::sim_sensor_by_channel(channel) == nullptr) {
      error = "--sim: 値かチャネルが違う: " + std::string(value);
      return true;
    }
    actions_.push_back({at, Type::SetValue, channel, static_cast<std::int32_t>(std::llround(*v * 1000.0)), false});
    return true;
  }
  if (option == "--sim-fault") {
    std::string_view range;
    std::uint64_t unused = 0;
    const std::size_t at = value.rfind('@');
    if (at == std::string_view::npos) {
      error = "--sim-fault <センサーID>@<開始秒>[-<終了秒>]";
      return true;
    }
    const std::string id(value.substr(0, at));
    range = value.substr(at + 1);
    const std::size_t dash = range.find('-');
    const auto start = seconds_to_ms(range.substr(0, dash));
    const auto end = dash == std::string_view::npos ? std::optional<std::uint64_t>(unused) : seconds_to_ms(range.substr(dash + 1));
    if (!start || !end || config::sim_sensor_by_id(id) == nullptr || (dash != std::string_view::npos && *end <= *start)) {
      error = "--sim-fault: センサーIDか時刻が違う: " + std::string(value);
      return true;
    }
    actions_.push_back({*start, Type::FaultOn, id, 0, false});
    if (dash != std::string_view::npos) actions_.push_back({*end, Type::FaultOff, id, 0, false});
    return true;
  }
  if (option == "--press-at") {
    const auto at = seconds_to_ms(value);
    if (!at) {
      error = "--press-at <秒>";
      return true;
    }
    actions_.push_back({*at, Type::Press, "", 0, false});
    return true;
  }
  if (option == "--manual") {
    std::string_view left;
    std::uint64_t at = 0;
    const std::size_t eq = value.find('=');
    if (eq == std::string_view::npos || !split_at(value.substr(eq + 1), left, at, true)) {
      error = "--manual <id>=<level>@<秒>";
      return true;
    }
    const auto v = number(left);
    if (!v || *v < 0 || *v > domain::kLevelMax) {
      error = "--manual: level は 0〜1000: " + std::string(value);
      return true;
    }
    actions_.push_back({at, Type::Manual, std::string(value.substr(0, eq)), static_cast<std::int32_t>(*v), false});
    return true;
  }
  return false;
}

void DebugInputs::apply_due(std::uint64_t elapsed_ms, app::Node& node, infra::ILogSink& log) {
  for (Action& a : actions_) {
    if (a.done || elapsed_ms < a.at_ms) continue;
    a.done = true;
    switch (a.type) {
      case Type::SetValue: {
        drivers::SimSensor* s = config::sim_sensor_by_channel(a.target);
        const bool ok = s != nullptr && s->set_channel_value(a.target, a.value);
        log_debug(log, "sim", a.target, a.value, ok ? "applied" : "unknown_channel");
        break;
      }
      case Type::FaultOn:
      case Type::FaultOff: {
        drivers::SimSensor* s = config::sim_sensor_by_id(a.target);
        if (s != nullptr) s->set_fault(a.type == Type::FaultOn);
        log_debug(log, a.type == Type::FaultOn ? "sim_fault_on" : "sim_fault_off", a.target, 0,
                  s != nullptr ? "applied" : "unknown_sensor");
        break;
      }
      case Type::Press:
        node.press_button();
        break;
      case Type::Manual:
        (void)node.set_manual(a.target, static_cast<domain::Level>(a.value));  // 結果は Node がログに残す
        break;
    }
  }
}

}  // namespace node::platform
