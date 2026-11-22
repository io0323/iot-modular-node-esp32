// 構成表の実機用の行（ドライバ）を、フェイクの hal・機器につないだ組み合わせ（ホスト版の --devices fakehw、
// ホスト結合テスト）。ピン番号は P6 で決めるので、ここでは論理名（I2C0・ADC_SOIL など）だけ。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include "config/devices.hpp"
#include "domain/device.hpp"
#include "domain/pump_interlock.hpp"
#include "drivers/actuators.hpp"
#include "drivers/bh1750.hpp"
#include "drivers/bme280.hpp"
#include "drivers/button.hpp"
#include "drivers/fake/fake_devices.hpp"
#include "drivers/fake/fake_hal.hpp"
#include "drivers/soil_moisture.hpp"
#include "drivers/ssd1306.hpp"

namespace node::config {

// ---- 論理名（実際のピン番号は P6 で決める）
inline constexpr hal::Pin kAdcSoil = 0;          ///< ADC_SOIL
inline constexpr hal::Pin kPwmFan = 0;           ///< PWM_FAN（LEDC のチャネル）
inline constexpr hal::Pin kPwmLed = 1;           ///< PWM_LED
inline constexpr hal::Pin kPwmBuzzer = 2;        ///< PWM_BUZZER
inline constexpr hal::Pin kGpioPumpRelay = 0;    ///< GPIO_PUMP_RELAY
inline constexpr hal::Pin kGpioButtonMode = 1;   ///< GPIO_BTN_MODE
inline constexpr hal::Pin kGpioButtonAction = 2; ///< GPIO_BTN_ACTION
inline constexpr std::uint8_t kAddrBme280 = 0x76;  ///< SDO を GND（BME280 データシート 6.2 節）
inline constexpr std::uint8_t kAddrBh1750 = drivers::Bh1750::kAddrLow;
inline constexpr std::uint8_t kAddrSsd1306 = drivers::Ssd1306::kAddrSa0Low;

// ---- PWM（P3 の決定 #44）
inline constexpr drivers::PwmConfig kPwmFanConfig{kPwmFan, 25000, 10};
inline constexpr drivers::PwmConfig kPwmLedConfig{kPwmLed, 1000, 10};
inline constexpr drivers::PwmConfig kPwmBuzzerConfig{kPwmBuzzer, 2700, 10};
inline constexpr bool kPumpRelayActiveHigh = true;  ///< #46
inline constexpr drivers::SoilCalibration kSoilCalibration{3000, 1300};  ///< 仮の値（#43。実機フェーズで校正）

/// @brief ドライバとフェイクの組み合わせ一式。状態を持つので、使う側（main・テスト）が1つずつ作る。
class FakeHwRig {
 public:
  /// @param timer ドライバの待ち時間（ホスト版は実時間、テストは FakeTimer）
  /// @param i2c_mutex I2C バスの排他
  FakeHwRig(hal::ITimer& timer, hal::IMutex& i2c_mutex);
  FakeHwRig(const FakeHwRig&) = delete;
  FakeHwRig& operator=(const FakeHwRig&) = delete;

  [[nodiscard]] domain::DeviceRegistry registry() {
    return domain::DeviceRegistry(sensors_, actuators_, kRules,
                                  domain::UiDevices{&mode_button_, &action_button_, &display_});
  }

  // ---- フェイク（テスト・デバッグ入力から値や故障を入れる）
  drivers::fake::FakeI2cBus bus;
  drivers::fake::FakeBme280Device bme280_device;
  drivers::fake::FakeBh1750Device bh1750_device;
  drivers::fake::FakeSsd1306Device ssd1306_device;
  drivers::fake::FakeAdc adc;
  drivers::fake::FakePwm pwm;
  drivers::fake::FakeGpio gpio;

 private:
  static constexpr std::array<domain::ChannelSpec, 3> kBmeChannels{{
      {"temp_air", domain::Unit::DegC, 2000},
      {"humidity", domain::Unit::PctRh, 2000},
      {"pressure", domain::Unit::HPa, 2000},
  }};
  static constexpr std::array<domain::ChannelSpec, 1> kLuxChannel{{{"lux", domain::Unit::Lux, 1000}}};
  static constexpr std::array<domain::ChannelSpec, 1> kSoilChannel{{{"soil", domain::Unit::Pct, 10000}}};
  static constexpr domain::ActuatorSpec kFanMain{"fan_main", 0, 1000, {}};
  static constexpr domain::ActuatorSpec kLedGrow{"led_grow", 0, 1000, {}};
  static constexpr domain::ActuatorSpec kPump{"pump", 0, 1000, std::uint16_t{kPumpMaxOnS}};
  static constexpr domain::ActuatorSpec kBuzzer{"buzzer", 0, 1000, {}};

  // ---- 構成表（1行が1台）
  drivers::Bme280 bme280_;
  drivers::Bh1750 bh1750_;
  drivers::SoilMoisture soil_;
  drivers::PwmActuator fan_main_;
  drivers::PwmActuator led_grow_;
  drivers::RelayActuator pump_;
  drivers::PwmActuator buzzer_;
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

  std::array<domain::ISensor*, 3> sensors_;
  std::array<domain::ActuatorBinding, 4> actuators_;
};

}  // namespace node::config
