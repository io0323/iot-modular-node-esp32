#include "app/control_arbiter.hpp"

#include <algorithm>

namespace node::app {

using domain::Level;
using domain::Owner;

namespace {
Level clamp_to(const domain::ActuatorSpec& spec, Level v) { return std::clamp(v, spec.min, spec.max); }
}  // namespace

ControlArbiter::ControlArbiter(std::span<const domain::ActuatorBinding> actuators, const domain::ReadingStore& store)
    : store_(store) {
  for (const domain::ActuatorBinding& b : actuators) {
    Slot s{};
    s.actuator = b.actuator;
    const std::size_t n = std::min(b.interlocks.size(), kMaxInterlocksPerActuator);
    s.interlocks = b.interlocks.first(n);  // 超える分は扱わない（構成表の誤り）
    if (!b.actuator->apply(0)) {
      ++apply_errors_;
    }
    if (!slots_.push_back(s)) {
      break;  // kMaxActuators を超える分は扱わない（構成表の誤り）
    }
  }
}

std::optional<std::size_t> ControlArbiter::find(std::string_view id) const {
  for (std::size_t i = 0; i < slots_.size(); ++i) {
    if (slots_[i].actuator->id() == id) {
      return i;
    }
  }
  return std::nullopt;
}

ControlArbiter::CommandResult ControlArbiter::set_command(std::size_t index, Level level, domain::Priority priority,
                                                          std::uint32_t lease_s, std::uint64_t now_ms,
                                                          InterlockChanges& changes) {
  Slot& s = slots_[index];
  const domain::ActuatorSpec& act = s.actuator->spec();
  const Level clamped = clamp_to(act, level);
  s.has_lease = true;
  s.lease_level = clamped;
  s.priority = priority;
  s.lease = domain::Lease(now_ms, lease_s);
  evaluate(s, now_ms, changes);

  CommandResult r{};
  r.applied = s.output;
  const bool rounded = clamped != level;
  const bool changed_by_interlock = s.output != clamped;
  const bool lease_exceeds_max_on = act.max_on_s && lease_s > *act.max_on_s;
  r.limited = rounded || changed_by_interlock || lease_exceeds_max_on;
  return r;
}

void ControlArbiter::release(std::size_t index, std::uint64_t now_ms, InterlockChanges& changes) {
  Slot& s = slots_[index];
  s.has_lease = false;
  evaluate(s, now_ms, changes);
}

void ControlArbiter::set_manual(std::size_t index, std::optional<Level> level) {
  if (index < slots_.size()) {
    slots_[index].manual = level;
  }
}

void ControlArbiter::clear_manual() {
  for (Slot& s : slots_) {
    s.manual.reset();
  }
}

void ControlArbiter::set_rule_proposal(std::size_t index, std::optional<Level> level) {
  if (index < slots_.size()) {
    slots_[index].rule = level;
  }
}

void ControlArbiter::set_stages(bool manual_enabled, bool rules_enabled) {
  manual_enabled_ = manual_enabled;
  rules_enabled_ = rules_enabled;
}

bool ControlArbiter::rejects_commands(std::size_t index, std::uint64_t now_ms) const {
  return std::ranges::any_of(slots_[index].interlocks,
                             [&](const domain::IInterlock* il) { return il->rejects_commands(store_, now_ms); });
}

void ControlArbiter::tick(std::uint64_t now_ms, InterlockChanges& changes) {
  for (Slot& s : slots_) {
    evaluate(s, now_ms, changes);
  }
}

ControlArbiter::StageResult ControlArbiter::stages(const Slot& s) const {
  const domain::ActuatorSpec& act = s.actuator->spec();
  if (s.has_lease) {
    return {s.lease_level, domain::owner_of(s.priority)};
  }
  if (manual_enabled_ && s.manual) {
    return {clamp_to(act, *s.manual), Owner::Local};
  }
  if (rules_enabled_ && s.rule) {
    return {clamp_to(act, *s.rule), Owner::Local};
  }
  return {clamp_to(act, 0), Owner::None};
}

namespace {

/// 複数のインターロックの判定をまとめたもの（強制値は大きい方、上限は小さい方）
struct Combined {
  std::optional<Level> force;
  std::optional<Level> upper;
};

void merge(Combined& c, const domain::InterlockVerdict& v) {
  if (v.force) {
    c.force = c.force ? std::max(*c.force, *v.force) : *v.force;
  }
  if (v.upper) {
    c.upper = c.upper ? std::min(*c.upper, *v.upper) : *v.upper;
  }
}

/// このインターロックが出力を実際に変えるか
bool changes_output(const domain::InterlockVerdict& v, Level pre) {
  if (v.force) {
    return *v.force != pre;
  }
  return v.upper && pre > *v.upper;
}

Level apply(const Combined& c, Level pre) {
  if (c.force) {
    return *c.force;
  }
  return c.upper ? std::min(pre, *c.upper) : pre;
}

}  // namespace

Level ControlArbiter::apply_interlocks(Slot& s, Level pre, std::uint64_t now_ms, InterlockChanges& changes) {
  Combined combined{};
  std::array<bool, kMaxInterlocksPerActuator> changed{};
  for (std::size_t i = 0; i < s.interlocks.size(); ++i) {
    const domain::InterlockVerdict v = s.interlocks[i]->evaluate(domain::InterlockInput{store_, pre, now_ms});
    merge(combined, v);
    changed[i] = changes_output(v, pre);
  }
  announce(s, changed, changes);
  return apply(combined, pre);
}

void ControlArbiter::announce(Slot& s, const std::array<bool, kMaxInterlocksPerActuator>& changes_output,
                              InterlockChanges& changes) {
  // INTERLOCK_ACTIVE は出力を実際に変えたときに、RELEASED は ACTIVE を出した回の条件が解けたときに出す（#24・#37）
  for (std::size_t i = 0; i < s.interlocks.size(); ++i) {
    domain::IInterlock const& il = *s.interlocks[i];
    if (il.engaged() && changes_output[i] && !s.announced[i]) {
      s.announced[i] = true;
      (void)changes.push_back(InterlockChange{s.actuator->id(), il.rule(), true});
    } else if (!il.engaged() && s.announced[i]) {
      s.announced[i] = false;
      (void)changes.push_back(InterlockChange{s.actuator->id(), il.rule(), false});
    }
  }
}

Level ControlArbiter::hold_for_min_time(const Slot& s, Level wanted, std::uint64_t now_ms) {
  // 0 と 0 より大きい値の切り替えだけを待たせる（オンの間の値の変更はすぐ反映。#59）
  const domain::ActuatorSpec& act = s.actuator->spec();
  const bool is_on = s.output > 0;
  const bool want_on = wanted > 0;
  if (is_on == want_on) {
    return wanted;
  }
  const std::optional<std::uint32_t> min = is_on ? act.min_on_ms : act.min_off_ms;
  if (min && now_ms - s.switched_at_ms < *min) {
    return s.output;
  }
  return wanted;
}

void ControlArbiter::evaluate(Slot& s, std::uint64_t now_ms, InterlockChanges& changes) {
  if (s.has_lease && s.lease.expired(now_ms)) {
    s.has_lease = false;
  }
  const StageResult pre = stages(s);
  const Level guarded = apply_interlocks(s, pre.level, now_ms, changes);
  // インターロックで値を変えたときは最小オン・オフ時間より優先する（#47）
  const Level out = guarded != pre.level ? guarded : hold_for_min_time(s, guarded, now_ms);
  s.owner = guarded != pre.level ? Owner::Interlock : pre.owner;
  if (out != s.output || out != s.actuator->current()) {
    if ((out > 0) != (s.output > 0)) {
      s.switched_at_ms = now_ms;
    }
    if (!s.actuator->apply(out)) {
      ++apply_errors_;
    }
    s.output = out;
  }
}

}  // namespace node::app
