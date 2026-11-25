// リセット要因と、BOOT の detail.reset_reason（7値）への対応（基本設計書 第6章「リセット要因の対応」、P6 の決定 #100・#112）。
// 機器に依らない列挙をここに置き、ESP-IDF の esp_reset_reason() の値からの変換は platform/esp32 が受け持つ。
#pragma once

#include <cstdint>
#include <string_view>

namespace node::infra {

/// @brief 機器に依らないリセット要因（ESP-IDF の esp_reset_reason_t の区別をそのまま持つ）。
enum class PlatformResetCause : std::uint8_t {
  PowerOn,            ///< 電源投入（ESP_RST_POWERON）
  External,           ///< 外部のリセットピン（ESP_RST_EXT）
  Software,           ///< ソフトウェアのリセット（ESP_RST_SW）
  Panic,              ///< 例外・パニック（ESP_RST_PANIC）
  InterruptWatchdog,  ///< 割り込みのウォッチドッグ（ESP_RST_INT_WDT）
  TaskWatchdog,       ///< タスクのウォッチドッグ（ESP_RST_TASK_WDT）
  OtherWatchdog,      ///< そのほかのウォッチドッグ（ESP_RST_WDT）
  Brownout,           ///< 電圧低下（ESP_RST_BROWNOUT）
  PowerGlitch,        ///< 電源のグリッチ（ESP_RST_PWR_GLITCH）
  DeepSleep,          ///< ディープスリープからの復帰（ESP_RST_DEEPSLEEP）
  Unknown,            ///< そのほか（ESP_RST_UNKNOWN・USB・JTAG・SDIO・eFuse・CPU のロックアップなど）
};

/// @brief BOOT の detail.reset_reason の値（基本設計書 第6章の表）。
/// @param cause リセット要因
/// @return power_on・pin・software・watchdog・brownout・low_power・unknown のどれか
constexpr std::string_view reset_reason_of(PlatformResetCause cause) {
  switch (cause) {
    case PlatformResetCause::PowerOn: return "power_on";
    case PlatformResetCause::External: return "pin";
    case PlatformResetCause::Software: return "software";
    case PlatformResetCause::Panic: return "software";  // 作品01と揃えるため共通仕様の変更案で確かめる（基本設計書）
    case PlatformResetCause::InterruptWatchdog:
    case PlatformResetCause::TaskWatchdog:
    case PlatformResetCause::OtherWatchdog: return "watchdog";
    case PlatformResetCause::Brownout:
    case PlatformResetCause::PowerGlitch: return "brownout";
    case PlatformResetCause::DeepSleep: return "low_power";
    case PlatformResetCause::Unknown: return "unknown";
  }
  return "unknown";
}

}  // namespace node::infra
