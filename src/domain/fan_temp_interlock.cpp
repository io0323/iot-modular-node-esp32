#include "domain/fan_temp_interlock.hpp"

namespace node::domain {

InterlockVerdict FanTempInterlock::evaluate(const InterlockInput& in) {
  const std::optional<std::int32_t> value = in.store.good_value(config_.channel, in.now_ms);
  if (!value) {
    below_since_ms_.reset();  // 疑わしい値の間を「43℃未満が続いた」とは数えない（#38）
  } else if (!engaged_) {
    if (*value >= config_.on_milli) {
      engaged_ = true;
      below_since_ms_.reset();
    }
  } else if (*value < config_.off_milli) {
    if (!below_since_ms_) {
      below_since_ms_ = in.now_ms;
    }
    if (in.now_ms - *below_since_ms_ >= config_.hold_ms) {
      engaged_ = false;
      below_since_ms_.reset();
    }
  } else {
    below_since_ms_.reset();
  }

  InterlockVerdict v{};
  if (engaged_) {
    v.force = config_.force;
  }
  return v;
}

}  // namespace node::domain
