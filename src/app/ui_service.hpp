#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "app/control_arbiter.hpp"
#include "app/manual_control.hpp"
#include "app/mode_manager.hpp"
#include "domain/io.hpp"
#include "domain/reading_store.hpp"
#include "domain/ui.hpp"
#include "support/fixed_string.hpp"

namespace node::app {

/// @brief 本体のボタンと OLED（基本設計書 FR-04・FR-13、P3 の決定 #34・#50・#60）。
///
/// - MODE：短押しでモードを切り替える（モードの切り替えは Node が行うので、tick の戻り値で知らせる）
/// - ACTION：手動モードでだけ働く。短押しで操作する機器を選び直し、長押しで選んだ機器を 0 と 1000 で切り替える
/// ハードに触る部分（ボタンの poll・OLED への書き込み）は domain::IUiIo（drivers::UiIo）が受け持つ（P6 の決定 #110）。
/// - 表示：1秒ごとに8行を書き換える。省電力モードでは表示を消す（ディスプレイオフ）
class UiService {
 public:
  static constexpr std::uint64_t kDisplayPeriodMs = 1000;  ///< 表示を書き直す周期
  using Line = domain::DisplayLine;                        ///< 表示の1行

  /// @brief tick の結果。
  struct Actions {
    bool mode_pressed{false};  ///< MODE の短押し（Node がモードを切り替える）
  };

  /// @brief UI を受け持つ。
  /// @param has_display OLED があるか（なければ表示の内容を作らない）
  /// @param modes モード（MODE ボタン）
  /// @param manual 本体の手動操作（ACTION ボタン）
  /// @param arbiter 出力（表示）
  /// @param store 計測値（表示）
  UiService(bool has_display, ModeManager& modes, ManualControl& manual, const ControlArbiter& arbiter,
            const domain::ReadingStore& store)
      : has_display_(has_display), modes_(modes), manual_(manual), arbiter_(arbiter), store_(store) {}

  /// @brief 入力を1件受ける（ACTION はその場で ManualControl に渡す。MODE は戻り値で Node に知らせる）。
  /// @param in 入力
  /// @param now_ms いまの時刻
  /// @param actions MODE の短押しの書き込み先
  void handle(const domain::UiInput& in, std::uint64_t now_ms, Actions& actions);
  /// @brief 表示を書き直す時刻なら、表示の内容を作る（1秒ごと。省電力モードに入ったり出たりしたらすぐ）。
  /// @param now_ms いまの時刻
  /// @param connected MQTT に接続しているか（1行目の表示）
  /// @return 表示の内容。書き直さないなら std::nullopt
  std::optional<domain::DisplayFrame> frame(std::uint64_t now_ms, bool connected);

  /// @brief 1周分：io から入力をすべて受け、表示を書き直す時刻なら io に渡す。10ms ごとに呼ぶ。
  /// @param now_ms いまの時刻
  /// @param connected MQTT に接続しているか（1行目の表示）
  /// @param io UI のハードに触る部分
  /// @return Node がすること（MODE の短押し）
  Actions tick(std::uint64_t now_ms, bool connected, domain::IUiIo& io);

  /// @brief 表示する8行を作る（テストで中身を確かめる）。
  /// @param now_ms いまの時刻（計測値の品質）
  /// @param connected MQTT に接続しているか
  /// @return 8行
  [[nodiscard]] std::array<Line, domain::IDisplay::kRows> compose(std::uint64_t now_ms, bool connected) const;

  /// @brief 表示がオンか（省電力モードではオフ）。
  /// @return オンなら true
  [[nodiscard]] bool display_on() const { return display_on_; }
  /// @brief 表示に失敗した回数（診断用）。
  /// @return 回数
  [[nodiscard]] std::uint32_t display_errors() const { return display_errors_; }

 private:
  bool has_display_;
  ModeManager& modes_;
  ManualControl& manual_;
  const ControlArbiter& arbiter_;
  const domain::ReadingStore& store_;
  std::uint64_t next_display_ms_{0};
  bool display_on_{true};
  std::uint32_t display_errors_{0};
};

}  // namespace node::app
