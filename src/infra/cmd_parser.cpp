#include "infra/cmd_parser.hpp"

#include <array>
#include <cstddef>
#include <optional>

#include "infra/formats.hpp"

#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"

namespace node::infra {
namespace {

constexpr std::size_t kMaxTokens = 128;
constexpr std::int64_t kUint32Max = 4294967295;

/// トークンの並びと元の文字列。
class Tokens {
 public:
  Tokens(std::string_view json, const jsmntok_t* toks, int count) : json_(json), toks_(toks), count_(count) {}

  [[nodiscard]] int count() const { return count_; }
  [[nodiscard]] const jsmntok_t& at(int i) const { return toks_[static_cast<std::size_t>(i)]; }
  [[nodiscard]] jsmntype_t type(int i) const { return at(i).type; }
  [[nodiscard]] int size(int i) const { return at(i).size; }

  /// 文字列トークンの中身（引用符を除き、エスケープはそのまま）
  [[nodiscard]] std::string_view text(int i) const {
    const auto start = static_cast<std::size_t>(at(i).start);
    const auto end = static_cast<std::size_t>(at(i).end);
    return json_.substr(start, end - start);
  }

  /// i の部分木の次のトークンの位置
  [[nodiscard]] int skip(int i) const {
    int n = size(i);
    int j = i + 1;
    while (n-- > 0 && j < count_) {
      j = skip(j);
    }
    return j;
  }

  [[nodiscard]] bool is_string(int i) const { return type(i) == JSMN_STRING; }
  [[nodiscard]] bool is_object(int i) const { return type(i) == JSMN_OBJECT; }

  /// 文字列で、エスケープを含まない（ID・列挙値の比較に使う）
  [[nodiscard]] std::optional<std::string_view> plain_string(int i) const {
    if (!is_string(i)) {
      return std::nullopt;
    }
    const std::string_view s = text(i);
    if (s.find('\\') != std::string_view::npos) {
      return std::nullopt;
    }
    return s;
  }

  /// 整数（JSON Schema の integer：小数部がすべて0の数も整数とみなす。指数表記は扱わない）
  [[nodiscard]] std::optional<std::int64_t> integer(int i) const {
    if (type(i) != JSMN_PRIMITIVE) {
      return std::nullopt;
    }
    std::string_view s = text(i);
    bool negative = false;
    if (!s.empty() && s[0] == '-') {
      negative = true;
      s.remove_prefix(1);
    }
    if (s.empty() || s[0] < '0' || s[0] > '9') {
      return std::nullopt;
    }
    std::int64_t v = 0;
    std::size_t k = 0;
    for (; k < s.size() && s[k] >= '0' && s[k] <= '9'; ++k) {
      if (v > (INT64_MAX - (s[k] - '0')) / 10) {
        return std::nullopt;
      }
      v = v * 10 + (s[k] - '0');
    }
    if (k < s.size()) {
      if (s[k] != '.') {
        return std::nullopt;
      }
      for (++k; k < s.size(); ++k) {
        if (s[k] != '0') {
          return std::nullopt;
        }
      }
    }
    return negative ? -v : v;
  }

 private:
  std::string_view json_;
  const jsmntok_t* toks_;
  int count_;
};

/// オブジェクト obj の中からキー name の値の位置を探す。なければ -1
int find_key(const Tokens& t, int obj, std::string_view name) {
  if (!t.is_object(obj)) {
    return -1;
  }
  int j = obj + 1;
  for (int n = t.size(obj); n > 0 && j < t.count(); --n) {
    if (t.is_string(j) && t.text(j) == name && j + 1 < t.count()) {
      return j + 1;
    }
    j = t.skip(j);
  }
  return -1;
}

bool envelope_ok(const Tokens& t) {
  const int schema = find_key(t, 0, "schema");
  const int msg_id = find_key(t, 0, "msg_id");
  const int device_id = find_key(t, 0, "device_id");
  const int seq = find_key(t, 0, "seq");
  const int boot_id = find_key(t, 0, "boot_id");
  const int ts = find_key(t, 0, "ts");
  const int ts_source = find_key(t, 0, "ts_source");
  const int payload = find_key(t, 0, "payload");
  const int uptime = find_key(t, 0, "uptime_ms");
  if (schema < 0 || msg_id < 0 || device_id < 0 || seq < 0 || boot_id < 0 || ts < 0 || ts_source < 0 ||
      payload < 0) {
    return false;
  }
  const auto in_uint32 = [&](int i) {
    const auto v = t.integer(i);
    return v && *v >= 0 && *v <= kUint32Max;
  };
  const auto schema_s = t.plain_string(schema);
  const auto msg_id_s = t.plain_string(msg_id);
  const auto device_s = t.plain_string(device_id);
  const auto boot_s = t.plain_string(boot_id);
  const auto ts_s = t.plain_string(ts);
  const auto source_s = t.plain_string(ts_source);
  return schema_s && *schema_s == "iot.v1.cmd" && msg_id_s && is_ulid(*msg_id_s) && device_s &&
         is_device_id(*device_s) && in_uint32(seq) && boot_s && is_boot_id(*boot_s) && ts_s &&
         parse_ts(*ts_s).has_value() && source_s && (*source_s == "device" || *source_s == "gateway") &&
         t.is_object(payload) && (uptime < 0 || in_uint32(uptime));
}

OpKind op_of(std::string_view s) {
  if (s == "set_actuator") {
    return OpKind::SetActuator;
  }
  if (s == "release") {
    return OpKind::Release;
  }
  if (s == "set_mode") {
    return OpKind::SetMode;
  }
  if (s == "set_param") {
    return OpKind::SetParam;
  }
  if (s == "get_status") {
    return OpKind::GetStatus;
  }
  return OpKind::Unknown;
}

void read_args(const Tokens& t, int args, CmdRequest& req) {
  if (!t.is_object(args)) {
    req.args_state = FieldState::Invalid;
    return;
  }
  req.args_state = FieldState::Ok;
  if (const int id = find_key(t, args, "id"); id >= 0) {
    const auto s = t.plain_string(id);
    req.id_state = (s && is_snake_id(*s) && req.id.assign(*s)) ? FieldState::Ok : FieldState::Invalid;
  }
  if (const int level = find_key(t, args, "level"); level >= 0) {
    const auto v = t.integer(level);
    req.level_state = v ? FieldState::Ok : FieldState::Invalid;
    req.level = v.value_or(0);
  }
  if (const int mode = find_key(t, args, "mode"); mode >= 0) {
    const auto s = t.plain_string(mode);
    req.mode_state = (s && !s->empty() && req.mode.assign(*s)) ? FieldState::Ok : FieldState::Invalid;
  }
}

bool origin_ok(const Tokens& t, int origin) {
  if (!t.is_object(origin)) {
    return false;
  }
  const int kind = find_key(t, origin, "kind");
  const int ref = find_key(t, origin, "ref");
  return (kind < 0 || t.is_string(kind)) && (ref < 0 || t.is_string(ref));
}

void read_payload(const Tokens& t, int payload, CmdRequest& req) {
  if (const int op = find_key(t, payload, "op"); op >= 0) {
    const auto s = t.plain_string(op);
    req.op_state = s ? FieldState::Ok : FieldState::Invalid;
    req.op = s ? op_of(*s) : OpKind::Unknown;
  }
  if (const int prio = find_key(t, payload, "priority"); prio >= 0) {
    const auto s = t.plain_string(prio);
    const auto p = s ? domain::parse_priority(*s) : std::nullopt;
    req.priority_state = p ? FieldState::Ok : FieldState::Invalid;
    req.priority = p.value_or(domain::Priority::Auto);
  }
  if (const int args = find_key(t, payload, "args"); args >= 0) {
    read_args(t, args, req);
  }
  if (const int lease = find_key(t, payload, "lease_s"); lease >= 0) {
    const auto v = t.integer(lease);
    req.lease_state = v ? FieldState::Ok : FieldState::Invalid;
    req.lease_s = v.value_or(0);
  }
  if (const int exp = find_key(t, payload, "expires_at"); exp >= 0) {
    const auto s = t.plain_string(exp);
    const auto ms = s ? parse_ts(*s) : std::nullopt;
    req.expires_state = ms ? FieldState::Ok : FieldState::Invalid;
    req.expires_at_ms = ms.value_or(0);
  }
  if (const int origin = find_key(t, payload, "origin"); origin >= 0) {
    req.origin_state = origin_ok(t, origin) ? FieldState::Ok : FieldState::Invalid;
  }
}

}  // namespace

ParsedCmd parse_cmd(std::string_view json) {
  ParsedCmd out{};
  std::array<jsmntok_t, kMaxTokens> toks{};
  jsmn_parser parser;
  jsmn_init(&parser);
  const int n = jsmn_parse(&parser, json.data(), json.size(), toks.data(), static_cast<unsigned int>(toks.size()));
  if (n <= 0 || toks[0].type != JSMN_OBJECT) {
    out.discard = DiscardReason::NotJson;
    return out;
  }
  const Tokens t(json, toks.data(), n);
  if (!envelope_ok(t)) {
    out.discard = DiscardReason::BadEnvelope;
    return out;
  }
  const int payload = find_key(t, 0, "payload");
  const int cmd_id = find_key(t, payload, "cmd_id");
  const auto id = cmd_id >= 0 ? t.plain_string(cmd_id) : std::nullopt;
  if (!id || !is_ulid(*id)) {
    out.discard = DiscardReason::NoCmdId;
    return out;
  }
  (void)out.request.cmd_id.assign(*id);  // ULID は26文字
  read_payload(t, payload, out.request);
  out.ok = true;
  return out;
}

}  // namespace node::infra
