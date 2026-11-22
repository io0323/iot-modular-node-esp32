#include "infra/ulid.hpp"

#include <string_view>

namespace node::infra {
namespace {
constexpr std::string_view kCrockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::uint64_t kTimeMask = (std::uint64_t{1} << 48U) - 1U;
}  // namespace

UlidString encode_ulid(std::uint64_t utc_ms, const std::array<std::uint8_t, 10>& random) {
  std::array<char, 26> out{};
  // 時刻部：48ビットを10文字（50ビット）に。先頭の2ビットは0なので先頭の文字は 0–7
  std::uint64_t t = utc_ms & kTimeMask;
  for (std::size_t i = 10; i > 0; --i) {
    out[i - 1] = kCrockford[static_cast<std::size_t>(t & 31U)];
    t >>= 5U;
  }
  // 乱数部：80ビットを16文字に。上位のビットから5ビットずつ取り出す
  std::uint32_t acc = 0;
  unsigned bits = 0;
  std::size_t pos = 10;
  for (std::uint8_t byte : random) {
    acc = (acc << 8U) | byte;
    bits += 8U;
    while (bits >= 5U) {
      bits -= 5U;
      out[pos++] = kCrockford[static_cast<std::size_t>((acc >> bits) & 31U)];
    }
  }
  UlidString s;
  (void)s.assign(std::string_view(out.data(), out.size()));
  return s;
}

UlidString make_ulid(std::int64_t utc_ms, IRandom& rng) {
  std::array<std::uint8_t, 10> random{};
  rng.fill(random);
  return encode_ulid(static_cast<std::uint64_t>(utc_ms < 0 ? 0 : utc_ms), random);
}

}  // namespace node::infra
