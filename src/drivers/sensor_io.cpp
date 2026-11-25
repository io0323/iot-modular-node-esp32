#include "drivers/sensor_io.hpp"

#include <array>

namespace node::drivers {

bool SensorIo::read_once(domain::ISensor& s, domain::SensorReport& out) {
  std::array<domain::Reading, domain::SensorReport::kMaxReadings> buf{};
  const auto r = s.read(buf);
  if (!r) {
    ++out.read_errors;
    return false;
  }
  out.count = 0;
  for (std::size_t k = 0; k < r.value() && k < buf.size(); ++k) {
    out.readings[k] = buf[k];
    ++out.count;
  }
  return true;
}

domain::SensorReport SensorIo::execute(const domain::SensorRequest& request) {
  domain::SensorReport out{};
  out.index = request.index;
  out.op = request.op;
  out.at_ms = request.at_ms;
  if (request.index >= sensors_.size()) {
    return out;
  }
  domain::ISensor& s = *sensors_[request.index];
  switch (request.op) {
    case domain::SensorOp::Read:
      // 読めなければその場で続けて読み直す。3回続けて失敗したら回復を1回だけ要求する（#55）
      for (std::uint32_t attempt = 0; attempt < kAttemptsBeforeRecovery && !out.ok; ++attempt) {
        out.ok = read_once(s, out);
      }
      if (!out.ok) {
        out.recovery_requested = true;
        (void)s.request_recovery();
      }
      break;
    case domain::SensorOp::Reinit:
      out.ok = s.init() && read_once(s, out);
      break;
    case domain::SensorOp::FaultDue: break;
  }
  return out;
}

bool SensorIo::submit(const domain::SensorRequest& request) {
  if (pending_.full()) {
    return false;
  }
  return pending_.push_back(execute(request));
}

bool SensorIo::poll_report(domain::SensorReport& out) {
  if (pending_.empty()) {
    return false;
  }
  out = pending_.front();
  pending_.erase_at(0);
  return true;
}

}  // namespace node::drivers
