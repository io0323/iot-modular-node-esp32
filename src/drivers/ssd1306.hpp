// Solomon Systech SSD1306（128×64 の OLED コントローラー）のドライバ。I2C で使う。
//
// 出典：Solomon Systech「SSD1306 Advance Information」Rev 1.1、Apr 2008（以下「データシート」）、
// 同じ文書に綴じられた「SSD1306 Application Note」Rev 0.4、Jan 2009（以下「アプリケーションノート」）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/ui.hpp"
#include "hal/hal.hpp"

namespace node::drivers {

/// @brief SSD1306 のドライバ。文字列を 5×7 のフォントで描き、ページ単位で書き込む。
class Ssd1306 final : public domain::IDisplay {
 public:
  static constexpr std::uint8_t kAddrSa0Low = 0x3C;   ///< "b0111100"（データシート 8.1.5 節）
  static constexpr std::uint8_t kAddrSa0High = 0x3D;  ///< "b0111101"（同）
  /// 制御バイト：Co = 0（以後はデータだけ）、D/C# = 0 でコマンド、1 で GDDRAM のデータ（データシート 8.1.5 節 5)）
  static constexpr std::uint8_t kControlCommands = 0x00;
  static constexpr std::uint8_t kControlData = 0x40;  ///< 制御バイト：GDDRAM のデータ（上の説明を参照）
  static constexpr std::size_t kWidth = 128;           ///< 列の数（ドット）
  static constexpr std::size_t kPages = 8;  ///< 8 ページ × 8 ドット（データシート 8.7 節 図8-13）
  static constexpr std::size_t kCellWidth = 6;  ///< 5 ドットの文字＋1 ドットの間

  /// 初期化のコマンド列。手順と順番はアプリケーションノート 3 章 図2（Software Initialization Flow Chart）のとおり。
  /// 値は 128×64 に合わせる（open-questions #56）。どれもデータシート 9 章 Command Table（表9-1）のリセット値：
  /// MUX 3Fh（64MUX）、COM ピン 12h（A[4]=1b Alternative、A[5]=0b）、セグメントの向き A0h、COM の走査 C0h。
  /// 表示の向きは実機フェーズで確かめる。チャージポンプは 8Dh, 14h（アプリケーションノート 2.1 節 Note (1)）。
  static constexpr std::array<std::uint8_t, 18> kInitSequence{
      0xA8, 0x3F,  // Set MUX Ratio
      0xD3, 0x00,  // Set Display Offset
      0x40,        // Set Display Start Line
      0xA0,        // Set Segment re-map
      0xC0,        // Set COM Output Scan Direction
      0xDA, 0x12,  // Set COM Pins hardware configuration
      0x81, 0x7F,  // Set Contrast Control
      0xA4,        // Disable Entire Display On（GDDRAM の内容を表示）
      0xA6,        // Set Normal Display
      0xD5, 0x80,  // Set Osc Frequency
      0x8D, 0x14,  // Enable charge pump regulator
      0xAF,        // Display On
  };
  static constexpr std::uint8_t kDisplayOff = 0xAE;  ///< データシート 10.1.12 節
  static constexpr std::uint8_t kDisplayOn = 0xAF;   ///< 同（表示が出るまで 100ms。8.9 節 tAF）
  static constexpr std::uint8_t kPageStart = 0xB0;   ///< B0h〜B7h（10.1.13 節。ページアドレッシングはリセット時の既定）
  static constexpr std::uint8_t kLowerColumn = 0x00;   ///< 00h〜0Fh（10.1.1 節）
  static constexpr std::uint8_t kHigherColumn = 0x10;  ///< 10h〜1Fh（10.1.2 節）

  /// @brief ドライバを作る。
  /// @param dev I2C の宛先（アドレスは 0x3C か 0x3D）
  explicit Ssd1306(hal::I2cDevice dev) : dev_(dev) {}

  support::Result<void> init() override;
  support::Result<void> set_power(bool on) override;
  support::Result<void> show(std::span<const std::string_view> lines) override;

  /// @brief フレームバッファ（1,024 バイト。ページ順、各ページ 128 バイト）。
  /// @return フレームバッファ
  [[nodiscard]] const std::array<std::uint8_t, kWidth * kPages>& framebuffer() const { return fb_; }

 private:
  support::Result<void> commands(std::span<const std::uint8_t> cmds) const;
  support::Result<void> flush();
  void render(std::span<const std::string_view> lines);

  hal::I2cDevice dev_;
  std::array<std::uint8_t, kWidth * kPages> fb_{};  ///< インスタンスと一緒に静的に確保する
};

}  // namespace node::drivers
