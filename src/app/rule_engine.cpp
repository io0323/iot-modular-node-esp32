#include "app/rule_engine.hpp"

#include <algorithm>

namespace node::app {

RuleEngine::RuleEngine(std::span<const domain::Rule> rules)
    : rules_(rules.first(std::min(rules.size(), kMaxRules))) {}

std::optional<domain::Level> RuleEngine::step(const domain::Rule& r, State& st, std::optional<std::int32_t> value,
                                              std::uint64_t now_ms, bool active) {
  // パルスは時刻で数え続ける。長さを過ぎたら終わり
  if (st.pulse_start_ms && now_ms - *st.pulse_start_ms >= r.pulse_ms) st.pulse_start_ms.reset();

  if (!value) {
    // 不明：提案を出さず、実行中のパルスも止める（クールダウンはパルスの開始から数え続ける）
    st.on = false;
    st.pulse_start_ms.reset();
    return std::nullopt;
  }
  if (!active) {
    // 無効の間は状態を持ち越さない（有効になったら、いまの入力から決め直す。#35）
    st.on = false;
    return std::nullopt;
  }

  if (r.pulse_ms == 0) {
    if (!st.on && domain::enters(r, *value)) {
      st.on = true;
    } else if (st.on && domain::exits(r, *value)) {
      st.on = false;
    }
    return st.on ? std::optional<domain::Level>(r.level) : std::nullopt;
  }

  const bool cooling = st.last_pulse_ms && now_ms - *st.last_pulse_ms < r.cooldown_ms;
  if (!st.pulse_start_ms && !cooling && domain::enters(r, *value)) {
    st.pulse_start_ms = now_ms;
    st.last_pulse_ms = now_ms;
  }
  return st.pulse_start_ms ? std::optional<domain::Level>(r.level) : std::nullopt;
}

void RuleEngine::evaluate(const domain::ReadingStore& store, std::uint64_t now_ms, bool rules_enabled,
                          bool lighting_enabled, Proposals& out) {
  out.clear();
  for (std::size_t i = 0; i < rules_.size(); ++i) {
    const domain::Rule& r = rules_[i];
    const bool active = rules_enabled && (!r.lighting || lighting_enabled);
    const auto level = step(r, states_[i], store.good_value(r.channel, now_ms), now_ms, active);
    if (!level) continue;
    bool merged = false;
    for (Proposal& p : out) {
      if (p.actuator == r.actuator) {
        p.level = std::max(p.level, *level);  // 複数のルールなら大きい方
        merged = true;
      }
    }
    if (!merged) (void)out.push_back(Proposal{r.actuator, *level});
  }
}

}  // namespace node::app
