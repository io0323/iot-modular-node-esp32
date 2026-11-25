#include "domain/pump_interlock.hpp"

namespace node::domain {

PumpInterlock::Output PumpInterlock::update(Level requested, std::uint64_t now_ms) {
  Output out{};
  if (active_) {
    if (requested == 0) {
      request_returned_to_zero_ = true;
    }
    const bool held_long_enough = now_ms - stopped_at_ms_ >= kRestartHoldMs;
    if (!(request_returned_to_zero_ && held_long_enough)) {
      out.level = 0;
      return out;
    }
    active_ = false;
    running_ = false;
    out.transition = Transition::Released;
  }

  if (requested == 0) {
    running_ = false;
    out.level = 0;
    return out;
  }
  if (!running_) {
    running_ = true;
    on_since_ms_ = now_ms;
  }
  if (now_ms - on_since_ms_ >= max_on_ms_) {
    // 解除と同じ tick でもう一度効くことはない（解除の直後は on_since_ms_ = now_ms）
    active_ = true;
    running_ = false;
    stopped_at_ms_ = now_ms;
    request_returned_to_zero_ = false;
    out.level = 0;
    out.transition = Transition::Activated;
    return out;
  }
  out.level = requested;
  return out;
}

InterlockVerdict PumpInterlock::evaluate(const InterlockInput& in) {
  const Output out = update(in.requested, in.now_ms);
  InterlockVerdict v{};
  if (active_ || out.level < in.requested) {
    v.upper = out.level;
  }
  return v;
}

}  // namespace node::domain
