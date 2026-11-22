#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/control_arbiter.hpp"
#include "app/manual_control.hpp"
#include "app/mode_manager.hpp"
#include "domain/reading_store.hpp"
#include "domain/ui.hpp"
#include "support/fixed_string.hpp"

namespace node::app {

/// @brief 本体のボタンと OLED（基本設計書 FR-04・FR-13、P3 の決定 #34・#50・#60）。
///
/// - MODE：短押しでモードを切り替える（モードの切り替えは Node が行うので、tick の戻り値で知らせる）
/// - ACTION：手動モードでだけ働く。短押しで操作する機器を選び直し、長押しで選んだ機器を 0 と 1000 で切り替える
/// - 表示：1秒ごとに8行を書き換える。省電力モードでは表示を消す（ディスプレイオフ）
class UiService {
 public:
  static constexpr std::uint64_t kButtonPeriodMs = 20;
  static constexpr std::uint64_t kDisplayPeriodMs = 1000;
  using Line = support::FixedString<domain::IDisplay::kColumns>;

  /// @brief UI の機器（どれも省略できる）。
  using Devices = domain::UiDevices;

  /// @brief tick の結果。
  struct Actions {
    bool mode_pressed{false};  ///< MODE の短押し（Node がモードを切り替える）
  };

  UiService(Devices devices, ModeManager& modes, ManualControl& manual, const ControlArbiter& arbiter,
            const domain::ReadingStore& store)
      : dev_(devices), modes_(modes), manual_(manual), arbiter_(arbiter), store_(store) {}

  /// @brief 機器を初期化する。
  support::Result<void> init();
  /// @brief 10ms ごとに呼ぶ（ボタンは 20ms ごと、表示は1秒ごとに動く）。
  /// @param now_ms いまの時刻
  /// @param connected MQTT に接続しているか（1行目の表示）
  Actions tick(std::uint64_t now_ms, bool connected);

  /// @brief 表示する8行を作る（テストで中身を確かめる）。
  [[nodiscard]] std::array<Line, domain::IDisplay::kRows> compose(std::uint64_t now_ms, bool connected) const;

  [[nodiscard]] bool display_on() const { return display_on_; }
  [[nodiscard]] std::uint32_t display_errors() const { return display_errors_; }

 private:
  void handle_action(domain::ButtonEvent ev, std::uint64_t now_ms);
  void refresh_display(std::uint64_t now_ms, bool connected);

  Devices dev_;
  ModeManager& modes_;
  ManualControl& manual_;
  const ControlArbiter& arbiter_;
  const domain::ReadingStore& store_;
  std::uint64_t next_button_ms_{0};
  std::uint64_t next_display_ms_{0};
  bool display_on_{true};
  std::uint32_t display_errors_{0};
};

}  // namespace node::app
