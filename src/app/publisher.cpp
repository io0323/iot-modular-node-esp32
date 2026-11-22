#include "app/publisher.hpp"

namespace node::app {

using infra::Kind;
using support::ErrorCode;
using support::make_error;
using support::Result;

Publisher::Publisher(infra::IMqttLink& link, infra::EnvelopeFactory& envelope, const infra::Topics& topics,
                     const infra::IClock& clock, infra::ILogSink& log)
    : link_(link), envelope_(envelope), topics_(topics), clock_(clock), log_(log) {}

Result<void> Publisher::send_now(Kind kind, std::string_view message) {
  Result<void> r =
      link_.publish(topics_.of(kind), message, infra::Topics::qos_of(kind), infra::Topics::retain_of(kind));
  if (r) {
    ++counters_.sent;
    log_tx(kind, message);
  }
  return r;
}

Result<void> Publisher::enqueue_retry(Kind kind, std::string_view message) {
  Pending p{};
  p.kind = kind;
  if (!p.message.assign(message) || !retries_.push_back(p)) {
    ++counters_.retry_overflow;
    return make_error(ErrorCode::BufferFull, "retry queue full");
  }
  ++counters_.retry_queued;
  envelope_.commit_seq(kind);
  return support::ok();
}

Result<void> Publisher::send_built(Kind kind, std::string_view message) {
  const std::uint64_t now = clock_.monotonic_ms();
  // 再送待ちがあるうちは順序を守るため後ろに並べる
  if (kind != Kind::Telemetry && !retries_.empty()) return enqueue_retry(kind, message);

  if (!limiter_.try_take(kind, now)) {
    if (kind == Kind::Telemetry) {
      ++counters_.telemetry_rate_dropped;
      return make_error(ErrorCode::Busy, "rate limited");
    }
    return enqueue_retry(kind, message);
  }
  Result<void> r = send_now(kind, message);
  if (r) {
    envelope_.commit_seq(kind);
    return r;
  }
  if (r.error().code == ErrorCode::Busy) {
    if (kind == Kind::Telemetry) {
      ++counters_.telemetry_busy_dropped;
      return r;
    }
    return enqueue_retry(kind, message);
  }
  return r;
}

Result<void> Publisher::publish_uncounted(Kind kind, std::string_view message) {
  if (!online()) return make_error(ErrorCode::NotConnected, "not connected");
  return send_now(kind, message);
}

void Publisher::flush_retries() {
  while (!retries_.empty() && online()) {
    Pending& p = retries_.front();
    if (!limiter_.try_take(p.kind, clock_.monotonic_ms())) return;
    if (!send_now(p.kind, p.message.view())) return;  // Busy などは次の tick に回す
    retries_.erase_at(0);
  }
}

void Publisher::log_tx(Kind kind, std::string_view message) {
  infra::JsonWriter w(log_buf_);
  w.begin_object();
  w.key("topic").string(topics_.of(kind));
  w.key("message").raw(message);
  w.end_object();
  if (w.ok()) log_.write("tx", w.view());
}

}  // namespace node::app
