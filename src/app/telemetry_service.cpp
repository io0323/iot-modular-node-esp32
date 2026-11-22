#include "app/telemetry_service.hpp"

namespace node::app {

TelemetryService::TelemetryService(std::span<domain::ISensor* const> sensors, const ControlArbiter& arbiter,
                                   domain::ReadingStore& store)
    : sensors_(sensors.first(sensors.size() < kMaxSensors ? sensors.size() : kMaxSensors)),
      arbiter_(arbiter), store_(store) {}

void TelemetryService::start(std::uint64_t now_ms) {
  for (std::size_t i = 0; i < sensors_.size(); ++i) next_due_ms_[i] = now_ms;
}

void TelemetryService::tick(std::uint64_t now_ms, Publisher& publisher) {
  support::StaticVector<domain::Reading, kMaxReadings> readings{};
  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    if (now_ms < next_due_ms_[i]) continue;
    domain::ISensor& s = *sensors_[i];
    const auto period = static_cast<std::uint64_t>(s.period().count());
    // 遅れが周期を超えたら、取り戻さずに次の周期から数え直す
    next_due_ms_[i] = (now_ms - next_due_ms_[i] >= period) ? now_ms + period : next_due_ms_[i] + period;

    std::array<domain::Reading, kMaxReadings> buf{};
    const auto r = s.read(buf);
    if (!r) {
      ++read_errors_;
      continue;
    }
    for (std::size_t k = 0; k < r.value() && k < buf.size(); ++k) {
      (void)store_.update(buf[k]);
      (void)readings.push_back(buf[k]);
    }
  }
  if (readings.empty()) return;

  (void)publisher.publish(infra::Kind::Telemetry, [&](infra::JsonWriter& w) {
    w.begin_object();
    w.key("readings").begin_array();
    for (const domain::Reading& rd : readings) {
      w.begin_object();
      w.key("channel").string(rd.channel);
      w.key("value").fixed_milli(rd.value_milli);
      w.key("unit").string(domain::to_string(rd.unit));
      w.key("quality").string(domain::to_string(rd.quality));
      w.end_object();
    }
    w.end_array();
    w.key("actuators").begin_array();
    for (std::size_t i = 0; i < arbiter_.size(); ++i) {
      w.begin_object();
      w.key("id").string(arbiter_.spec(i).id);
      w.key("level").uinteger(arbiter_.output(i));
      w.key("owner").string(domain::to_string(arbiter_.owner(i)));
      w.end_object();
    }
    w.end_array();
    w.end_object();
  });
}

}  // namespace node::app
