#include "infra/json_writer.hpp"

namespace node::infra {

void JsonWriter::put(char c) {
  if (!ok_) {
    return;
  }
  if (len_ >= buf_.size()) {
    ok_ = false;
    return;
  }
  buf_[len_++] = c;
}

void JsonWriter::put(std::string_view s) {
  for (char const c : s) {
    put(c);
  }
}

void JsonWriter::put_uint(std::uint64_t v) {
  std::array<char, 20> tmp{};
  std::size_t n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + static_cast<int>(v % 10U));
    v /= 10U;
  } while (v != 0U);
  while (n > 0) {
    put(tmp[--n]);
  }
}

void JsonWriter::put_escaped(std::string_view s) {
  static constexpr std::string_view kHex = "0123456789abcdef";
  put('"');
  for (char const c : s) {
    const auto u = static_cast<unsigned char>(c);
    switch (c) {
      case '"': put("\\\""); break;
      case '\\': put("\\\\"); break;
      case '\b': put("\\b"); break;
      case '\f': put("\\f"); break;
      case '\n': put("\\n"); break;
      case '\r': put("\\r"); break;
      case '\t': put("\\t"); break;
      default:
        if (u < 0x20U) {
          put("\\u00");
          put(kHex[u >> 4U]);
          put(kHex[u & 0x0FU]);
        } else {
          put(c);
        }
    }
  }
  put('"');
}

void JsonWriter::before_value() {
  if (after_key_) {
    after_key_ = false;
    return;
  }
  if (depth_ > 0) {
    if (!first_[depth_]) {
      put(',');
    }
    first_[depth_] = false;
  }
}

void JsonWriter::open(char c) {
  before_value();
  if (depth_ >= kMaxDepth) {
    ok_ = false;
    return;
  }
  put(c);
  ++depth_;
  first_[depth_] = true;
}

void JsonWriter::close(char c) {
  if (depth_ == 0) {
    ok_ = false;
    return;
  }
  put(c);
  --depth_;
}

JsonWriter& JsonWriter::begin_object() {
  open('{');
  return *this;
}
JsonWriter& JsonWriter::end_object() {
  close('}');
  return *this;
}
JsonWriter& JsonWriter::begin_array() {
  open('[');
  return *this;
}
JsonWriter& JsonWriter::end_array() {
  close(']');
  return *this;
}

JsonWriter& JsonWriter::key(std::string_view k) {
  before_value();
  put_escaped(k);
  put(':');
  after_key_ = true;
  return *this;
}

JsonWriter& JsonWriter::string(std::string_view s) {
  before_value();
  put_escaped(s);
  return *this;
}

JsonWriter& JsonWriter::integer(std::int64_t v) {
  before_value();
  if (v < 0) {
    put('-');
    // INT64_MIN でもあふれないよう、符号なしで絶対値を求める
    put_uint(static_cast<std::uint64_t>(-(v + 1)) + 1U);
  } else {
    put_uint(static_cast<std::uint64_t>(v));
  }
  return *this;
}

JsonWriter& JsonWriter::uinteger(std::uint64_t v) {
  before_value();
  put_uint(v);
  return *this;
}

JsonWriter& JsonWriter::boolean(bool v) {
  before_value();
  put(v ? "true" : "false");
  return *this;
}

JsonWriter& JsonWriter::null() {
  before_value();
  put("null");
  return *this;
}

JsonWriter& JsonWriter::fixed_milli(std::int32_t milli) {
  before_value();
  std::int64_t v = milli;
  if (v < 0) {
    put('-');
    v = -v;
  }
  const auto abs = static_cast<std::uint64_t>(v);
  put_uint(abs / 1000U);
  put('.');
  std::uint64_t frac = abs % 1000U;
  if (frac == 0U) {
    put('0');
    return *this;
  }
  std::array<char, 3> digits{};
  for (std::size_t i = 3; i > 0; --i) {
    digits[i - 1] = static_cast<char>('0' + static_cast<int>(frac % 10U));
    frac /= 10U;
  }
  std::size_t n = 3;
  while (n > 1 && digits[n - 1] == '0') {
    --n;
  }
  for (std::size_t i = 0; i < n; ++i) {
    put(digits[i]);
  }
  return *this;
}

JsonWriter& JsonWriter::raw(std::string_view json) {
  before_value();
  put(json);
  return *this;
}

}  // namespace node::infra
