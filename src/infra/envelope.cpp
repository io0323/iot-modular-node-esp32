#include "infra/envelope.hpp"

#include "infra/formats.hpp"
#include "infra/ulid.hpp"

namespace node::infra {

EnvelopeFactory::EnvelopeFactory(std::string_view device_id, const IClock& clock, const ITimeSync& sync,
                                 IRandom& rng)
    : device_id_(device_id), clock_(clock), sync_(sync), rng_(rng), boot_monotonic_ms_(clock.monotonic_ms()) {
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::uint32_t v = rng_.next_u32();
  std::array<char, 8> digits{};
  for (std::size_t i = 8; i > 0; --i) {
    digits[i - 1] = kHex[v & 0x0FU];
    v >>= 4U;
  }
  (void)boot_id_.assign(std::string_view(digits.data(), digits.size()));
}

EnvelopeFields EnvelopeFactory::begin(JsonWriter& w, Kind k, std::uint32_t seq, const Stamp& stamp) {
  EnvelopeFields f{};
  f.utc_ms = utc_of(stamp);
  f.msg_id = make_ulid(f.utc_ms, rng_);
  f.seq = seq;
  f.stamp = stamp;
  begin_same(w, k, f);
  return f;
}

void EnvelopeFactory::begin_same(JsonWriter& w, Kind k, const EnvelopeFields& f) const {
  const TsString ts = format_ts(f.utc_ms);
  w.begin_object();
  w.key("schema").string(schema_of(k));
  w.key("msg_id").string(f.msg_id.view());
  w.key("device_id").string(device_id_);
  w.key("seq").uinteger(f.seq);
  w.key("boot_id").string(boot_id_.view());
  w.key("ts").string(ts.view());
  w.key("ts_source").string("device");
  w.key("uptime_ms").uinteger(uptime_of(f.stamp));
  w.key("payload");
}

}  // namespace node::infra
