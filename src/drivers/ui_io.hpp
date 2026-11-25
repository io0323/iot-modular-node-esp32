// 本体の UI のハードに触る部分（P6 の決定 #96・#110）。ボタンを 20ms ごとに読み、表示の内容を OLED に書く。
// ホスト版は Node がその場で呼ぶ。ターゲット版は ui のタスクが呼ぶ。
#pragma once

#include <cstddef>
#include <cstdint>

#include "domain/io.hpp"
#include "domain/ui.hpp"
#include "support/static_vector.hpp"

namespace node::drivers {

/// @brief ボタンと OLED（どれも省略できる。P3 の決定 #34）。
class UiIo final : public domain::IUiIo {
 public:
  static constexpr std::uint64_t kButtonPeriodMs = 20;  ///< ボタンを読む周期（#41）
  static constexpr std::size_t kMaxPending = 8;          ///< 取り出していない入力の上限

  /// @brief UI の機器を受け取る。
  /// @param devices ボタンと OLED（なければ nullptr）
  explicit UiIo(const domain::UiDevices& devices) : dev_(devices) {}

  support::Result<void> init() override;
  bool poll_input(std::uint64_t now_ms, domain::UiInput& out) override;
  void show(const domain::DisplayFrame& frame) override;

 private:
  void push(domain::UiInputKind kind, domain::ButtonEvent event);

  domain::UiDevices dev_;
  std::uint64_t next_button_ms_{0};
  bool display_on_{true};
  support::StaticVector<domain::UiInput, kMaxPending> pending_{};
};

}  // namespace node::drivers
