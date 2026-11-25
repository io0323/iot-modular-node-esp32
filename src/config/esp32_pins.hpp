// ターゲット版（ESP32-S3-DevKitC-1 を想定）のピンの割り当て（仮。P6 の決定 #105。実機フェーズで見直す）。
// ストラッピングピン（GPIO0・3・45・46）と USB（GPIO19・20）を避ける。platform/esp32 の hal がこの表で
// 論理名（config/hw.hpp の hal::Pin）を GPIO の番号に変える。ESP-IDF の型は使わない（番号だけ）。
#pragma once

#include <array>
#include <cstdint>

#include "config/hw.hpp"
#include "hal/hal.hpp"

namespace node::config::esp32 {

inline constexpr int kI2cSdaGpio = 8;  ///< I2C0 の SDA
inline constexpr int kI2cSclGpio = 9;  ///< I2C0 の SCL
inline constexpr int kSoilAdcGpio = 1;  ///< 土壌水分（GPIO1 は ADC1 のチャネル0）

/// @brief 論理名と GPIO の番号の組。
struct PinMap {
  hal::Pin logical;  ///< 論理名（config/hw.hpp）
  int gpio;          ///< GPIO の番号
};

/// @brief LEDC のチャネル（kPwmFan など）と GPIO。
inline constexpr std::array<PinMap, 3> kPwmPins{{{kPwmFan, 4}, {kPwmLed, 5}, {kPwmBuzzer, 6}}};
/// @brief GPIO の論理名と番号（ポンプのリレー・MODE・ACTION）。
inline constexpr std::array<PinMap, 3> kGpioPins{{{kGpioPumpRelay, 7}, {kGpioButtonMode, 10}, {kGpioButtonAction, 11}}};
/// @brief ADC の論理名と GPIO。
inline constexpr std::array<PinMap, 1> kAdcPins{{{kAdcSoil, kSoilAdcGpio}}};

}  // namespace node::config::esp32
