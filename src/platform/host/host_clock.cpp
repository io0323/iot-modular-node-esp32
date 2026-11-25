#include "platform/host/host_clock.hpp"

#include <algorithm>
#include <utility>

namespace node::platform {
namespace {
std::uint64_t steady_ms() {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}
std::int64_t system_ms() {
  const auto d = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}
}  // namespace

std::uint64_t HostClock::monotonic_ms() const { return steady_ms(); }

HostTimeSync::HostTimeSync(std::uint32_t delay_ms)
    : started_(std::chrono::steady_clock::now()), delay_(delay_ms) {}

bool HostTimeSync::synced() const {
  // 一度同期したら、以後は同期済み（#67）
  if (!synced_ && std::chrono::steady_clock::now() - started_ >= delay_) {
    synced_ = true;
  }
  return synced_;
}

std::int64_t HostTimeSync::utc_offset_ms() const { return system_ms() - static_cast<std::int64_t>(steady_ms()); }

HostNetwork::HostNetwork(std::vector<Window> down_windows)
    : started_(std::chrono::steady_clock::now()), down_(std::move(down_windows)) {}

bool HostNetwork::in_down_window() const {
  const auto elapsed = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count());
  return std::ranges::any_of(down_, [elapsed](const Window& w) { return elapsed >= w.begin_ms && elapsed < w.end_ms; });
}

support::Result<void> HostNetwork::connect() {
  pending_connect_ = true;
  return support::ok();
}

infra::NetworkEvent HostNetwork::poll_event() {
  const bool down = in_down_window();
  if (up_ && down) {
    up_ = false;
    return infra::NetworkEvent::Down;
  }
  if (pending_connect_) {
    pending_connect_ = false;
    if (down) {
      return infra::NetworkEvent::ConnectFailed;
    }
    up_ = true;
    return infra::NetworkEvent::Up;
  }
  return infra::NetworkEvent::None;
}

}  // namespace node::platform
