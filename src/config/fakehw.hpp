// 構成表の実機用の行（config/hw.hpp の HwRig）を、フェイクの hal・機器につないだ組み合わせ（ホスト版の --devices fakehw、
// ホスト結合テスト）。
#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include "config/devices.hpp"
#include "config/hw.hpp"
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

/// @brief ドライバとフェイクの組み合わせ一式。状態を持つので、使う側（main・テスト）が1つずつ作る。
class FakeHwRig {
 public:
  /// @param timer ドライバの待ち時間（ホスト版は実時間、テストは FakeTimer）
  /// @param i2c_mutex I2C バスの排他
  FakeHwRig(hal::ITimer& timer, hal::IMutex& i2c_mutex);
  FakeHwRig(const FakeHwRig&) = delete;
  FakeHwRig& operator=(const FakeHwRig&) = delete;
  FakeHwRig(FakeHwRig&&) = delete;
  FakeHwRig& operator=(FakeHwRig&&) = delete;
  ~FakeHwRig() = default;

  /// @brief 機器の一覧（Node に渡す）。
  /// @return センサー・アクチュエーター・ルール・UI の機器
  [[nodiscard]] domain::DeviceRegistry registry() { return hw_.registry(); }

  // ---- フェイク（テスト・デバッグ入力から値や故障を入れる）
  /// @brief I2C バスのフェイク。
  /// @return バス
  drivers::fake::FakeI2cBus& bus() { return bus_; }
  /// @brief BME280 のフェイク。
  /// @return 機器
  drivers::fake::FakeBme280Device& bme280_device() { return bme280_device_; }
  /// @brief BH1750 のフェイク。
  /// @return 機器
  drivers::fake::FakeBh1750Device& bh1750_device() { return bh1750_device_; }
  /// @brief SSD1306 のフェイク。
  /// @return 機器
  drivers::fake::FakeSsd1306Device& ssd1306_device() { return ssd1306_device_; }
  /// @brief ADC のフェイク。
  /// @return ADC
  drivers::fake::FakeAdc& adc() { return adc_; }
  /// @brief PWM のフェイク。
  /// @return PWM
  drivers::fake::FakePwm& pwm() { return pwm_; }
  /// @brief PWM のフェイク（読むだけ）。
  /// @return PWM
  [[nodiscard]] const drivers::fake::FakePwm& pwm() const { return pwm_; }
  /// @brief GPIO のフェイク。
  /// @return GPIO
  drivers::fake::FakeGpio& gpio() { return gpio_; }

 private:
  drivers::fake::FakeI2cBus bus_;
  drivers::fake::FakeBme280Device bme280_device_;
  drivers::fake::FakeBh1750Device bh1750_device_;
  drivers::fake::FakeSsd1306Device ssd1306_device_;
  drivers::fake::FakeAdc adc_;
  drivers::fake::FakePwm pwm_;
  drivers::fake::FakeGpio gpio_;

  // ---- 構成表の実機用の行（ターゲット版と同じ表）
  HwRig hw_;
};

}  // namespace node::config
