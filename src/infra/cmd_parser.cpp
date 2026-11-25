#include "infra/cmd_parser.hpp"

#include <algorithm>
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

constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }

/// 整数の後ろが、なにもないか「.」と0だけ（"1.00" も整数とみなす）
constexpr bool zero_fraction(std::string_view rest) {
  if (rest.empty()) {
    return true;
  }
  return rest[0] == '.' && std::ranges::all_of(rest.substr(1), [](char c) { return c == '0'; });
}

/// 先頭から続く数字を読む（k に数字の後ろの位置を入れる）。int64_t を超えたら std::nullopt
std::optional<std::int64_t> leading_digits(std::string_view s, std::size_t& k) {
  std::int64_t v = 0;
  for (k = 0; k < s.size() && is_digit(s[k]); ++k) {
    if (v > (INT64_MAX - (s[k] - '0')) / 10) {
      return std::nullopt;
    }
    v = v * 10 + (s[k] - '0');
  }
  return v;
}

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

  /// i の部分木の次のトークンの位置（再帰しない。残りのトークンの数を数えながら進む）
  [[nodiscard]] int skip(int i) const {
    int pending = 1;  // この部分木で、まだ読んでいないトークンの数
    int j = i;
    while (pending > 0 && j < count_) {
      pending += size(j) - 1;
      ++j;
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
    const bool negative = !s.empty() && s[0] == '-';
    if (negative) {
      s.remove_prefix(1);
    }
    if (s.empty() || !is_digit(s[0])) {
      return std::nullopt;
    }
    std::size_t k = 0;
    const std::optional<std::int64_t> v = leading_digits(s, k);
    if (!v || !zero_fraction(s.substr(k))) {
      return std::nullopt;
    }
    return negative ? -*v : *v;
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

/// i が 0〜4294967295 の整数
bool in_uint32(const Tokens& t, int i) {
  const auto v = t.integer(i);
  return v && *v >= 0 && *v <= kUint32Max;
}

/// i がエスケープのない文字列で、pred を満たす（i が -1（キーがない）なら false）
template <typename Pred>
bool string_ok(const Tokens& t, int i, Pred pred) {
  if (i < 0) {
    return false;
  }
  const auto s = t.plain_string(i);
  return s && pred(*s);
}

bool ts_ok(std::string_view s) { return parse_ts(s).has_value(); }
bool ts_source_ok(std::string_view s) { return s == "device" || s == "gateway"; }
bool schema_ok(std::string_view s) { return s == "iot.v1.cmd"; }

/// エンベロープの数値と payload（uptime_ms は任意）
bool envelope_numbers_ok(const Tokens& t) {
  const int seq = find_key(t, 0, "seq");
  const int payload = find_key(t, 0, "payload");
  const int uptime = find_key(t, 0, "uptime_ms");
  return seq >= 0 && payload >= 0 && in_uint32(t, seq) && t.is_object(payload) && (uptime < 0 || in_uint32(t, uptime));
}

bool envelope_ok(const Tokens& t) {
  // 必須のキーがない・型が違う・書式が違うものは捨てる（判定の順は結果に関係しない）
  return string_ok(t, find_key(t, 0, "schema"), schema_ok) && string_ok(t, find_key(t, 0, "msg_id"), is_ulid) &&
         string_ok(t, find_key(t, 0, "device_id"), is_device_id) && string_ok(t, find_key(t, 0, "boot_id"), is_boot_id) &&
         string_ok(t, find_key(t, 0, "ts"), ts_ok) && string_ok(t, find_key(t, 0, "ts_source"), ts_source_ok) &&
         envelope_numbers_ok(t);
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

constexpr FieldState state_of(bool ok) { return ok ? FieldState::Ok : FieldState::Invalid; }

/// 整数の項目（あれば state と値を入れる）
void read_integer(const Tokens& t, int obj, std::string_view key, FieldState& state, std::int64_t& value) {
  if (const int i = find_key(t, obj, key); i >= 0) {
    const auto v = t.integer(i);
    state = state_of(v.has_value());
    value = v.value_or(0);
  }
}

void read_args(const Tokens& t, int args, CmdRequest& req) {
  if (!t.is_object(args)) {
    req.args_state = FieldState::Invalid;
    return;
  }
  req.args_state = FieldState::Ok;
  if (const int id = find_key(t, args, "id"); id >= 0) {
    req.id_state = state_of(string_ok(t, id, [&req](std::string_view s) { return is_snake_id(s) && req.id.assign(s); }));
  }
  read_integer(t, args, "level", req.level_state, req.level);
  if (const int mode = find_key(t, args, "mode"); mode >= 0) {
    req.mode_state = state_of(string_ok(t, mode, [&req](std::string_view s) { return !s.empty() && req.mode.assign(s); }));
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

void read_op_and_priority(const Tokens& t, int payload, CmdRequest& req) {
  if (const int op = find_key(t, payload, "op"); op >= 0) {
    const auto s = t.plain_string(op);
    req.op_state = state_of(s.has_value());
    req.op = s ? op_of(*s) : OpKind::Unknown;
  }
  if (const int prio = find_key(t, payload, "priority"); prio >= 0) {
    const auto s = t.plain_string(prio);
    const auto p = s ? domain::parse_priority(*s) : std::nullopt;
    req.priority_state = state_of(p.has_value());
    req.priority = p.value_or(domain::Priority::Auto);
  }
}

void read_payload(const Tokens& t, int payload, CmdRequest& req) {
  read_op_and_priority(t, payload, req);
  if (const int args = find_key(t, payload, "args"); args >= 0) {
    read_args(t, args, req);
  }
  read_integer(t, payload, "lease_s", req.lease_state, req.lease_s);
  if (const int exp = find_key(t, payload, "expires_at"); exp >= 0) {
    const auto s = t.plain_string(exp);
    const auto ms = s ? parse_ts(*s) : std::nullopt;
    req.expires_state = state_of(ms.has_value());
    req.expires_at_ms = ms.value_or(0);
  }
  if (const int origin = find_key(t, payload, "origin"); origin >= 0) {
    req.origin_state = state_of(origin_ok(t, origin));
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
