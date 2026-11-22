#include "app/manual_control.hpp"

namespace node::app {

ManualControl::Outcome ManualControl::set_manual(std::string_view id, domain::Level level, std::uint64_t now_ms) {
  if (modes_.mode() != domain::Mode::Manual) return Outcome::NotManualMode;
  const auto index = arbiter_.find(id);
  if (!index) return Outcome::UnknownTarget;
  arbiter_.set_manual(*index, level);
  modes_.touch_manual(now_ms);
  return Outcome::Accepted;
}

}  // namespace node::app
