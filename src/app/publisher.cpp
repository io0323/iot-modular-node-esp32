#include "app/publisher.hpp"

namespace node::app {

using infra::Kind;
using support::ErrorCode;
using support::make_error;
using support::Result;

Publisher::Publisher(infra::IMqttLink& link, infra::EnvelopeFactory& envelope, const infra::Topics& topics,
                     const infra::IClock& clock, infra::ILogSink& log)
    : link_(link), envelope_(envelope), topics_(topics), clock_(clock), log_(log) {}

Result<std::uint32_t> Publisher::send_now(Kind kind, std::string_view message) {
  Result<std::uint32_t> r =
      link_.publish(topics_.of(kind), message, infra::Topics::qos_of(kind), infra::Topics::retain_of(kind));
  if (r) {
    ++counters_.sent;
    log_tx(kind, message);
  }
  return r;
}

Result<Publisher::Sent> Publisher::enqueue_retry(Kind kind, std::string_view message, std::uint32_t seq) {
  Pending p{};
  p.kind = kind;
  if (!p.message.assign(message) || !retries_.push_back(p)) {
    ++counters_.retry_overflow;
    return make_error(ErrorCode::BufferFull, "retry queue full");
  }
  ++counters_.retry_queued;
  envelope_.commit_seq(kind);
  return Sent{0, seq};
}

Result<Publisher::Sent> Publisher::send_built(Kind kind, std::string_view message, std::uint32_t seq, bool commit) {
  const bool retryable = kind == Kind::CmdAck || kind == Kind::State;
  // cmd_ack・state は、送り直し待ちがあるうちは順序を守るため後ろに並べる
  if (retryable && commit && !retries_.empty()) {
    return enqueue_retry(kind, message, seq);
  }

  const auto deferred = [&]() -> Result<Sent> {
    if (kind == Kind::Telemetry) {
      return make_error(ErrorCode::Busy, "telemetry dropped");
    }
    if (kind == Kind::Event) {
      ++counters_.event_deferred;
      return make_error(ErrorCode::Busy, "event deferred");
    }
    if (!commit) {
      return make_error(ErrorCode::Busy, "busy");
    }
    return enqueue_retry(kind, message, seq);
  };

  if (!limiter_.try_take(kind, clock_.monotonic_ms())) {
    if (kind == Kind::Telemetry) {
      ++counters_.telemetry_rate_dropped;
    }
    return deferred();
  }
  const Result<std::uint32_t> r = send_now(kind, message);
  if (r) {
    if (commit) {
      envelope_.commit_seq(kind);
    }
    return Sent{r.value(), seq};
  }
  if (r.error().code == ErrorCode::Busy) {
    if (kind == Kind::Telemetry) {
      ++counters_.telemetry_busy_dropped;
    }
    return deferred();
  }
  return r.error();
}

Result<Publisher::Sent> Publisher::republish(Kind kind, std::string_view message, std::uint32_t seq) {
  if (!online()) {
    return make_error(ErrorCode::NotConnected, "not connected");
  }
  return send_built(kind, message, seq, false);
}

Result<void> Publisher::publish_uncounted(Kind kind, std::string_view message) {
  if (!online()) {
    return make_error(ErrorCode::NotConnected, "not connected");
  }
  const Result<std::uint32_t> r = send_now(kind, message);
  if (!r) {
    return r.error();
  }
  return support::ok();
}

void Publisher::flush_retries() {
  while (!retries_.empty() && online()) {
    Pending const& p = retries_.front();
    if (!limiter_.try_take(p.kind, clock_.monotonic_ms())) {
      return;
    }
    if (!send_now(p.kind, p.message.view())) {
      return;  // Busy などは次の tick に回す
    }
    retries_.erase_at(0);
  }
}

void Publisher::log_tx(Kind kind, std::string_view message) {
  infra::JsonWriter w(log_buf_);
  w.begin_object();
  w.key("topic").string(topics_.of(kind));
  w.key("message").raw(message);
  w.end_object();
  if (w.ok()) {
    log_.write("tx", w.view());
  }
}

}  // namespace node::app
