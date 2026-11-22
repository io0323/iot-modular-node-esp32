#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "support/result.hpp"

namespace node::domain {

/// @brief ボタンの判定結果（P3 の決定 #41）。
enum class ButtonEvent : std::uint8_t { None, Short, Long };

/// @brief ボタン（タクトスイッチ）の共通の契約。
class IButton {
 public:
  virtual ~IButton() = default;
  virtual support::Result<void> init() = 0;
  /// @brief 20ms ごとに呼び、判定が出たら返す。
  /// @param now_ms いまの時刻（単調時計の ms）
  virtual ButtonEvent poll(std::uint64_t now_ms) = 0;
};

/// @brief 文字の表示器（OLED）の共通の契約。
class IDisplay {
 public:
  static constexpr std::size_t kRows = 8;
  static constexpr std::size_t kColumns = 21;

  virtual ~IDisplay() = default;
  virtual support::Result<void> init() = 0;
  /// @brief 表示のオン・オフ。
  virtual support::Result<void> set_power(bool on) = 0;
  /// @brief 行ごとの文字列を表示する（kRows 行まで。各行は kColumns 文字まで、超える分は切る）。
  virtual support::Result<void> show(std::span<const std::string_view> lines) = 0;
};

/// @brief 本体の UI の機器（構成表で決める。どれも省略できる。P3 の決定 #34）。
struct UiDevices {
  IButton* mode_button{nullptr};    ///< MODE：短押しでモードを切り替える
  IButton* action_button{nullptr};  ///< ACTION：手動モードで機器を選ぶ・切り替える
  IDisplay* display{nullptr};       ///< OLED
};

}  // namespace node::domain
