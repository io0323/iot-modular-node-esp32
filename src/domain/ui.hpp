#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "support/result.hpp"

namespace node::domain {

/// @brief ボタンの判定結果（P3 の決定 #41）。
enum class ButtonEvent : std::uint8_t {
  None,   ///< 判定なし
  Short,  ///< 短押し
  Long,   ///< 長押し
};

/// @brief ボタン（タクトスイッチ）の共通の契約。
class IButton {
 public:
  virtual ~IButton() = default;
  /// @brief 初期化する（GPIO を入力にする）。
  /// @return 初期化できなければ失敗
  virtual support::Result<void> init() = 0;
  /// @brief 20ms ごとに呼び、判定が出たら返す。
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 判定（なければ ButtonEvent::None）
  virtual ButtonEvent poll(std::uint64_t now_ms) = 0;
};

/// @brief 文字の表示器（OLED）の共通の契約。
class IDisplay {
 public:
  static constexpr std::size_t kRows = 8;      ///< 行の数
  static constexpr std::size_t kColumns = 21;  ///< 1行の文字数

  virtual ~IDisplay() = default;
  /// @brief 初期化する。
  /// @return 初期化できなければ失敗
  virtual support::Result<void> init() = 0;
  /// @brief 表示のオン・オフ。
  /// @param on オンにするなら true
  /// @return 切り替えられなければ失敗
  virtual support::Result<void> set_power(bool on) = 0;
  /// @brief 行ごとの文字列を表示する（kRows 行まで。各行は kColumns 文字まで、超える分は切る）。
  /// @param lines 行ごとの文字列
  /// @return 表示できなければ失敗
  virtual support::Result<void> show(std::span<const std::string_view> lines) = 0;
};

/// @brief 本体の UI の機器（構成表で決める。どれも省略できる。P3 の決定 #34）。
struct UiDevices {
  IButton* mode_button{nullptr};    ///< MODE：短押しでモードを切り替える
  IButton* action_button{nullptr};  ///< ACTION：手動モードで機器を選ぶ・切り替える
  IDisplay* display{nullptr};       ///< OLED
};

}  // namespace node::domain
