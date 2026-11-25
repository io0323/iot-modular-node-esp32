#include "app/telemetry_service.hpp"

namespace node::app {

void TelemetryService::publish(std::span<const domain::Reading> readings, Publisher& publisher) const {
  if (readings.empty()) {
    return;
  }
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
