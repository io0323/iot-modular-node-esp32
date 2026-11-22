#include "app/state_service.hpp"

namespace node::app {

void StateService::write_payload(infra::JsonWriter& w, bool online, domain::Mode mode) const {
  w.begin_object();
  w.key("status").string(online ? "online" : "offline");
  w.key("mode").string(domain::to_string(mode));
  w.key("channels").begin_array();
  for (const domain::ISensor* s : sensors_) {
    for (const domain::ChannelSpec& ch : s->channels()) {
      w.begin_object();
      w.key("channel").string(ch.channel);
      w.key("unit").string(domain::to_string(ch.unit));
      w.key("period_ms").uinteger(ch.period_ms);
      w.end_object();
    }
  }
  w.end_array();
  w.key("actuators").begin_array();
  for (std::size_t i = 0; i < arbiter_.size(); ++i) {
    const domain::ActuatorSpec& a = arbiter_.spec(i);
    w.begin_object();
    w.key("id").string(a.id);
    w.key("min").uinteger(a.min);
    w.key("max").uinteger(a.max);
    w.key("max_on_s");
    if (a.max_on_s) {
      w.uinteger(*a.max_on_s);
    } else {
      w.null();
    }
    w.end_object();
  }
  w.end_array();
  w.key("fw_version").string(fw_version_);
  w.end_object();
}

}  // namespace node::app
