#include <gtest/gtest.h>

#include <string>

#include "domain/cmd_dedup_cache.hpp"
#include "domain/lease.hpp"
#include "domain/pump_interlock.hpp"
#include "domain/reading_store.hpp"
#include "domain/types.hpp"
#include "support/fakes.hpp"

namespace node::domain {
namespace {

// ---- Lease

TEST(Lease, OmittedIs60AndMaxIs600) {
  EXPECT_EQ(Lease::resolve(std::nullopt), 60U);
  EXPECT_EQ(Lease::resolve(1), 1U);
  EXPECT_EQ(Lease::resolve(600), 600U);
  EXPECT_FALSE(Lease::resolve(0).has_value());
  EXPECT_FALSE(Lease::resolve(601).has_value());
  EXPECT_FALSE(Lease::resolve(-5).has_value());
}

TEST(Lease, ExpiresExactlyAtStartPlusLease) {
  const Lease l(10'000, 3);
  EXPECT_EQ(l.deadline_ms(), 13'000U);
  EXPECT_FALSE(l.expired(12'999));
  EXPECT_TRUE(l.expired(13'000));  // ちょうどで切れる（v0.2.1 #15）
  EXPECT_TRUE(l.expired(13'001));
}

// ---- CmdDedupCache

TEST(CmdDedupCache, RemembersPayloadIncludingRejected) {
  CmdDedupCache c;
  ASSERT_TRUE(c.insert(test::ulid(1), R"({"status":"rejected","reason":"unknown_op"})", 0));
  const auto hit = c.find(test::ulid(1), 1000);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->view(), R"({"status":"rejected","reason":"unknown_op"})");
  EXPECT_FALSE(c.find(test::ulid(2), 1000).has_value());
}

TEST(CmdDedupCache, ForgetsAfterTenMinutes) {
  CmdDedupCache c;
  ASSERT_TRUE(c.insert(test::ulid(1), "{}", 5'000));
  EXPECT_TRUE(c.find(test::ulid(1), 5'000 + CmdDedupCache::kRetentionMs - 1).has_value());
  EXPECT_FALSE(c.find(test::ulid(1), 5'000 + CmdDedupCache::kRetentionMs).has_value());
}

TEST(CmdDedupCache, PushesOutOldestBeyond64) {
  CmdDedupCache c;
  for (int i = 0; i < 65; ++i) ASSERT_TRUE(c.insert(test::ulid(i), std::to_string(i), 0));
  EXPECT_EQ(c.size(), 64U);
  EXPECT_FALSE(c.find(test::ulid(0), 0).has_value());  // 最初の1件が押し出される
  EXPECT_TRUE(c.find(test::ulid(1), 0).has_value());
  EXPECT_EQ(c.find(test::ulid(64), 0)->view(), "64");
}

TEST(CmdDedupCache, RefusesTooLongInput) {
  CmdDedupCache c;
  EXPECT_FALSE(c.insert(std::string(27, 'A'), "{}", 0));
  EXPECT_FALSE(c.insert(test::ulid(1), std::string(CmdDedupCache::kPayloadMax + 1, 'x'), 0));
}

// ---- PumpInterlock

using T = PumpInterlock::Transition;

TEST(PumpInterlock, PassesRequestUntil30sThenStops) {
  PumpInterlock p(30);
  EXPECT_EQ(p.update(1000, 0).level, 1000);
  EXPECT_EQ(p.update(1000, 29'999).level, 1000);
  const auto out = p.update(1000, 30'000);
  EXPECT_EQ(out.level, 0);
  EXPECT_EQ(out.transition, T::Activated);
  EXPECT_TRUE(p.active());
  EXPECT_EQ(p.update(1000, 30'010).transition, T::None);  // 遷移は1回だけ
}

TEST(PumpInterlock, DoesNotRestartWhileRequestStaysNonZero) {
  PumpInterlock p(30);
  (void)p.update(1000, 0);
  (void)p.update(1000, 30'000);
  // 上位がリースを更新し続けても（要求が0に戻らない）、60秒を過ぎても再開しない
  const auto out = p.update(1000, 30'000 + 120'000);
  EXPECT_EQ(out.level, 0);
  EXPECT_TRUE(p.active());
}

TEST(PumpInterlock, DoesNotRestartBefore60sEvenIfRequestReturnedToZero) {
  PumpInterlock p(30);
  (void)p.update(1000, 0);
  (void)p.update(1000, 30'000);
  EXPECT_EQ(p.update(0, 31'000).level, 0);
  EXPECT_EQ(p.update(800, 89'999).level, 0);
  EXPECT_TRUE(p.active());
}

TEST(PumpInterlock, ReleasesWhenBothConditionsHold) {
  PumpInterlock p(30);
  (void)p.update(1000, 0);
  (void)p.update(1000, 30'000);
  (void)p.update(0, 40'000);  // 要求が0に戻る
  const auto out = p.update(700, 90'000);  // 停止から60秒
  EXPECT_EQ(out.transition, T::Released);
  EXPECT_EQ(out.level, 700);
  EXPECT_FALSE(p.active());
  EXPECT_EQ(p.update(700, 90'010).transition, T::None);
  // 再開後も、また連続30秒で止まる
  EXPECT_EQ(p.update(700, 120'000).transition, T::Activated);
}

TEST(PumpInterlock, ReleaseHappensWhenZeroArrivesAfter60s) {
  PumpInterlock p(30);
  (void)p.update(1000, 0);
  (void)p.update(1000, 30'000);
  EXPECT_EQ(p.update(1000, 100'000).transition, T::None);
  const auto out = p.update(0, 100'010);
  EXPECT_EQ(out.transition, T::Released);
  EXPECT_EQ(out.level, 0);
}

TEST(PumpInterlock, ZeroResetsTheContinuousTimer) {
  PumpInterlock p(30);
  (void)p.update(1000, 0);
  (void)p.update(0, 20'000);
  (void)p.update(1000, 25'000);
  EXPECT_EQ(p.update(1000, 54'999).level, 1000);  // 25秒から数えて30秒未満
  EXPECT_EQ(p.update(1000, 55'000).transition, T::Activated);
}

// ---- ReadingStore と型

TEST(ReadingStore, KeepsLatestPerChannel) {
  ReadingStore s;
  EXPECT_FALSE(s.latest("soil").has_value());
  ASSERT_TRUE(s.update({"soil", 40000, Unit::Pct, Quality::Good}, 0));
  ASSERT_TRUE(s.update({"soil", 39000, Unit::Pct, Quality::Fault}, 10));
  const auto r = s.latest("soil");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->value_milli, 39000);
  EXPECT_EQ(r->quality, Quality::Fault);
  EXPECT_EQ(s.quality("soil", 10), Quality::Fault);
}

TEST(ReadingStore, StaleAfterThreeTimesThePeriod) {
  ReadingStore s;
  ASSERT_TRUE(s.set_period("lux", 1000));
  ASSERT_TRUE(s.update({"lux", 300000, Unit::Lux, Quality::Good}, 5000));
  EXPECT_EQ(s.quality("lux", 5000), Quality::Good);
  EXPECT_EQ(s.quality("lux", 8000), Quality::Good);   // ちょうど3倍はまだ good
  EXPECT_EQ(s.quality("lux", 8001), Quality::Stale);  // 3倍を超えたら stale
  EXPECT_EQ(s.good_value("lux", 8000), 300000);
  EXPECT_FALSE(s.good_value("lux", 8001).has_value());
  // 省電力モードの周期（5倍）では、しきい値も5倍（#39）
  ASSERT_TRUE(s.set_period("lux", 5000));
  EXPECT_EQ(s.quality("lux", 20000), Quality::Good);
  EXPECT_EQ(s.quality("lux", 20001), Quality::Stale);
}

TEST(ReadingStore, NeverReadIsStale) {
  ReadingStore s;
  ASSERT_TRUE(s.set_period("soil", 10000));
  EXPECT_EQ(s.quality("soil", 0), Quality::Stale);  // 一度も good で読めていない（#36）
  EXPECT_EQ(s.quality("unknown", 0), Quality::Stale);
}

TEST(ReadingStore, FaultThenGoodRecovers) {
  ReadingStore s;
  ASSERT_TRUE(s.set_period("temp_air", 2000));
  ASSERT_TRUE(s.update({"temp_air", 25000, Unit::DegC, Quality::Good}, 0));
  ASSERT_TRUE(s.mark_fault("temp_air", 2000));
  EXPECT_EQ(s.quality("temp_air", 2000), Quality::Fault);
  EXPECT_EQ(s.latest("temp_air")->value_milli, 25000);  // 値はそのまま
  ASSERT_TRUE(s.update({"temp_air", 26000, Unit::DegC, Quality::Good}, 4000));
  EXPECT_EQ(s.quality("temp_air", 4000), Quality::Good);
  // センサーが stale を返したら stale
  ASSERT_TRUE(s.update({"temp_air", 26000, Unit::DegC, Quality::Stale}, 4010));
  EXPECT_EQ(s.quality("temp_air", 4010), Quality::Stale);
}

TEST(Types, StringsMatchCommonSpec) {
  EXPECT_EQ(to_string(Owner::Interlock), "interlock");
  EXPECT_EQ(to_string(Owner::None), "none");
  EXPECT_EQ(to_string(owner_of(Priority::Schedule)), "schedule");
  EXPECT_EQ(to_string(Unit::PctRh), "pct_rh");
  EXPECT_EQ(to_string(Mode::PowerSave), "power_save");
  EXPECT_EQ(parse_priority("safety"), Priority::Safety);
  EXPECT_FALSE(parse_priority("urgent").has_value());
  EXPECT_EQ(parse_mode("manual"), Mode::Manual);
  EXPECT_FALSE(parse_mode("eco").has_value());
}

}  // namespace
}  // namespace node::domain
