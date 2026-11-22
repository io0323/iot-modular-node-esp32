#pragma once

#include <chrono>
#include <mutex>
#include <thread>

#include "hal/hal.hpp"

namespace node::platform {

/// @brief ホストのミューテックス（std::timed_mutex）。
class HostMutex final : public hal::IMutex {
 public:
  bool try_lock_for(std::uint32_t timeout_ms) override {
    return mu_.try_lock_for(std::chrono::milliseconds(timeout_ms));
  }
  void unlock() override { mu_.unlock(); }

 private:
  std::timed_mutex mu_;
};

/// @brief ホストのタイマー（steady_clock と sleep_for）。
class HostTimer final : public hal::ITimer {
 public:
  [[nodiscard]] std::uint64_t now_ms() const override {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }
  void delay_ms(std::uint32_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

}  // namespace node::platform
