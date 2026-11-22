#include "platform/host/host_clock.hpp"

#include <chrono>

namespace node::platform {

std::uint64_t HostClock::monotonic_ms() const {
  const auto d = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

std::int64_t HostClock::utc_ms() const {
  const auto d = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(d).count());
}

}  // namespace node::platform
