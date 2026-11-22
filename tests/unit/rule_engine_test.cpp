#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string_view>

#include "app/rule_engine.hpp"
#include "config/devices.hpp"

namespace node::app {
namespace {

using domain::Quality;
using domain::Unit;

class RuleEngineTest : public ::testing::Test {
 protected:
  domain::ReadingStore store;
  RuleEngine engine{config::kRules};
  RuleEngine::Proposals out{};

  void SetUp() override {
    ASSERT_TRUE(store.set_period("lux", 1000));
    ASSERT_TRUE(store.set_period("temp_air", 2000));
    ASSERT_TRUE(store.set_period("soil", 10000));
  }
  void set(std::string_view ch, std::int32_t milli, std::uint64_t now, Quality q = Quality::Good) {
    ASSERT_TRUE(store.update({ch, milli, Unit::Lux, q}, now));
  }
  std::optional<domain::Level> run(std::string_view actuator, std::uint64_t now, bool rules = true,
                                   bool lighting = true) {
    engine.evaluate(store, now, rules, lighting, out);
    for (const auto& p : out) {
      if (p.actuator == actuator) return p.level;
    }
    return std::nullopt;
  }
};

TEST_F(RuleEngineTest, LightOnDarkHysteresis199_200_249_250) {
  set("lux", 200000, 0);
  EXPECT_FALSE(run("led_grow", 0).has_value());  // 200 は「200未満」ではない
  set("lux", 199999, 10);
  EXPECT_EQ(run("led_grow", 10), 1000);
  set("lux", 249999, 20);
  EXPECT_EQ(run("led_grow", 20), 1000);  // 250未満の間は入ったまま
  set("lux", 250000, 30);
  EXPECT_FALSE(run("led_grow", 30).has_value());  // 250以上で出る
  set("lux", 220000, 40);
  EXPECT_FALSE(run("led_grow", 40).has_value());  // 200〜250 の間では入らない
}

TEST_F(RuleEngineTest, CoolOnHighTemp29_9_30_27_9_28) {
  set("temp_air", 29900, 0);
  EXPECT_FALSE(run("fan_main", 0).has_value());
  set("temp_air", 30000, 10);
  EXPECT_EQ(run("fan_main", 10), 600);
  set("temp_air", 28000, 20);
  EXPECT_EQ(run("fan_main", 20), 600);  // 28.0 は「28.0未満」ではない
  set("temp_air", 27900, 30);
  EXPECT_FALSE(run("fan_main", 30).has_value());
}

TEST_F(RuleEngineTest, WaterOnDryPulseExactly5sAndCooldownExactly10min) {
  set("soil", 30000, 0);
  EXPECT_FALSE(run("pump", 0).has_value());  // 30.0% は「30.0%未満」ではない
  set("soil", 29900, 1000);
  EXPECT_EQ(run("pump", 1000), 1000);
  EXPECT_EQ(run("pump", 1000 + 4999), 1000);
  EXPECT_FALSE(run("pump", 1000 + 5000).has_value());  // ちょうど5秒で止まる
  EXPECT_FALSE(run("pump", 1000 + 599'999).has_value());  // クールダウン中（パルスの開始から10分）
  // stale にならないよう読み直す
  set("soil", 29900, 1000 + 599'999);
  EXPECT_FALSE(run("pump", 1000 + 599'999).has_value());
  EXPECT_EQ(run("pump", 1000 + 600'000), 1000);  // ちょうど10分で再びパルス
}

TEST_F(RuleEngineTest, NotGoodInputGivesNoProposalAndStopsPulse) {
  set("soil", 25000, 0);
  EXPECT_EQ(run("pump", 0), 1000);
  set("soil", 25000, 2000, Quality::Fault);
  EXPECT_FALSE(run("pump", 2000).has_value());  // パルスを止める
  set("soil", 25000, 3000);
  EXPECT_FALSE(run("pump", 3000).has_value());  // クールダウンはパルスの開始から数え続ける
  // stale でも提案を出さない
  set("lux", 100000, 0);
  EXPECT_EQ(run("led_grow", 3000), 1000);
  EXPECT_FALSE(run("led_grow", 3001).has_value());  // 周期1000の3倍を超えた
}

TEST_F(RuleEngineTest, PowerSaveStopsLightingOnly) {
  set("lux", 100000, 0);
  set("temp_air", 31000, 0);
  EXPECT_EQ(run("led_grow", 0, true, false), std::nullopt);
  EXPECT_EQ(run("fan_main", 0, true, false), 600);
  EXPECT_EQ(run("led_grow", 10, true, true), 1000);  // 戻すと、いまの入力で決め直す
}

TEST_F(RuleEngineTest, RulesDisabledThenReenabledReevaluatesNow) {
  set("soil", 25000, 0);
  EXPECT_FALSE(run("pump", 0, false).has_value());  // 手動モード（ルール無効）ではパルスを始めない
  set("soil", 25000, 5000);
  EXPECT_EQ(run("pump", 5000, true), 1000);  // 有効になった瞬間に、いまの入力で始める（#35）
}

TEST_F(RuleEngineTest, PulseKeepsCountingWhileRulesDisabled) {
  set("soil", 25000, 0);
  EXPECT_EQ(run("pump", 0), 1000);
  EXPECT_FALSE(run("pump", 2000, false).has_value());
  EXPECT_EQ(run("pump", 3000, true), 1000);          // 残り2秒は動かす
  EXPECT_FALSE(run("pump", 5000, true).has_value());  // 開始から5秒で終わり
}

TEST_F(RuleEngineTest, LargerProposalWinsForSameActuator) {
  constexpr std::array<domain::Rule, 2> rules{{
      {"a", "temp_air", domain::Comparison::AtLeast, 30000, 28000, "fan_main", 600, 0, 0, false},
      {"b", "temp_air", domain::Comparison::AtLeast, 35000, 33000, "fan_main", 900, 0, 0, false},
  }};
  RuleEngine e(rules);
  set("temp_air", 36000, 0);
  e.evaluate(store, 0, true, true, out);
  ASSERT_EQ(out.size(), 1U);
  EXPECT_EQ(out[0].level, 900);
  set("temp_air", 31000, 10);
  e.evaluate(store, 10, true, true, out);
  ASSERT_EQ(out.size(), 1U);
  EXPECT_EQ(out[0].level, 600);
}

}  // namespace
}  // namespace node::app
