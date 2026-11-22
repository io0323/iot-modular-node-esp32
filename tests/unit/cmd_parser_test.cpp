#include <gtest/gtest.h>

#include <string>

#include "infra/cmd_parser.hpp"
#include "support/fakes.hpp"

namespace node::infra {
namespace {

using test::make_cmd;
using test::ulid;

TEST(CmdParser, ReadsSetActuator) {
  const auto p = parse_cmd(make_cmd(ulid(1), "set_actuator", R"({"id":"fan_main","level":800})", "auto",
                                    R"("lease_s":60,"expires_at":"2026-11-20T06:01:12.000Z","origin":{"kind":"rule","ref":"r1"})"));
  ASSERT_TRUE(p.ok);
  const CmdRequest& r = p.request;
  EXPECT_EQ(r.cmd_id.view(), ulid(1));
  EXPECT_EQ(r.op, OpKind::SetActuator);
  EXPECT_EQ(r.priority_state, FieldState::Ok);
  EXPECT_EQ(r.priority, domain::Priority::Auto);
  EXPECT_EQ(r.id_state, FieldState::Ok);
  EXPECT_EQ(r.id.view(), "fan_main");
  EXPECT_EQ(r.level_state, FieldState::Ok);
  EXPECT_EQ(r.level, 800);
  EXPECT_EQ(r.lease_state, FieldState::Ok);
  EXPECT_EQ(r.lease_s, 60);
  EXPECT_EQ(r.expires_state, FieldState::Ok);
  EXPECT_EQ(r.expires_at_ms, *parse_ts("2026-11-20T06:01:12.000Z"));
  EXPECT_EQ(r.origin_state, FieldState::Ok);
}

TEST(CmdParser, ReadsOtherOps) {
  EXPECT_EQ(parse_cmd(make_cmd(ulid(2), "release", R"({"id":"pump"})")).request.op, OpKind::Release);
  const auto m = parse_cmd(make_cmd(ulid(3), "set_mode", R"({"mode":"power_save"})"));
  EXPECT_EQ(m.request.op, OpKind::SetMode);
  EXPECT_EQ(m.request.mode.view(), "power_save");
  EXPECT_EQ(parse_cmd(make_cmd(ulid(4), "get_status", "")).request.op, OpKind::GetStatus);
  EXPECT_EQ(parse_cmd(make_cmd(ulid(5), "set_param", R"({"name":"x","value":1})")).request.op, OpKind::SetParam);
  EXPECT_EQ(parse_cmd(make_cmd(ulid(6), "toggle", "")).request.op, OpKind::Unknown);
}

TEST(CmdParser, MarksWrongTypesInvalid) {
  const auto p = parse_cmd(make_cmd(ulid(7), "set_actuator", R"({"id":"Fan Main","level":"800"})", "urgent",
                                    R"("lease_s":1.5,"expires_at":"2026-11-20 06:10:12","origin":"x")"));
  ASSERT_TRUE(p.ok);
  EXPECT_EQ(p.request.id_state, FieldState::Invalid);
  EXPECT_EQ(p.request.level_state, FieldState::Invalid);
  EXPECT_EQ(p.request.priority_state, FieldState::Invalid);
  EXPECT_EQ(p.request.lease_state, FieldState::Invalid);
  EXPECT_EQ(p.request.expires_state, FieldState::Invalid);
  EXPECT_EQ(p.request.origin_state, FieldState::Invalid);
  EXPECT_EQ(parse_cmd(make_cmd(ulid(8), "set_actuator", "[1]")).request.args_state, FieldState::Invalid);
  EXPECT_EQ(parse_cmd(make_cmd(ulid(9), "set_mode", R"({"mode":""})")).request.mode_state, FieldState::Invalid);
}

TEST(CmdParser, IntegerAcceptsZeroFractionOnly) {
  EXPECT_EQ(parse_cmd(make_cmd(ulid(10), "set_actuator", R"({"id":"pump","level":500.0})")).request.level, 500);
  EXPECT_EQ(parse_cmd(make_cmd(ulid(11), "set_actuator", R"({"id":"pump","level":-1})")).request.level, -1);
  EXPECT_EQ(parse_cmd(make_cmd(ulid(12), "set_actuator", R"({"id":"pump","level":5e2})")).request.level_state,
            FieldState::Invalid);
  EXPECT_EQ(parse_cmd(make_cmd(ulid(13), "set_actuator", R"({"id":"pump","level":99999999999999999999})"))
                .request.level_state,
            FieldState::Invalid);
}

TEST(CmdParser, DiscardsNonJson) {
  EXPECT_EQ(parse_cmd("not json").discard, DiscardReason::NotJson);
  EXPECT_EQ(parse_cmd("[1,2]").discard, DiscardReason::NotJson);
  EXPECT_EQ(parse_cmd(R"({"schema":)").discard, DiscardReason::NotJson);
  EXPECT_FALSE(parse_cmd("").ok);
}

TEST(CmdParser, DiscardsBadEnvelope) {
  std::string m = make_cmd(ulid(1), "get_status", "");
  std::string bad = m;
  bad.replace(bad.find("iot.v1.cmd"), 10, "iot.v1.cmx");
  EXPECT_EQ(parse_cmd(bad).discard, DiscardReason::BadEnvelope);
  bad = m;
  bad.replace(bad.find("7f3a9c01"), 8, "7F3A9C01");
  EXPECT_EQ(parse_cmd(bad).discard, DiscardReason::BadEnvelope);
  bad = m;
  bad.replace(bad.find("\"device\""), 8, "\"server\"");
  EXPECT_EQ(parse_cmd(bad).discard, DiscardReason::BadEnvelope);
  bad = m;
  bad.replace(bad.find("\"seq\":1"), 7, "\"seq\":4294967296");
  EXPECT_EQ(parse_cmd(bad).discard, DiscardReason::BadEnvelope);
  bad = m;
  bad.replace(bad.find("06:00:12.345Z"), 13, "06:00:12Z");
  EXPECT_EQ(parse_cmd(bad).discard, DiscardReason::BadEnvelope);
}

TEST(CmdParser, DiscardsMissingOrBadCmdId) {
  EXPECT_EQ(parse_cmd(make_cmd("not-a-ulid", "get_status", "")).discard, DiscardReason::NoCmdId);
  std::string m = make_cmd(ulid(1), "get_status", "");
  m.replace(m.find("\"cmd_id\""), 8, "\"cmd_xx\"");
  EXPECT_EQ(parse_cmd(m).discard, DiscardReason::NoCmdId);
}

TEST(CmdParser, IgnoresUnknownFields) {
  const auto p = parse_cmd(make_cmd(ulid(1), "set_actuator", R"({"id":"fan_main","level":1,"extra":[1,{"a":2}]})",
                                    "auto", R"("future":{"x":[1,2,3]})"));
  ASSERT_TRUE(p.ok);
  EXPECT_EQ(p.request.level, 1);
}

}  // namespace
}  // namespace node::infra
