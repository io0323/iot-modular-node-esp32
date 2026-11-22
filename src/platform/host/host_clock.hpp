#pragma once

#include "infra/clock.hpp"

namespace node::platform {

/// @brief ホストの時計（steady_clock と system_clock）。ホストの時刻は合っているものとする（synced は常に true）。
class HostClock final : public infra::IClock {
 public:
  [[nodiscard]] std::uint64_t monotonic_ms() const override;
  [[nodiscard]] std::int64_t utc_ms() const override;
  [[nodiscard]] bool synced() const override { return true; }
};

}  // namespace node::platform
