#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

#include "infra/clock.hpp"
#include "infra/network.hpp"
#include "infra/time_sync.hpp"

namespace node::platform {

/// @brief ホストの単調時計（steady_clock）。
class HostClock final : public infra::IClock {
 public:
  [[nodiscard]] std::uint64_t monotonic_ms() const override;
};

/// @brief ホストの時刻同期。ホストの時刻は合っているものとし、起動から delay_ms たったら同期済みにする
/// （--sntp-delay。P4）。差は system_clock − steady_clock の最新の値。
class HostTimeSync final : public infra::ITimeSync {
 public:
  explicit HostTimeSync(std::uint32_t delay_ms = 0);
  [[nodiscard]] bool synced() const override;
  [[nodiscard]] std::int64_t utc_offset_ms() const override;

 private:
  std::chrono::steady_clock::time_point started_;
  std::chrono::milliseconds delay_;
  mutable bool synced_{false};
};

/// @brief ホストのネットワーク。いつも使えるが、--net-down で指定した時間帯だけ使えなくする（P4）。
class HostNetwork final : public infra::INetwork {
 public:
  /// @brief 使えない時間帯（起動からの ms、[begin, end)）。
  struct Window {
    std::uint64_t begin_ms;
    std::uint64_t end_ms;
  };

  explicit HostNetwork(std::vector<Window> down_windows = {});
  support::Result<void> connect() override;
  [[nodiscard]] bool up() const override { return up_; }
  infra::NetworkEvent poll_event() override;

 private:
  [[nodiscard]] bool in_down_window() const;
  std::chrono::steady_clock::time_point started_;
  std::vector<Window> down_;
  bool up_{false};
  bool pending_connect_{false};
};

}  // namespace node::platform
