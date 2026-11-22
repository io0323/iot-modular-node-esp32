#include "infra/formats.hpp"

#include <array>

namespace node::infra {
namespace {

constexpr bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_digit(char c) { return c >= '0' && c <= '9'; }

constexpr bool is_crockford(char c) {
  if (is_digit(c)) return true;
  if (c < 'A' || c > 'Z') return false;
  return c != 'I' && c != 'L' && c != 'O' && c != 'U';
}

// [a-z][a-z0-9]*
constexpr bool is_word(std::string_view s) {
  if (s.empty() || !is_lower(s[0])) return false;
  for (char c : s) {
    if (!is_lower(c) && !is_digit(c)) return false;
  }
  return true;
}

// 2桁の数字を読む。範囲外なら -1
constexpr int two_digits(std::string_view s, std::size_t pos, int lo, int hi) {
  if (!is_digit(s[pos]) || !is_digit(s[pos + 1])) return -1;
  const int v = (s[pos] - '0') * 10 + (s[pos + 1] - '0');
  return (v < lo || v > hi) ? -1 : v;
}

// 1970-01-01 からの日数（Howard Hinnant の days_from_civil）
constexpr std::int64_t days_from_civil(std::int64_t y, std::int64_t m, std::int64_t d) {
  y -= m <= 2 ? 1 : 0;
  const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
  const std::int64_t yoe = y - era * 400;
  const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

struct Civil {
  std::int64_t y;
  std::int64_t m;
  std::int64_t d;
};

// days_from_civil の逆
constexpr Civil civil_from_days(std::int64_t z) {
  z += 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::int64_t doe = z - era * 146097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t y = yoe + era * 400;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const std::int64_t m = mp + (mp < 10 ? 3 : -9);
  return Civil{y + (m <= 2 ? 1 : 0), m, d};
}

void put_digits(TsString& out, std::int64_t v, int width) {
  std::array<char, 4> tmp{};
  for (int i = width - 1; i >= 0; --i) {
    tmp[static_cast<std::size_t>(i)] = static_cast<char>('0' + static_cast<int>(v % 10));
    v /= 10;
  }
  for (int i = 0; i < width; ++i) (void)out.push_back(tmp[static_cast<std::size_t>(i)]);
}

}  // namespace

bool is_ulid(std::string_view s) {
  if (s.size() != 26 || s[0] < '0' || s[0] > '7') return false;
  for (char c : s) {
    if (!is_crockford(c)) return false;
  }
  return true;
}

bool is_device_id(std::string_view s) {
  // ^[a-z][a-z0-9]*(-[a-z][a-z0-9]*)?-[0-9]{2,}$
  const std::size_t last = s.rfind('-');
  if (last == std::string_view::npos) return false;
  const std::string_view number = s.substr(last + 1);
  if (number.size() < 2) return false;
  for (char c : number) {
    if (!is_digit(c)) return false;
  }
  const std::string_view head = s.substr(0, last);
  const std::size_t dash = head.find('-');
  if (dash == std::string_view::npos) return is_word(head);
  return is_word(head.substr(0, dash)) && is_word(head.substr(dash + 1));
}

bool is_snake_id(std::string_view s) {
  // ^[a-z][a-z0-9]*(_[a-z0-9]+)*$
  if (s.empty() || !is_lower(s[0]) || s.back() == '_') return false;
  char prev = s[0];
  for (char c : s.substr(1)) {
    if (c == '_') {
      if (prev == '_') return false;
    } else if (!is_lower(c) && !is_digit(c)) {
      return false;
    }
    prev = c;
  }
  return true;
}

bool is_boot_id(std::string_view s) {
  if (s.size() != 8) return false;
  for (char c : s) {
    if (!is_digit(c) && (c < 'a' || c > 'f')) return false;
  }
  return true;
}

std::optional<std::int64_t> parse_ts(std::string_view s) {
  // YYYY-MM-DDTHH:MM:SS.mmmZ
  if (s.size() != 24) return std::nullopt;
  if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != '.' ||
      s[23] != 'Z') {
    return std::nullopt;
  }
  for (std::size_t i : {0U, 1U, 2U, 3U, 20U, 21U, 22U}) {
    if (!is_digit(s[i])) return std::nullopt;
  }
  const int month = two_digits(s, 5, 1, 12);
  const int day = two_digits(s, 8, 1, 31);
  const int hour = two_digits(s, 11, 0, 23);
  const int minute = two_digits(s, 14, 0, 59);
  const int second = two_digits(s, 17, 0, 59);
  if (month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0) return std::nullopt;
  const std::int64_t year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
  const std::int64_t millis = (s[20] - '0') * 100 + (s[21] - '0') * 10 + (s[22] - '0');
  const std::int64_t days = days_from_civil(year, month, day);
  return ((days * 24 + hour) * 60 + minute) * 60000 + second * 1000 + millis;
}

TsString format_ts(std::int64_t utc_ms) {
  if (utc_ms < 0) utc_ms = 0;
  const std::int64_t days = utc_ms / 86400000;
  std::int64_t rem = utc_ms % 86400000;
  const Civil c = civil_from_days(days);
  TsString out;
  put_digits(out, c.y, 4);
  (void)out.push_back('-');
  put_digits(out, c.m, 2);
  (void)out.push_back('-');
  put_digits(out, c.d, 2);
  (void)out.push_back('T');
  put_digits(out, rem / 3600000, 2);
  rem %= 3600000;
  (void)out.push_back(':');
  put_digits(out, rem / 60000, 2);
  rem %= 60000;
  (void)out.push_back(':');
  put_digits(out, rem / 1000, 2);
  (void)out.push_back('.');
  put_digits(out, rem % 1000, 3);
  (void)out.push_back('Z');
  return out;
}

}  // namespace node::infra
