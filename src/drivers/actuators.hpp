// PWM のアクチュエーター（ファン・LED・ブザー）と、リレーのアクチュエーター（ポンプ）。
//
// 出典：データシートのある部品ではない（ESP32-S3 の LEDC と GPIO で駆動する）。値の決まりは P3 の決定：
// PWM（open-questions #44・#58）：level 0 は必ず出力 0、level 1000 は最大デューティ（2^分解能 − 1）。
//   デューティは level × (2^分解能 − 1) / 1000 を四捨五入。周波数と分解能は構成で持つ。
// ブザー（#45）：level をデューティとして扱う（500 で 50%）。
// リレー（#46）：level が 0 より大きければオン、0 ならオフ。オンの論理（High か Low か）は構成で持つ。
#pragma once

#include <cstdint>
#include <string_view>

#include "domain/device.hpp"
#include "hal/hal.hpp"

namespace node::drivers {

/// @brief level（0〜1000）を PWM のデューティにする。
[[nodiscard]] constexpr std::uint32_t level_to_duty(domain::Level level, std::uint8_t resolution_bits) {
  const std::uint32_t max = (std::uint32_t{1} << resolution_bits) - 1U;
  const std::uint32_t l = level > domain::kLevelMax ? domain::kLevelMax : level;
  return (l * max + domain::kLevelMax / 2) / domain::kLevelMax;
}

/// @brief PWM の設定。
struct PwmConfig {
  hal::Pin channel{0};
  std::uint32_t freq_hz{1000};
  std::uint8_t resolution_bits{10};
};

/// @brief PWM で出力するアクチュエーター。
class PwmActuator final : public domain::IActuator {
 public:
  PwmActuator(hal::IPwm& pwm, PwmConfig config, const domain::ActuatorSpec& spec)
      : pwm_(pwm), config_(config), spec_(spec) {}

  [[nodiscard]] std::string_view id() const override { return spec_.id; }
  support::Result<void> init() override {
    if (auto r = pwm_.configure(config_.channel, config_.freq_hz, config_.resolution_bits); !r) return r;
    return apply(0);
  }
  support::Result<void> apply(domain::Level level) override {
    if (auto r = pwm_.set_duty(config_.channel, level_to_duty(level, config_.resolution_bits)); !r) return r;
    level_ = level;
    return support::ok();
  }
  [[nodiscard]] domain::Level current() const override { return level_; }
  [[nodiscard]] const domain::ActuatorSpec& spec() const override { return spec_; }

 private:
  hal::IPwm& pwm_;
  PwmConfig config_;
  const domain::ActuatorSpec& spec_;
  domain::Level level_{0};
};

/// @brief リレー（GPIO のオン・オフ）で出力するアクチュエーター。
class RelayActuator final : public domain::IActuator {
 public:
  /// @param active_high High でオンなら true（既定。#46）
  RelayActuator(hal::IGpio& gpio, hal::Pin pin, bool active_high, const domain::ActuatorSpec& spec)
      : gpio_(gpio), pin_(pin), active_high_(active_high), spec_(spec) {}

  [[nodiscard]] std::string_view id() const override { return spec_.id; }
  support::Result<void> init() override {
    if (auto r = gpio_.configure_output(pin_); !r) return r;
    return apply(0);
  }
  support::Result<void> apply(domain::Level level) override {
    const bool on = level > 0;
    if (auto r = gpio_.write(pin_, on == active_high_); !r) return r;
    level_ = on ? domain::kLevelMax : domain::Level{0};
    return support::ok();
  }
  /// オン・オフしかないので、オンのときは 1000 を返す
  [[nodiscard]] domain::Level current() const override { return level_; }
  [[nodiscard]] const domain::ActuatorSpec& spec() const override { return spec_; }

 private:
  hal::IGpio& gpio_;
  hal::Pin pin_;
  bool active_high_;
  const domain::ActuatorSpec& spec_;
  domain::Level level_{0};
};

}  // namespace node::drivers
