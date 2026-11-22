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

ManualControl::Outcome ManualControl::select_next() {
  if (modes_.mode() != domain::Mode::Manual) return Outcome::NotManualMode;
  if (arbiter_.size() == 0) return Outcome::UnknownTarget;
  selected_ = (selected_ + 1) % arbiter_.size();
  return Outcome::Accepted;
}

ManualControl::Outcome ManualControl::toggle_selected(std::uint64_t now_ms) {
  if (modes_.mode() != domain::Mode::Manual) return Outcome::NotManualMode;
  if (selected_ >= arbiter_.size()) return Outcome::UnknownTarget;
  const auto current = arbiter_.manual(selected_);
  const domain::Level next = (current && *current > 0) ? domain::Level{0} : domain::kLevelMax;
  return set_manual(arbiter_.spec(selected_).id, next, now_ms);
}

std::string_view ManualControl::selected_id() const {
  return selected_ < arbiter_.size() ? arbiter_.spec(selected_).id : std::string_view{};
}

}  // namespace node::app
