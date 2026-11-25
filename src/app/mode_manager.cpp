#include "app/mode_manager.hpp"

namespace node::app {
namespace {

using domain::Mode;

class AutoState final : public ModeState {
 public:
  [[nodiscard]] Mode mode() const override { return Mode::Auto; }
  [[nodiscard]] const ModeState& next_on_press() const override;
  [[nodiscard]] bool manual_enabled() const override { return false; }
  [[nodiscard]] bool rules_enabled() const override { return true; }
  [[nodiscard]] bool lighting_enabled() const override { return true; }
  [[nodiscard]] std::uint32_t period_factor() const override { return 1; }
  [[nodiscard]] bool times_out() const override { return false; }
};

class ManualState final : public ModeState {
 public:
  [[nodiscard]] Mode mode() const override { return Mode::Manual; }
  [[nodiscard]] const ModeState& next_on_press() const override;
  [[nodiscard]] bool manual_enabled() const override { return true; }
  [[nodiscard]] bool rules_enabled() const override { return false; }
  [[nodiscard]] bool lighting_enabled() const override { return false; }
  [[nodiscard]] std::uint32_t period_factor() const override { return 1; }
  [[nodiscard]] bool times_out() const override { return true; }
};

class PowerSaveState final : public ModeState {
 public:
  [[nodiscard]] Mode mode() const override { return Mode::PowerSave; }
  [[nodiscard]] const ModeState& next_on_press() const override;
  [[nodiscard]] bool manual_enabled() const override { return false; }
  [[nodiscard]] bool rules_enabled() const override { return true; }
  [[nodiscard]] bool lighting_enabled() const override { return false; }
  [[nodiscard]] std::uint32_t period_factor() const override { return 5; }
  [[nodiscard]] bool times_out() const override { return false; }
};

const AutoState kAuto{};
const ManualState kManual{};
const PowerSaveState kPowerSave{};

const ModeState& AutoState::next_on_press() const { return kManual; }
const ModeState& ManualState::next_on_press() const { return kPowerSave; }
const ModeState& PowerSaveState::next_on_press() const { return kAuto; }

}  // namespace

ModeManager::ModeManager() : state_(&kAuto) {}

const ModeState& ModeManager::state_of(Mode m) {
  switch (m) {
    case Mode::Auto: return kAuto;
    case Mode::Manual: return kManual;
    case Mode::PowerSave: return kPowerSave;
  }
  return kAuto;
}

std::optional<ModeChange> ModeManager::go(const ModeState& to, ModeCause cause, std::uint64_t now_ms) {
  if (&to == state_) {
    return std::nullopt;
  }
  const ModeChange c{state_->mode(), to.mode(), cause};
  state_ = &to;
  last_activity_ms_ = now_ms;  // 手動モードに入った時刻からも30分を数える
  return c;
}

std::optional<ModeChange> ModeManager::press(std::uint64_t now_ms) {
  return go(state_->next_on_press(), ModeCause::Button, now_ms);
}

std::optional<ModeChange> ModeManager::request(Mode to, std::uint64_t now_ms) {
  return go(state_of(to), ModeCause::Cmd, now_ms);
}

std::optional<ModeChange> ModeManager::tick(std::uint64_t now_ms) {
  if (!state_->times_out() || now_ms - last_activity_ms_ < kManualTimeoutMs) {
    return std::nullopt;
  }
  return go(kAuto, ModeCause::Timeout, now_ms);
}

}  // namespace node::app
