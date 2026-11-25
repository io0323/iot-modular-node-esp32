#include "drivers/ui_io.hpp"

#include <array>
#include <string_view>

namespace node::drivers {

support::Result<void> UiIo::init() {
  for (domain::IButton* b : {dev_.mode_button, dev_.action_button}) {
    if (b == nullptr) {
      continue;
    }
    if (auto r = b->init(); !r) {
      return r;
    }
  }
  if (dev_.display != nullptr) {
    if (auto r = dev_.display->init(); !r) {
      return r;
    }
  }
  return support::ok();
}

void UiIo::push(domain::UiInputKind kind, domain::ButtonEvent event) {
  (void)pending_.push_back(domain::UiInput{kind, event});
}

bool UiIo::poll_input(std::uint64_t now_ms, domain::UiInput& out) {
  if (pending_.empty() && now_ms >= next_button_ms_) {
    next_button_ms_ = now_ms + kButtonPeriodMs;
    if (dev_.mode_button != nullptr) {
      if (const domain::ButtonEvent e = dev_.mode_button->poll(now_ms); e != domain::ButtonEvent::None) {
        push(domain::UiInputKind::ModeButton, e);
      }
    }
    if (dev_.action_button != nullptr) {
      if (const domain::ButtonEvent e = dev_.action_button->poll(now_ms); e != domain::ButtonEvent::None) {
        push(domain::UiInputKind::ActionButton, e);
      }
    }
  }
  if (pending_.empty()) {
    return false;
  }
  out = pending_.front();
  pending_.erase_at(0);
  return true;
}

void UiIo::show(const domain::DisplayFrame& frame) {
  if (dev_.display == nullptr) {
    return;
  }
  if (frame.power_on != display_on_) {
    if (!dev_.display->set_power(frame.power_on)) {
      push(domain::UiInputKind::DisplayError, domain::ButtonEvent::None);  // 省電力モードでは表示を消す
    }
    display_on_ = frame.power_on;
  }
  if (!display_on_ || !frame.has_lines) {
    return;
  }
  std::array<std::string_view, domain::IDisplay::kRows> views{};
  for (std::size_t i = 0; i < frame.lines.size(); ++i) {
    views[i] = frame.lines[i].view();
  }
  if (!dev_.display->show(views)) {
    push(domain::UiInputKind::DisplayError, domain::ButtonEvent::None);
  }
}

}  // namespace node::drivers
