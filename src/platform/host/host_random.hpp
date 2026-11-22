#pragma once

#include "infra/random.hpp"

namespace node::platform {

/// @brief OS の乱数（getentropy）。
class HostRandom final : public infra::IRandom {
 public:
  void fill(std::span<std::uint8_t> out) override;
};

}  // namespace node::platform
