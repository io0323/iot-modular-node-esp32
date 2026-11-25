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
  if (factor == 0 || factor == factor_) {
    return;
  }
  factor_ = factor;
  register_periods();
  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    states_[i].next_due_ms = states_[i].last_read_ms + period_ms(*sensors_[i]);  // #40
  }
}

void SensorPoller::advance_due(State& st, const domain::ISensor& s, std::uint64_t now_ms) const {
  const std::uint64_t period = period_ms(s);
  // 遅れが周期を超えたら、取り戻さずに次の周期から数え直す
  st.next_due_ms = (now_ms - st.next_due_ms >= period) ? now_ms + period : st.next_due_ms + period;
  st.last_read_ms = now_ms;
}

void SensorPoller::store_readings(const domain::SensorReport& report, Result& out) {
  for (std::size_t k = 0; k < report.count && k < report.readings.size(); ++k) {
    (void)store_.update(report.readings[k], report.at_ms);
    (void)out.readings.push_back(report.readings[k]);
  }
}

void SensorPoller::add_fault_readings(const domain::ISensor& s, Result& out) const {
  for (const domain::ChannelSpec& ch : s.channels()) {
    (void)out.readings.push_back(
        domain::Reading{ch.channel, store_.last_good_value(ch.channel), ch.unit, domain::Quality::Fault});
  }
}

void SensorPoller::mark_faulted(std::size_t i, std::uint64_t now_ms, Result& out) {
  State& st = states_[i];
  st.faulted = true;
  st.next_reinit_ms = now_ms + kReinitIntervalMs;
  for (const domain::ChannelSpec& ch : sensors_[i]->channels()) {
    (void)store_.mark_fault(ch.channel, now_ms);
    (void)out.changes.push_back(SensorChange{ch.channel, true});
  }
  add_fault_readings(*sensors_[i], out);
}

void SensorPoller::plan(std::uint64_t now_ms, Requests& out) {
  out.clear();
  for (std::size_t i = 0; i < sensors_.size(); ++i) {
    State& st = states_[i];
    const auto index = static_cast<std::uint8_t>(i);
    if (st.faulted) {
      // 故障の間は30秒ごとに再初期化を試みる。周期が来たら、読めないチャネルも fault で telemetry に入れる（#52）
      if (now_ms >= st.next_reinit_ms) {
        st.next_reinit_ms = now_ms + kReinitIntervalMs;
        (void)out.push_back(domain::SensorRequest{index, domain::SensorOp::Reinit, now_ms});
      } else if (now_ms >= st.next_due_ms) {
        advance_due(st, *sensors_[i], now_ms);
        (void)out.push_back(domain::SensorRequest{index, domain::SensorOp::FaultDue, now_ms});
      }
      continue;
    }
    if (now_ms < st.next_due_ms) {
      continue;
    }
    advance_due(st, *sensors_[i], now_ms);
    (void)out.push_back(domain::SensorRequest{index, domain::SensorOp::Read, now_ms});
  }
}

void SensorPoller::apply_reinit(const domain::SensorReport& report, Result& out) {
  const std::size_t i = report.index;
  const domain::ISensor& s = *sensors_[i];
  State& st = states_[i];
  if (report.ok) {
    // 再初期化して続けて読めたら復旧
    store_readings(report, out);
    st.faulted = false;
    st.last_read_ms = report.at_ms;
    st.next_due_ms = report.at_ms + period_ms(s);
    for (const domain::ChannelSpec& ch : s.channels()) {
      (void)out.changes.push_back(SensorChange{ch.channel, false});
    }
    return;
  }
  if (report.at_ms >= st.next_due_ms) {
    advance_due(st, s, report.at_ms);
    add_fault_readings(s, out);
  }
}

void SensorPoller::apply(const domain::SensorReport& report, Result& out) {
  if (report.index >= sensors_.size()) {
    return;
  }
  read_errors_ += report.read_errors;
  switch (report.op) {
    case domain::SensorOp::Read:
      if (report.ok) {
        store_readings(report, out);
        return;
      }
      // 3回続けて失敗した：回復を要求したので、そのセンサーのチャネルを故障にする（#55）
      if (report.recovery_requested) {
        ++recoveries_;
      }
      mark_faulted(report.index, report.at_ms, out);
      return;
    case domain::SensorOp::Reinit: apply_reinit(report, out); return;
    case domain::SensorOp::FaultDue: add_fault_readings(*sensors_[report.index], out); return;
  }
}

void SensorPoller::tick(std::uint64_t now_ms, domain::ISensorIo& io, Result& out) {
  out.readings.clear();
  out.changes.clear();
  plan(now_ms, requests_);
  for (const domain::SensorRequest& r : requests_) {
    (void)io.submit(r);
  }
  for (domain::SensorReport report{}; io.poll_report(report);) {
    apply(report, out);
  }
}

}  // namespace node::app
