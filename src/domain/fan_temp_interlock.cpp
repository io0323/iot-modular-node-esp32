#include "domain/fan_temp_interlock.hpp"

namespace node::domain {

InterlockVerdict FanTempInterlock::evaluate(const InterlockInput& in) {
  const std::optional<std::int32_t> value = in.store.good_value(config_.channel, in.now_ms);
  if (!value || (engaged_ && *value >= config_.off_milli)) {
    // 疑わしい値の間を「43℃未満が続いた」とは数えない（#38）。解除のしきい値以上に戻ったときも数え直す
    below_since_ms_.reset();
  } else if (!engaged_) {
    if (*value >= config_.on_milli) {
      engaged_ = true;
      below_since_ms_.reset();
    }
  } else {
    if (!below_since_ms_) {
      below_since_ms_ = in.now_ms;
    }
    if (in.now_ms - *below_since_ms_ >= config_.hold_ms) {
      engaged_ = false;
      below_since_ms_.reset();
    }
  }

  InterlockVerdict v{};
  if (engaged_) {
    v.force = config_.force;
  }
  return v;
}

}  // namespace node::domain
