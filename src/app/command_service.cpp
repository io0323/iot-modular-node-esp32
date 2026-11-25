#include "app/command_service.hpp"

#include <array>

#include "domain/lease.hpp"
#include "infra/json_writer.hpp"

namespace node::app {

using infra::FieldState;
using infra::OpKind;

namespace {

constexpr bool invalid(FieldState s) { return s == FieldState::Invalid; }

/// ack の値（重複排除で覚える。#116）
domain::AckRecord record_of(const std::optional<RejectReason>& reject, bool output,
                            const ControlArbiter::CommandResult& r) {
  domain::AckRecord a{};
  a.rejected = reject.has_value();
  a.reason = reject ? static_cast<std::uint8_t>(*reject) : std::uint8_t{0};
  a.output = output;
  a.limited = r.limited;
  a.applied = r.applied;
  return a;
}

/// ack の payload を値から書く（初めての ack も、重複した cmd_id への ack も同じ関数で書くので、payload は同じになる）
void write_ack(AckPayload& out, std::string_view cmd_id, const domain::AckRecord& a) {
  std::array<char, kAckPayloadMax> buf{};
  infra::JsonWriter w(buf);
  w.begin_object();
  w.key("cmd_id").string(cmd_id);
  if (a.rejected) {
    w.key("status").string("rejected");
    w.key("reason").string(to_string(static_cast<RejectReason>(a.reason)));
  } else {
    w.key("status").string("done");
    if (a.output) {
      w.key("limited").boolean(a.limited);
      w.key("applied").begin_object().key("level").uinteger(a.applied).end_object();
    }
    w.key("reason").null();
  }
  w.end_object();
  (void)out.assign(w.ok() ? w.view() : std::string_view{});
}

}  // namespace

CommandOutcome CommandService::handle(std::string_view json, std::uint64_t now_ms, std::int64_t now_utc_ms) {
  CommandOutcome out{};
  const infra::ParsedCmd parsed = infra::parse_cmd(json);
  if (!parsed.ok) {
    out.kind = CommandOutcome::Kind::Discarded;
    out.discard = parsed.discard;
    return out;
  }
  const infra::CmdRequest& req = parsed.request;
  out.kind = CommandOutcome::Kind::Ack;

  if (const auto cached = cache_.find(req.cmd_id.view(), now_ms)) {
    out.duplicate = true;
    write_ack(out.ack_payload, req.cmd_id.view(), *cached);
    return out;
  }

  const Exec e = execute(req, now_ms, now_utc_ms, out);
  const domain::AckRecord ack = record_of(e.reject, e.output, e.result);
  write_ack(out.ack_payload, req.cmd_id.view(), ack);
  (void)cache_.insert(req.cmd_id.view(), ack, now_ms);
  return out;
}

std::optional<RejectReason> CommandService::precheck(const infra::CmdRequest& req, std::int64_t now_utc_ms) {
  if (req.priority_state != FieldState::Ok || req.op_state != FieldState::Ok) {
    return RejectReason::InvalidArg;
  }
  if (req.op == OpKind::Unknown || req.op == OpKind::SetParam) {
    return RejectReason::UnknownOp;
  }
  if (invalid(req.origin_state) || invalid(req.args_state) || invalid(req.expires_state)) {
    return RejectReason::InvalidArg;
  }
  if (req.expires_state == FieldState::Ok && now_utc_ms > req.expires_at_ms) {
    return RejectReason::Expired;
  }
  return std::nullopt;
}

CommandService::Exec CommandService::execute(const infra::CmdRequest& req, std::uint64_t now_ms,
                                             std::int64_t now_utc_ms, CommandOutcome& out) {
  Exec e{};
  if (const std::optional<RejectReason> r = precheck(req, now_utc_ms)) {
    e.reject = r;
    return e;
  }
  switch (req.op) {
    case OpKind::SetActuator: return set_actuator(req, now_ms, out);
    case OpKind::Release: return release(req, now_ms, out);
    case OpKind::SetMode: return set_mode(req, now_ms, out);
    case OpKind::GetStatus:
      out.republish_state = true;
      return e;
    case OpKind::SetParam:
    case OpKind::Unknown: break;
  }
  e.reject = RejectReason::UnknownOp;
  return e;
}

CommandService::Exec CommandService::set_actuator(const infra::CmdRequest& req, std::uint64_t now_ms,
                                                  CommandOutcome& out) {
  Exec e{};
  const bool level_ok = req.level_state == FieldState::Ok && req.level >= 0 && req.level <= domain::kLevelMax;
  // lease_s は set_actuator だけが持つ。付いていれば 1〜600 の整数であること（v0.2.1 #4・#13）
  std::optional<std::int64_t> lease_in{};
  if (req.lease_state == FieldState::Ok) {
    lease_in = req.lease_s;
  }
  const std::optional<std::uint32_t> lease_s =
      invalid(req.lease_state) ? std::nullopt : domain::Lease::resolve(lease_in);
  if (req.id_state != FieldState::Ok || !level_ok || !lease_s) {
    e.reject = RejectReason::InvalidArg;
    return e;
  }
  const auto index = arbiter_.find(req.id.view());
  if (!index) {
    e.reject = RejectReason::UnknownTarget;
    return e;
  }
  // 禁止中（土壌水分センサーが fault・stale の間のポンプなど）は rejected（unsafe）。リースも登録しない
  if (arbiter_.rejects_commands(*index, now_ms)) {
    e.reject = RejectReason::Unsafe;
    return e;
  }
  e.output = true;
  e.result = arbiter_.set_command(*index, static_cast<domain::Level>(req.level), req.priority, *lease_s, now_ms,
                                  out.interlock_changes);
  return e;
}

CommandService::Exec CommandService::release(const infra::CmdRequest& req, std::uint64_t now_ms,
                                             CommandOutcome& out) {
  Exec e{};
  if (req.id_state != FieldState::Ok) {
    e.reject = RejectReason::InvalidArg;
    return e;
  }
  const auto index = arbiter_.find(req.id.view());
  if (!index) {
    e.reject = RejectReason::UnknownTarget;
    return e;
  }
  arbiter_.release(*index, now_ms, out.interlock_changes);
  return e;
}

CommandService::Exec CommandService::set_mode(const infra::CmdRequest& req, std::uint64_t now_ms,
                                              CommandOutcome& out) {
  Exec e{};
  const auto mode = req.mode_state == FieldState::Ok ? domain::parse_mode(req.mode.view()) : std::nullopt;
  if (!mode) {
    e.reject = RejectReason::InvalidArg;
    return e;
  }
  // 同じモードへの set_mode は done だけ返し、MODE_CHANGED も state の出し直しもしない（#18）
  out.mode_change = modes_.request(*mode, now_ms);
  return e;
}

}  // namespace node::app
