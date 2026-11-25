#pragma once

#include <cstdint>
#include <string_view>

#include "app/control_arbiter.hpp"
#include "app/mode_manager.hpp"
#include "domain/types.hpp"

namespace node::app {

/// @brief 本体の手動操作（手動モードでの手動の出力）。
///
/// 手動モードのときだけ受け付け、最後の操作の時刻を ModeManager に知らせる。
/// ボタンの割り当ては P3 で決める（open-questions #34）。P2 ではテストとホスト版のデバッグ入力から呼ぶ。
class ManualControl {
 public:
  /// @brief set_manual の結果。
  enum class Outcome : std::uint8_t { Accepted, NotManualMode, UnknownTarget };

  /// @brief 本体の手動操作を受け持つ。
  /// @param arbiter 出力（手動の段）
  /// @param modes モード（手動モードのときだけ受け付ける）
  ManualControl(ControlArbiter& arbiter, ModeManager& modes) : arbiter_(arbiter), modes_(modes) {}

  /// @brief 手動の出力を決める。
  /// @param id アクチュエーター
  /// @param level 出力（min〜max に収める）
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 受け付けたか、受け付けなかった理由
  Outcome set_manual(std::string_view id, domain::Level level, std::uint64_t now_ms);

  /// @brief 手動の設定をすべて消す（手動モードを出たとき）。
  void clear() { arbiter_.clear_manual(); }

  /// @brief 操作する機器を次に選び直す（構成表の順に一周する。P3 の決定 #34）。手動モードでなければ何もしない。
  /// @return 選び直したか、しなかった理由
  Outcome select_next();
  /// @brief 選んでいる機器を 0 と 1000 で切り替える（P3 の決定 #34）。手動モードでなければ何もしない。
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 切り替えたか、しなかった理由
  Outcome toggle_selected(std::uint64_t now_ms);
  /// @brief 選んでいる機器の番号。
  /// @return 番号
  [[nodiscard]] std::size_t selected() const { return selected_; }
  /// @brief 選んでいる機器の ID。
  /// @return ID（機器がなければ空）
  [[nodiscard]] std::string_view selected_id() const;

 private:
  ControlArbiter& arbiter_;
  ModeManager& modes_;
  std::size_t selected_{0};
};

/// @brief 結果の文字列（ログ用）。
constexpr std::string_view to_string(ManualControl::Outcome o) {
  switch (o) {
    case ManualControl::Outcome::Accepted: return "accepted";
    case ManualControl::Outcome::NotManualMode: return "not_manual_mode";
    case ManualControl::Outcome::UnknownTarget: return "unknown_target";
  }
  return "";
}

}  // namespace node::app
