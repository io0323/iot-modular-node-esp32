#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "domain/reading_store.hpp"
#include "domain/rule.hpp"
#include "domain/types.hpp"
#include "support/static_vector.hpp"

namespace node::app {

/// @brief ローカルルールの評価（基本設計書 FR-05、P2 の決定 #29〜#32、#35）。出力は「提案」だけで、機器は動かさない。
///
/// - 入力が good でなければ、そのルールは「不明」として提案を出さず、実行中のパルスも止める
/// - ルールが有効になった時点で、いまの入力から状態を決め直す（#35）。無効の間の経緯は持ち越さない
/// - パルスは時刻で数え続ける（上位の段が勝っている間も）。クールダウンはパルスの開始から数える
/// - 同じアクチュエーターに複数のルールが提案したら、大きい方の level を使う
class RuleEngine {
 public:
  static constexpr std::size_t kMaxRules = 16;

  /// @brief アクチュエーターごとの提案。
  struct Proposal {
    std::string_view actuator;
    domain::Level level{0};
  };
  using Proposals = support::StaticVector<Proposal, kMaxRules>;

  /// @brief ルールを受け取る（kMaxRules 件まで）。
  explicit RuleEngine(std::span<const domain::Rule> rules);

  /// @brief ルールを評価し、提案を出す。
  /// @param store 最新の計測値と品質
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @param rules_enabled ルールの段が有効か（自動・省電力モード）
  /// @param lighting_enabled lighting の印のルールが有効か（省電力モードでは止める）
  /// @param out 提案の書き込み先（アクチュエーターごとに1件）
  void evaluate(const domain::ReadingStore& store, std::uint64_t now_ms, bool rules_enabled, bool lighting_enabled,
                Proposals& out);

 private:
  struct State {
    bool on{false};                                ///< ヒステリシスのルールが入っている
    std::optional<std::uint64_t> pulse_start_ms;   ///< 実行中のパルスの開始
    std::optional<std::uint64_t> last_pulse_ms;    ///< 最後にパルスを始めた時刻（クールダウン）
  };

  static std::optional<domain::Level> step(const domain::Rule& r, State& st, std::optional<std::int32_t> value,
                                    std::uint64_t now_ms, bool active);
  // ヒステリシスのルール（pulse_ms が 0）
  static std::optional<domain::Level> step_hysteresis(const domain::Rule& r, State& st, std::int32_t value);
  // パルスのルール（クールダウンつき）
  static std::optional<domain::Level> step_pulse(const domain::Rule& r, State& st, std::int32_t value,
                                                 std::uint64_t now_ms);

  std::span<const domain::Rule> rules_;
  std::array<State, kMaxRules> states_{};
};

}  // namespace node::app
