#include <gtest/gtest.h>

#include "support/fixed_string.hpp"
#include "support/result.hpp"
#include "support/static_vector.hpp"

namespace node::support {
namespace {

Result<int> half(int v) {
  if (v % 2 != 0) return make_error(ErrorCode::InvalidArgument, "odd");
  return v / 2;
}

TEST(Result, HoldsValueOrError) {
  const auto ok = half(4);
  ASSERT_TRUE(ok.has_value());
  EXPECT_EQ(ok.value(), 2);

  const auto ng = half(3);
  ASSERT_FALSE(ng);
  EXPECT_EQ(ng.error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(ng.error().what, "odd");
  EXPECT_EQ(ng.value_or(-1), -1);
}

TEST(Result, VoidSpecialization) {
  const Result<void> a = ok();
  EXPECT_TRUE(a.has_value());
  const Result<void> b = make_error(ErrorCode::Busy);
  EXPECT_FALSE(b);
  EXPECT_EQ(b.error().code, ErrorCode::Busy);
  EXPECT_EQ(to_string(ErrorCode::Busy), "busy");
}

TEST(StaticVector, PushUntilFullThenRefuses) {
  StaticVector<int, 3> v;
  EXPECT_TRUE(v.empty());
  EXPECT_TRUE(v.push_back(1));
  EXPECT_TRUE(v.push_back(2));
  EXPECT_TRUE(v.push_back(3));
  EXPECT_TRUE(v.full());
  EXPECT_FALSE(v.push_back(4));
  EXPECT_EQ(v.size(), 3U);
  EXPECT_EQ(v.back(), 3);
}

TEST(StaticVector, EraseShiftsLaterElements) {
  StaticVector<int, 4> v;
  for (int i : {10, 20, 30}) ASSERT_TRUE(v.push_back(i));
  v.erase_at(0);
  ASSERT_EQ(v.size(), 2U);
  EXPECT_EQ(v[0], 20);
  EXPECT_EQ(v[1], 30);
  v.erase_at(5);  // 範囲外は何もしない
  EXPECT_EQ(v.size(), 2U);
  int sum = 0;
  for (int x : v) sum += x;
  EXPECT_EQ(sum, 50);
  v.clear();
  EXPECT_TRUE(v.empty());
}

TEST(FixedString, AssignAppendAndCapacity) {
  FixedString<5> s;
  EXPECT_TRUE(s.assign("abc"));
  EXPECT_EQ(s.view(), "abc");
  EXPECT_TRUE(s.append("de"));
  EXPECT_EQ(s.view(), "abcde");
  EXPECT_FALSE(s.append("f"));  // 容量を超える追加は中身を変えない
  EXPECT_EQ(s.view(), "abcde");
  EXPECT_FALSE(s.push_back('x'));
  EXPECT_FALSE(s.assign("toolong"));  // 失敗した assign は空になる
  EXPECT_TRUE(s.empty());
  EXPECT_STREQ(s.c_str(), "");
  EXPECT_TRUE(s.assign("ok"));
  EXPECT_TRUE(s == "ok");
}

}  // namespace
}  // namespace node::support
