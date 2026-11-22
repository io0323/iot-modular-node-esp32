#include "app/sensor_poller.hpp"

#include <algorithm>

namespace node::app {

SensorPoller::SensorPoller(std::span<domain::ISensor* const> sensors, domain::ReadingStore& store)
    : sensors_(sensors.first(std::min(sensors.size(), kMaxSensors))), store_(store) {}

std::uint64_t SensorPoller::period_ms(const domain::ISensor& s) const {
  return static_cast<std::uint64_t>(s.period().count()) * factor_;
}

void SensorPoller::register_periods() {
  for (const domain::ISensor* s : sensors_) {
    for (const domain::ChannelSpec& ch : s->channels()) {
      (void)store_.set_period(ch.channel, ch.period_ms * factor_);
    }
  }
}

void SensorPoller::start(std::uint64_t now_ms) {
  register_periods();
  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    states_[i].next_due_ms = now_ms;
    states_[i].last_read_ms = now_ms;
  }
}

void SensorPoller::set_period_factor(std::uint32_t factor) {
  if (factor == 0 || factor == factor_) return;
  factor_ = factor;
  register_periods();
  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    states_[i].next_due_ms = states_[i].last_read_ms + period_ms(*sensors_[i]);  // #40
  }
}

bool SensorPoller::read_into(domain::ISensor& s, std::uint64_t now_ms, Result& out) {
  std::array<domain::Reading, kMaxReadings> buf{};
  const auto r = s.read(buf);
  if (!r) {
    ++read_errors_;
    return false;
  }
  for (std::size_t k = 0; k < r.value() && k < buf.size(); ++k) {
    (void)store_.update(buf[k], now_ms);
    (void)out.readings.push_back(buf[k]);
  }
  return true;
}

void SensorPoller::mark_faulted(std::size_t i, std::uint64_t now_ms, Result& out) {
  State& st = states_[i];
  st.faulted = true;
  st.next_reinit_ms = now_ms + kReinitIntervalMs;
  for (const domain::ChannelSpec& ch : sensors_[i]->channels()) {
    (void)store_.mark_fault(ch.channel, now_ms);
    (void)out.changes.push_back(SensorChange{ch.channel, true});
  }
}

void SensorPoller::tick(std::uint64_t now_ms, Result& out) {
  out.readings.clear();
  out.changes.clear();
  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    domain::ISensor& s = *sensors_[i];
    State& st = states_[i];
    if (st.faulted) {
      // 故障の間は30秒ごとに再初期化を試み、続けて読めたら復旧
      if (now_ms < st.next_reinit_ms) continue;
      st.next_reinit_ms = now_ms + kReinitIntervalMs;
      if (!s.init()) continue;
      if (!read_into(s, now_ms, out)) continue;
      st.faulted = false;
      st.last_read_ms = now_ms;
      st.next_due_ms = now_ms + period_ms(s);
      for (const domain::ChannelSpec& ch : s.channels()) (void)out.changes.push_back(SensorChange{ch.channel, false});
      continue;
    }
    if (now_ms < st.next_due_ms) continue;
    const std::uint64_t period = period_ms(s);
    // 遅れが周期を超えたら、取り戻さずに次の周期から数え直す
    st.next_due_ms = (now_ms - st.next_due_ms >= period) ? now_ms + period : st.next_due_ms + period;
    st.last_read_ms = now_ms;
    if (!read_into(s, now_ms, out)) mark_faulted(i, now_ms, out);
  }
}

}  // namespace node::app
