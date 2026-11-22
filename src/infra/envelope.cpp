#include "infra/envelope.hpp"

#include "infra/formats.hpp"
#include "infra/ulid.hpp"

namespace node::infra {

EnvelopeFactory::EnvelopeFactory(std::string_view device_id, const IClock& clock, IRandom& rng)
    : device_id_(device_id), clock_(clock), rng_(rng), boot_monotonic_ms_(clock.monotonic_ms()) {
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::uint32_t v = rng_.next_u32();
  std::array<char, 8> digits{};
  for (std::size_t i = 8; i > 0; --i) {
    digits[i - 1] = kHex[v & 0x0FU];
    v >>= 4U;
  }
  (void)boot_id_.assign(std::string_view(digits.data(), digits.size()));
}

Stamp EnvelopeFactory::now() const {
  // uptime_ms は uint32 なので約49日で一周する
  const std::uint64_t uptime = clock_.monotonic_ms() - boot_monotonic_ms_;
  return Stamp{clock_.utc_ms(), static_cast<std::uint32_t>(uptime & 0xFFFFFFFFU)};
}

void EnvelopeFactory::begin(JsonWriter& w, Kind k, std::uint32_t seq, const Stamp& stamp) {
  const UlidString msg_id = make_ulid(stamp.utc_ms, rng_);
  const TsString ts = format_ts(stamp.utc_ms);
  w.begin_object();
  w.key("schema").string(schema_of(k));
  w.key("msg_id").string(msg_id.view());
  w.key("device_id").string(device_id_);
  w.key("seq").uinteger(seq);
  w.key("boot_id").string(boot_id_.view());
  w.key("ts").string(ts.view());
  w.key("ts_source").string("device");
  w.key("uptime_ms").uinteger(stamp.uptime_ms);
  w.key("payload");
}

}  // namespace node::infra
