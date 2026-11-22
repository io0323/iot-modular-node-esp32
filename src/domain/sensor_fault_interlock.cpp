#include "domain/sensor_fault_interlock.hpp"

namespace node::domain {

InterlockVerdict SensorFaultInterlock::evaluate(const InterlockInput& in) {
  engaged_ = in.store.quality(config_.channel, in.now_ms) != Quality::Good;
  InterlockVerdict v{};
  if (engaged_) {
    if (config_.action == Action::Force) {
      v.force = config_.level;
    } else {
      v.upper = config_.level;
    }
  }
  return v;
}

bool SensorFaultInterlock::rejects_commands(const ReadingStore& store, std::uint64_t now_ms) const {
  return config_.rejects_commands && store.quality(config_.channel, now_ms) != Quality::Good;
}

}  // namespace node::domain
