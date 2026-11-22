#include "app/node.hpp"

namespace node::app {

using infra::Kind;
using infra::LinkEvent;

Node::Node(const NodeConfig& config, domain::DeviceRegistry registry, infra::IMqttLink& link,
           const infra::IClock& clock, infra::IRandom& rng, infra::ILogSink& log)
    : config_(config),
      registry_(registry),
      link_(link),
      clock_(clock),
      log_(log),
      envelope_(config.device_id, clock, rng),
      topics_(config.site, config.device_id),
      publisher_(link, envelope_, topics_, clock, log),
      arbiter_(registry.actuators()),
      commands_(arbiter_, store_),
      state_(registry.sensors(), arbiter_, config.fw_version),
      telemetry_(registry.sensors(), arbiter_, store_) {}

support::Result<void> Node::start() {
  if (!topics_.ok()) return support::make_error(support::ErrorCode::InvalidArgument, "topic too long");
  for (domain::ISensor* s : registry_.sensors()) {
    if (auto r = s->init(); !r) return r;
  }
  for (domain::IActuator* a : registry_.actuators()) {
    if (auto r = a->init(); !r) return r;
  }
  const std::uint64_t now = clock_.monotonic_ms();
  telemetry_.start(now);
  next_connect_ms_ = now;
  next_diag_ms_ = now + config_.diag_interval_ms;

  // BOOT は接続後に state（online）の次に送る
  std::array<char, 128> detail{};
  infra::JsonWriter w(detail);
  w.begin_object();
  w.key("reset_reason").string(config_.reset_reason);
  w.key("platform_reason").string(config_.platform_reason);
  w.end_object();
  (void)events_.post("BOOT", Severity::Info, w.ok() ? w.view() : std::string_view{}, envelope_.now());

  std::array<char, 160> fields{};
  infra::JsonWriter f(fields);
  f.begin_object();
  f.key("boot_id").string(envelope_.boot_id());
  f.key("reset_reason").string(config_.reset_reason);
  f.end_object();
  if (f.ok()) log_.write("boot", f.view());
  return support::ok();
}

void Node::tick() {
  const std::uint64_t now = clock_.monotonic_ms();
  handle_link_events(now);
  if (link_state_ == LinkState::Disconnected && now >= next_connect_ms_) try_connect(now);
  handle_inbound(now);

  ControlArbiter::InterlockChanges changes{};
  arbiter_.tick(now, changes);
  post_interlock_events(changes);

  telemetry_.tick(now, publisher_);
  if (link_state_ == LinkState::Connected) {
    publisher_.flush_retries();
    events_.flush(publisher_);
  }
  if (now >= next_diag_ms_) {
    next_diag_ms_ = now + config_.diag_interval_ms;
    log_diag();
  }
}

void Node::shutdown() {
  if (link_state_ != LinkState::Connected) return;
  std::array<char, Publisher::kMessageMax>& buf = work_buf_;
  const std::string_view offline = build_offline_state(buf);
  if (!offline.empty()) (void)publisher_.publish_uncounted(Kind::State, offline);
  log_diag();
}

void Node::handle_link_events(std::uint64_t now) {
  for (LinkEvent ev = link_.poll_event(); ev != LinkEvent::None; ev = link_.poll_event()) {
    switch (ev) {
      case LinkEvent::Connected:
        link_state_ = LinkState::Connected;
        publisher_.set_online(true);
        log_fields("connect", "result", "ok");
        on_connected();
        break;
      case LinkEvent::ConnectFailed:
      case LinkEvent::Disconnected:
        link_state_ = LinkState::Disconnected;
        publisher_.set_online(false);
        next_connect_ms_ = now + config_.reconnect_interval_ms;
        publisher_.clear_retries();
        log_fields("disconnect", "cause", ev == LinkEvent::ConnectFailed ? "connect_failed" : "connection_lost");
        break;
      case LinkEvent::None: break;
    }
  }
}

std::string_view Node::build_offline_state(std::array<char, Publisher::kMessageMax>& buf) {
  // offline の state は seq を進めず、その時点で次に使う値を入れる（共通仕様 v0.2.1 #29）
  infra::JsonWriter w(buf);
  envelope_.begin(w, Kind::State, envelope_.peek_seq(Kind::State), envelope_.now());
  state_.write_payload(w, false, commands_.mode());
  w.end_object();
  return w.ok() ? w.view() : std::string_view{};
}

void Node::try_connect(std::uint64_t now) {
  // LWT は接続のたびに作り直す（ts は接続時刻、seq はその時点で次に使う値、boot_id は online と同じ）
  const std::string_view will = build_offline_state(will_buf_);
  will_len_ = will.size();
  infra::ConnectOptions opt{};
  opt.client_id = config_.device_id;
  opt.username = config_.username;
  opt.password = config_.password;
  opt.keepalive_s = config_.keepalive_s;
  opt.will = infra::Will{topics_.of(Kind::State), will, infra::Qos::AtLeastOnce, true};
  if (link_.connect(opt)) {
    link_state_ = LinkState::Connecting;
    log_fields("connect", "result", "started");
  } else {
    next_connect_ms_ = now + config_.reconnect_interval_ms;
    log_fields("connect", "result", "error");
  }
}

void Node::on_connected() {
  if (!link_.subscribe(topics_.cmd(), infra::Qos::AtLeastOnce)) log_fields("connect", "result", "subscribe_error");
  publish_state();  // state（online）→ BOOT の順（BOOT は tick の最後に events_ から送る）
}

void Node::publish_state() {
  (void)publisher_.publish(Kind::State, [this](infra::JsonWriter& w) {
    state_.write_payload(w, true, commands_.mode());
  });
}

void Node::handle_inbound(std::uint64_t now) {
  for (std::size_t n = 0; n < kMaxInboundPerTick && link_.poll_message(inbound_); ++n) {
    if (inbound_.topic.view() != topics_.cmd()) {
      ++wrong_topic_;
      continue;
    }
    infra::JsonWriter w(log_buf_);
    w.begin_object().key("topic").string(inbound_.topic.view()).key("bytes").uinteger(inbound_.payload.size());
    w.end_object();
    if (w.ok()) log_.write("rx", w.view());

    const CommandOutcome outcome = commands_.handle(inbound_.payload.view(), now, clock_.utc_ms());
    if (outcome.kind == CommandOutcome::Kind::Discarded) {
      ++discards_[static_cast<std::size_t>(outcome.discard)];
      log_fields("rx_discard", "reason", to_string(outcome.discard));
      continue;
    }
    apply_outcome(outcome);
  }
}

void Node::apply_outcome(const CommandOutcome& outcome) {
  // 重複した cmd_id にも、payload は前回と同じで新しいエンベロープの ack を返す（v0.2.1 #28）
  const std::string_view payload = outcome.ack_payload.view();
  (void)publisher_.publish(Kind::CmdAck, [payload](infra::JsonWriter& w) { w.raw(payload); });
  {
    infra::JsonWriter w(log_buf_);
    w.begin_object().key("duplicate").boolean(outcome.duplicate).key("payload").raw(payload).end_object();
    if (w.ok()) log_.write("ack", w.view());
  }
  post_interlock_events(outcome.interlock_changes);
  if (outcome.mode_changed_to) {
    std::array<char, 96> detail{};
    infra::JsonWriter w(detail);
    w.begin_object().key("mode").string(domain::to_string(*outcome.mode_changed_to)).end_object();
    (void)events_.post("MODE_CHANGED", Severity::Info, w.ok() ? w.view() : std::string_view{}, envelope_.now());
  }
  if (outcome.republish_state) publish_state();
}

void Node::post_interlock_events(const ControlArbiter::InterlockChanges& changes) {
  for (const auto& c : changes) {
    const bool active = c.transition == domain::PumpInterlock::Transition::Activated;
    std::array<char, 96> detail{};
    infra::JsonWriter w(detail);
    w.begin_object().key("id").string(c.id).key("rule").string("max_on_s").end_object();
    (void)events_.post(active ? "INTERLOCK_ACTIVE" : "INTERLOCK_RELEASED", active ? Severity::Warning : Severity::Info,
                       w.ok() ? w.view() : std::string_view{}, envelope_.now());
  }
}

void Node::log_fields(std::string_view event, std::string_view key, std::string_view value) {
  infra::JsonWriter w(log_buf_);
  w.begin_object().key(key).string(value).end_object();
  if (w.ok()) log_.write(event, w.view());
}

void Node::log_diag() {
  const Publisher::Counters& c = publisher_.counters();
  infra::JsonWriter w(log_buf_);
  w.begin_object();
  w.key("sent").uinteger(c.sent);
  w.key("telemetry_rate_dropped").uinteger(c.telemetry_rate_dropped);
  w.key("telemetry_busy_dropped").uinteger(c.telemetry_busy_dropped);
  w.key("retry_queued").uinteger(c.retry_queued);
  w.key("retry_overflow").uinteger(c.retry_overflow);
  w.key("retry_pending").uinteger(publisher_.retry_pending());
  w.key("encode_errors").uinteger(c.encode_errors);
  w.key("events_pending").uinteger(events_.pending());
  w.key("events_dropped").uinteger(events_.dropped());
  w.key("rx_discard").begin_object();
  for (auto r : {infra::DiscardReason::NotJson, infra::DiscardReason::BadEnvelope, infra::DiscardReason::NoCmdId}) {
    w.key(to_string(r)).uinteger(discards_[static_cast<std::size_t>(r)]);
  }
  w.end_object();
  w.key("rx_wrong_topic").uinteger(wrong_topic_);
  w.key("read_errors").uinteger(telemetry_.read_errors());
  w.key("apply_errors").uinteger(arbiter_.apply_errors());
  w.key("inflight").uinteger(link_.inflight());
  w.end_object();
  if (w.ok()) log_.write("diag", w.view());
}

}  // namespace node::app
