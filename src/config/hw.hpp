// 構成表の実機用の行（ドライバ）。hal のインターフェースにつなぐので、ホスト版の --devices fakehw（フェイクの hal）と
// ターゲット版（platform/esp32 の hal）で同じ表を使う（NFR-01、P6 の決定 #94・#105）。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>

#include "config/devices.hpp"
#include "domain/device.hpp"
#include "domain/pump_interlock.hpp"
#include "drivers/actuators.hpp"
#include "drivers/bh1750.hpp"
#include "drivers/bme280.hpp"
#include "drivers/button.hpp"
#include "drivers/sht31.hpp"
#include "drivers/soil_moisture.hpp"
#include "drivers/ssd1306.hpp"
#include "hal/hal.hpp"

/// 構成表の行の、括弧でくくった引数の並びを外す（config/hw_table.hpp・fakehw_table.hpp）
#define NODE_CONFIG_UNPAREN(...) __VA_ARGS__

namespace node::config {

// ---- 論理名（hal::Pin。ターゲット版の GPIO の番号は esp32_pins.hpp）
inline constexpr hal::Pin kAdcSoil = 0;          ///< ADC_SOIL
inline constexpr hal::Pin kPwmFan = 0;           ///< PWM_FAN（LEDC のチャネル）
inline constexpr hal::Pin kPwmLed = 1;           ///< PWM_LED
inline constexpr hal::Pin kPwmBuzzer = 2;        ///< PWM_BUZZER
inline constexpr hal::Pin kGpioPumpRelay = 0;    ///< GPIO_PUMP_RELAY
inline constexpr hal::Pin kGpioButtonMode = 1;   ///< GPIO_BTN_MODE
inline constexpr hal::Pin kGpioButtonAction = 2; ///< GPIO_BTN_ACTION
inline constexpr std::uint8_t kAddrBme280 = 0x76;  ///< SDO を GND（BME280 データシート 6.2 節）
inline constexpr std::uint8_t kAddrBh1750 = drivers::Bh1750::kAddrLow;      ///< ADDR を GND
inline constexpr std::uint8_t kAddrSsd1306 = drivers::Ssd1306::kAddrSa0Low;  ///< SA0 を GND

// ---- PWM（P3 の決定 #44）
inline constexpr drivers::PwmConfig kPwmFanConfig{kPwmFan, 25000, 10};       ///< fan_main
inline constexpr drivers::PwmConfig kPwmLedConfig{kPwmLed, 1000, 10};        ///< led_grow
inline constexpr drivers::PwmConfig kPwmBuzzerConfig{kPwmBuzzer, 2700, 10};  ///< buzzer
inline constexpr bool kPumpRelayActiveHigh = true;  ///< #46
inline constexpr drivers::SoilCalibration kSoilCalibration{3000, 1300};  ///< 仮の値（#43。実機フェーズで校正）

/// @brief 実機用の hal の組（どれも寿命はプログラム全体）。
struct HwInterfaces {
  hal::II2cBus& i2c;        ///< I2C0
  hal::IMutex& i2c_mutex;   ///< I2C0 の排他（sense と ui のタスクが分け合う）
  hal::IAdc& adc;           ///< ADC
  hal::IPwm& pwm;           ///< PWM（LEDC）
  hal::IGpio& gpio;         ///< GPIO
  hal::ITimer& timer;       ///< ドライバの待ち時間
};

/// @brief 構成表の実機用の行（センサー・アクチュエーターは config/hw_table.hpp の1行が1台。ほかにインターロック・
/// ボタン2つ・OLED）。
class HwRig {
 public:
  /// @brief ドライバを hal につなぐ（機器にはまだ触らない。初期化は Node::start）。
  /// @param hw hal の組
  explicit HwRig(const HwInterfaces& hw);
  HwRig(const HwRig&) = delete;
  HwRig& operator=(const HwRig&) = delete;
  HwRig(HwRig&&) = delete;
  HwRig& operator=(HwRig&&) = delete;
  ~HwRig() = default;

  /// @brief 機器の一覧（Node に渡す）。
  /// @return センサー・アクチュエーター・ルール・UI の機器
  [[nodiscard]] domain::DeviceRegistry registry() {
    return domain::DeviceRegistry(sensors_, actuators_, kRules,
                                  domain::UiDevices{&mode_button_, &action_button_, &display_});
  }

 private:
  /// I2C0 の宛先（構成表の行から使う）
  static hal::I2cDevice i2c(const HwInterfaces& hw, std::uint8_t address) {
    return hal::I2cDevice{&hw.i2c, &hw.i2c_mutex, address};
  }

  // ---- 構成表（config/hw_table.hpp。1行が1台）：チャネル・仕様（ドライバが参照する）とドライバ
#define NODE_SENSOR(type, name, args, period_ms, channels)                                                \
  static constexpr auto name##_channels_ = std::to_array<domain::ChannelSpec>({NODE_CONFIG_UNPAREN channels}); \
  type name##_;
#define NODE_ACTUATOR(type, name, args, spec, interlocks) \
  static constexpr domain::ActuatorSpec name##_spec_{NODE_CONFIG_UNPAREN spec}; \
  type name##_;
#include "config/hw_table.hpp"
#undef NODE_SENSOR
#undef NODE_ACTUATOR

  // ---- 本体の UI
  drivers::Ssd1306 display_;
  drivers::Button mode_button_;
  drivers::Button action_button_;

  // ---- インターロック（1行が1つ）
  domain::FanTempInterlock fan_temp_high_{kFanTempHigh};
  domain::SensorFaultInterlock fan_on_temp_fault_{kFanOnTempFault};
  domain::PumpInterlock pump_max_on_{kPumpMaxOnS};
  domain::SensorFaultInterlock pump_off_on_soil_fault_{kPumpOffOnSoilFault};
  std::array<domain::IInterlock*, 2> fan_interlocks_{&fan_temp_high_, &fan_on_temp_fault_};
  std::array<domain::IInterlock*, 2> pump_interlocks_{&pump_max_on_, &pump_off_on_soil_fault_};

  // ---- 一覧（構成表の行の数と順）
#define NODE_SENSOR(type, name, args, period_ms, channels) 1,
#define NODE_ACTUATOR(type, name, args, spec, interlocks)
  static constexpr std::size_t kSensorCount = std::initializer_list<int>{
#include "config/hw_table.hpp"
  }.size();
#undef NODE_SENSOR
#undef NODE_ACTUATOR
#define NODE_SENSOR(type, name, args, period_ms, channels)
#define NODE_ACTUATOR(type, name, args, spec, interlocks) 1,
  static constexpr std::size_t kActuatorCount = std::initializer_list<int>{
#include "config/hw_table.hpp"
  }.size();
#undef NODE_SENSOR
#undef NODE_ACTUATOR
  std::array<domain::ISensor*, kSensorCount> sensors_;
  std::array<domain::ActuatorBinding, kActuatorCount> actuators_;
};

}  // namespace node::config
