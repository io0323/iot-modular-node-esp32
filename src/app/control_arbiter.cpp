#include "app/control_arbiter.hpp"

#include <algorithm>

namespace node::app {

using domain::Level;
using domain::Owner;
using domain::PumpInterlock;

ControlArbiter::ControlArbiter(std::span<domain::IActuator* const> actuators) {
  for (domain::IActuator* a : actuators) {
    Slot s{};
    s.actuator = a;
    if (a->spec().max_on_s) s.interlock.emplace(*a->spec().max_on_s);
    if (!a->apply(0)) ++apply_errors_;
    if (!slots_.push_back(s)) break;  // kMaxActuators を超える分は扱わない（構成表の誤り）
  }
}

std::optional<std::size_t> ControlArbiter::find(std::string_view id) const {
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].actuator->id() == id) return i;
  }
  return std::nullopt;
}

ControlArbiter::CommandResult ControlArbiter::set_command(std::size_t index, Level level,
                                                          domain::Priority priority, std::uint32_t lease_s,
                                                          std::uint64_t now_ms, InterlockChanges& changes) {
  Slot& s = slots_[index];
  const domain::ActuatorSpec& spec = s.actuator->spec();
  const Level clamped = std::clamp(level, spec.min, spec.max);
  s.has_lease = true;
  s.lease_level = clamped;
  s.priority = priority;
  s.lease = domain::Lease(now_ms, lease_s);
  evaluate(s, now_ms, changes);

  CommandResult r{};
  r.applied = s.output;
  const bool rounded = clamped != level;
  const bool held_by_interlock = s.interlock && s.interlock->active();
  const bool lease_exceeds_max_on = spec.max_on_s && lease_s > *spec.max_on_s;
  r.limited = rounded || held_by_interlock || lease_exceeds_max_on;
  return r;
}

void ControlArbiter::release(std::size_t index, std::uint64_t now_ms, InterlockChanges& changes) {
  Slot& s = slots_[index];
  s.has_lease = false;
  evaluate(s, now_ms, changes);
}

void ControlArbiter::tick(std::uint64_t now_ms, InterlockChanges& changes) {
  for (Slot& s : slots_) evaluate(s, now_ms, changes);
}

void ControlArbiter::evaluate(Slot& s, std::uint64_t now_ms, InterlockChanges& changes) {
  if (s.has_lease && s.lease.expired(now_ms)) s.has_lease = false;
  const Level requested = s.has_lease ? s.lease_level : Level{0};

  Level out = requested;
  if (s.interlock) {
    const PumpInterlock::Output r = s.interlock->update(requested, now_ms);
    out = r.level;
    if (r.transition != PumpInterlock::Transition::None) {
      (void)changes.push_back(InterlockChange{s.actuator->id(), r.transition});
    }
  }

  if (s.interlock && s.interlock->active()) {
    s.owner = Owner::Interlock;
  } else if (s.has_lease) {
    s.owner = domain::owner_of(s.priority);
  } else {
    s.owner = Owner::None;
  }

  if (out != s.output || out != s.actuator->current()) {
    if (!s.actuator->apply(out)) ++apply_errors_;
    s.output = out;
  }
}

}  // namespace node::app
