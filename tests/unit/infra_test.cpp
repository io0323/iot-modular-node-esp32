#include <gtest/gtest.h>

#include <array>
#include <regex>
#include <set>
#include <string>

#include "infra/envelope.hpp"
#include "infra/formats.hpp"
#include "infra/json_writer.hpp"
#include "infra/rate_limiter.hpp"
#include "infra/topic.hpp"
#include "infra/ulid.hpp"
#include "support/fakes.hpp"

namespace node::infra {
namespace {

// ---- JsonWriter

TEST(JsonWriter, WritesNestedStructureWithCommas) {
  std::array<char, 256> buf{};
  JsonWriter w(buf);
  w.begin_object();
  w.key("a").integer(-12);
  w.key("b").begin_array().uinteger(1).boolean(true).null().end_array();
  w.key("c").begin_object().key("d").string("x").end_object();
  w.key("e").raw(R"({"z":1})");
  w.end_object();
  ASSERT_TRUE(w.ok());
  EXPECT_EQ(w.view(), R"({"a":-12,"b":[1,true,null],"c":{"d":"x"},"e":{"z":1}})");
}

TEST(JsonWriter, EscapesStrings) {
  std::array<char, 128> buf{};
  JsonWriter w(buf);
  w.string(std::string_view("q\"b\\n\n\t\x01\xe6\x97\xa5", 11));
  ASSERT_TRUE(w.ok());
  EXPECT_EQ(w.view(), "\"q\\\"b\\\\n\\n\\t\\u0001\xe6\x97\xa5\"");
}

TEST(JsonWriter, FixedMilliUsesUpToThreeDecimals) {
  std::array<char, 128> buf{};
  JsonWriter w(buf);
  w.begin_array().fixed_milli(1013250).fixed_milli(25000).fixed_milli(-1500).fixed_milli(7).fixed_milli(0).end_array();
  ASSERT_TRUE(w.ok());
  EXPECT_EQ(w.view(), "[1013.25,25.0,-1.5,0.007,0.0]");
}

TEST(JsonWriter, ReportsBufferShortage) {
  std::array<char, 8> buf{};
  JsonWriter w(buf);
  w.begin_object().key("long_key").string("value").end_object();
  EXPECT_FALSE(w.ok());
}

TEST(JsonWriter, ReportsUnbalancedNesting) {
  std::array<char, 16> buf{};
  JsonWriter w(buf);
  w.begin_object();
  EXPECT_FALSE(w.ok());
  w.end_object();
  EXPECT_TRUE(w.ok());
  w.end_object();
  EXPECT_FALSE(w.ok());
}

TEST(JsonWriter, Int64Extremes) {
  std::array<char, 64> buf{};
  JsonWriter w(buf);
  w.begin_array().integer(INT64_MIN).uinteger(UINT64_MAX).end_array();
  ASSERT_TRUE(w.ok());
  EXPECT_EQ(w.view(), "[-9223372036854775808,18446744073709551615]");
}

// ---- 形の検査と ts

TEST(Formats, Validators) {
  EXPECT_TRUE(is_ulid("01JD3Q8Z5K7M2N4P6R8T0V2X4Y"));
  EXPECT_FALSE(is_ulid("01JD3Q8Z5K7M2N4P6R8T0V2XIL"));  // I・L は Crockford にない
  EXPECT_FALSE(is_ulid("81JD3Q8Z5K7M2N4P6R8T0V2X4Y"));  // 先頭は 0–7
  EXPECT_FALSE(is_ulid("01JD3Q8Z5K7M2N4P6R8T0V2X4"));
  EXPECT_TRUE(is_device_id("mcu-cpp-01"));
  EXPECT_TRUE(is_device_id("gw-01"));
  EXPECT_FALSE(is_device_id("MCU_CPP_01"));
  EXPECT_FALSE(is_device_id("mcu-cpp-1"));
  EXPECT_FALSE(is_device_id("a-b-c-01"));
  EXPECT_TRUE(is_snake_id("fan_main"));
  EXPECT_TRUE(is_snake_id("temp2"));
  EXPECT_FALSE(is_snake_id("Fan"));
  EXPECT_FALSE(is_snake_id("fan__main"));
  EXPECT_FALSE(is_snake_id("fan_"));
  EXPECT_FALSE(is_snake_id("_fan"));
  EXPECT_TRUE(is_boot_id("7f3a9c01"));
  EXPECT_FALSE(is_boot_id("7F3A9C01"));
  EXPECT_FALSE(is_boot_id("7f3a9c0"));
}

TEST(Formats, TsRoundTripAndShape) {
  const auto ms = parse_ts("2026-11-20T06:00:12.345Z");
  ASSERT_TRUE(ms.has_value());
  EXPECT_EQ(*ms, 1795154412345);
  EXPECT_EQ(format_ts(*ms).view(), "2026-11-20T06:00:12.345Z");
  EXPECT_EQ(format_ts(0).view(), "1970-01-01T00:00:00.000Z");
  EXPECT_EQ(format_ts(951782400000).view(), "2000-02-29T00:00:00.000Z");  // うるう日
  const std::regex shape(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$)");
  EXPECT_TRUE(std::regex_match(std::string(format_ts(1795154412345 + 86399999).view()), shape));
}

TEST(Formats, RejectsBadTs) {
  EXPECT_FALSE(parse_ts("2026-11-20T06:00:12Z").has_value());           // ミリ秒なし
  EXPECT_FALSE(parse_ts("2026-11-20T15:00:12.345+09:00").has_value());  // UTC でない
  EXPECT_FALSE(parse_ts("2026-11-20 06:10:12").has_value());
  EXPECT_FALSE(parse_ts("2026-13-20T06:00:12.345Z").has_value());
  EXPECT_FALSE(parse_ts("2026-11-20T24:00:12.345Z").has_value());
}

// ---- ULID

TEST(Ulid, EncodesTimeAndRandomInCrockford) {
  // 時刻部 0x01563E3AB5D3（ulid の仕様書の例）＋乱数部すべて0
  const std::array<std::uint8_t, 10> zero{};
  const UlidString u = encode_ulid(0x01563E3AB5D3ULL, zero);
  EXPECT_EQ(u.view(), "01ARZ3NDEK0000000000000000");
  const std::array<std::uint8_t, 10> ones{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  EXPECT_EQ(encode_ulid(0, ones).view(), "0000000000ZZZZZZZZZZZZZZZZ");
}

TEST(Ulid, MadeUlidsAreValidAndDistinct) {
  test::FakeRandom rng;
  std::set<std::string> seen;
  for (int i = 0; i < 100; ++i) {
    const UlidString u = make_ulid(1795154412345, rng);
    ASSERT_EQ(u.size(), 26U);
    ASSERT_TRUE(is_ulid(u.view())) << u.view();
    seen.emplace(u.view());
  }
  EXPECT_EQ(seen.size(), 100U);
  // 時刻部（先頭10文字）は乱数によらず時刻だけで決まる
  const std::array<std::uint8_t, 10> zero{};
  EXPECT_EQ(make_ulid(1795154412345, rng).view().substr(0, 10),
            encode_ulid(1795154412345, zero).view().substr(0, 10));
}

// ---- エンベロープ

TEST(Envelope, BootIdIsEightLowerHex) {
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeRandom rng;
  EnvelopeFactory f("mcu-cpp-01", clock, sync, rng);
  EXPECT_TRUE(std::regex_match(std::string(f.boot_id()), std::regex("^[0-9a-f]{8}$")));
}

TEST(Envelope, SeqPerKindStartsAtZeroAndWraps) {
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeRandom rng;
  EnvelopeFactory f("mcu-cpp-01", clock, sync, rng);
  EXPECT_EQ(f.peek_seq(Kind::Telemetry), 0U);
  f.commit_seq(Kind::Telemetry);
  f.commit_seq(Kind::Telemetry);
  EXPECT_EQ(f.peek_seq(Kind::Telemetry), 2U);
  EXPECT_EQ(f.peek_seq(Kind::State), 0U);  // kind ごとに別
  for (int i = 0; i < 3; ++i) f.commit_seq(Kind::Event);
  EXPECT_EQ(f.peek_seq(Kind::Event), 3U);
}

TEST(Envelope, SeqWrapsAtUint32) {
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeRandom rng;
  EnvelopeFactory f("mcu-cpp-01", clock, sync, rng);
  f.set_seq_for_test(Kind::Telemetry, 0xFFFFFFFFU);
  f.commit_seq(Kind::Telemetry);
  EXPECT_EQ(f.peek_seq(Kind::Telemetry), 0U);
}

TEST(Envelope, WritesAllFieldsInShape) {
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeRandom rng;
  EnvelopeFactory f("mcu-cpp-01", clock, sync, rng);
  clock.advance(250);
  std::array<char, 512> buf{};
  JsonWriter w(buf);
  f.begin(w, Kind::CmdAck, 7, f.now());
  w.begin_object().end_object();
  w.end_object();
  ASSERT_TRUE(w.ok());
  const std::string m(w.view());
  EXPECT_EQ(test::json_value(m, "schema"), "iot.v1.cmd_ack");
  EXPECT_EQ(test::json_value(m, "device_id"), "mcu-cpp-01");
  EXPECT_EQ(test::json_value(m, "seq"), "7");
  EXPECT_EQ(test::json_value(m, "boot_id"), std::string(f.boot_id()));
  // FakeClock の単調時計は 1000ms から始まり、FakeTimeSync で単調時計 0 が 06:00:12.345Z
  EXPECT_EQ(test::json_value(m, "ts"), "2026-11-20T06:00:13.595Z");
  EXPECT_EQ(test::json_value(m, "ts_source"), "device");
  EXPECT_EQ(test::json_value(m, "uptime_ms"), "250");
  EXPECT_TRUE(is_ulid(test::json_value(m, "msg_id").value_or("")));
  EXPECT_EQ(test::json_value(m, "payload"), "{}");
}

TEST(Envelope, TsIsMonotonicPlusOffsetAndUptimeIsAtStamp) {
  test::FakeClock clock;
  test::FakeTimeSync sync;
  test::FakeRandom rng;
  EnvelopeFactory f("mcu-cpp-01", clock, sync, rng);
  clock.advance(500);
  const Stamp s = f.now();  // 発生時刻（単調時計 1500ms）
  clock.advance(10000);     // 送るのは 10 秒後
  sync.set_offset(test::FakeTimeSync::kSampleTs + 60000);  // その間に同期で1分ずれを直した
  EXPECT_EQ(f.utc_of(s), test::FakeTimeSync::kSampleTs + 1500 + 60000);
  EXPECT_EQ(f.uptime_of(s), 500U);
  std::array<char, 512> buf{};
  JsonWriter w(buf);
  f.begin(w, Kind::Event, 3, s);
  w.begin_object().end_object();
  w.end_object();
  ASSERT_TRUE(w.ok());
  EXPECT_EQ(test::json_value(w.view(), "ts"), "2026-11-20T06:01:13.845Z");
  EXPECT_EQ(test::json_value(w.view(), "uptime_ms"), "500");
}

// ---- トピック

TEST(Topics, BuildsTopicsAndQos) {
  const Topics t("home", "mcu-cpp-01");
  ASSERT_TRUE(t.ok());
  EXPECT_EQ(t.of(Kind::Telemetry), "iot/v1/home/mcu-cpp-01/telemetry");
  EXPECT_EQ(t.of(Kind::Event), "iot/v1/home/mcu-cpp-01/event");
  EXPECT_EQ(t.of(Kind::State), "iot/v1/home/mcu-cpp-01/state");
  EXPECT_EQ(t.of(Kind::CmdAck), "iot/v1/home/mcu-cpp-01/cmd/ack");
  EXPECT_EQ(t.cmd(), "iot/v1/home/mcu-cpp-01/cmd");
  EXPECT_EQ(Topics::qos_of(Kind::Telemetry), Qos::AtMostOnce);
  EXPECT_EQ(Topics::qos_of(Kind::CmdAck), Qos::AtLeastOnce);
  EXPECT_TRUE(Topics::retain_of(Kind::State));
  EXPECT_FALSE(Topics::retain_of(Kind::Event));
  EXPECT_FALSE(Topics(std::string(100, 'x'), "mcu-cpp-01").ok());
}

// ---- 送信量の上限

TEST(RateLimiter, Burst50ThenTenPerSecond) {
  RateLimiter r;
  int taken = 0;
  for (int i = 0; i < 60; ++i) taken += r.try_take(Kind::Telemetry, 1000) ? 1 : 0;
  EXPECT_EQ(taken, 50);
  EXPECT_FALSE(r.try_take(Kind::Telemetry, 1000));
  EXPECT_TRUE(r.try_take(Kind::State, 1000));  // kind ごとに別
  EXPECT_FALSE(r.try_take(Kind::Telemetry, 1099));
  EXPECT_TRUE(r.try_take(Kind::Telemetry, 1100));  // 100ms で1件分
  int later = 0;
  for (int i = 0; i < 20; ++i) later += r.try_take(Kind::Telemetry, 2100) ? 1 : 0;
  EXPECT_EQ(later, 10);  // 1秒で10件分
}

}  // namespace
}  // namespace node::infra
