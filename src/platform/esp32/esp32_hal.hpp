// hal の ESP-IDF v5.5.6 での実装（I2C：i2c_master、ADC：adc_oneshot、PWM：LEDC、GPIO、ミューテックス：FreeRTOS、
// タイマー：esp_timer と vTaskDelay）。P6 の決定 #105・#106・#113。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "config/esp32_pins.hpp"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/hal.hpp"
#include "platform/esp32/rtos.hpp"
#include "support/static_vector.hpp"

namespace node::platform::esp32 {

/// @brief I2C0 のマスター（i2c_master）。アドレスごとのデバイスは最初に使うときに足す。
class EspI2cBus final : public hal::II2cBus {
 public:
  static constexpr std::uint32_t kSclSpeedHz = 400000;  ///< fast mode（#113）
  static constexpr std::uint32_t kRecoveryClocks = 15;  ///< バスの回復で出す SCL のクロック（基本設計書 第7章・#106）
  static constexpr std::size_t kMaxDevices = 4;         ///< アドレスの数

  /// @brief ピンを受け取る（バスは init で作る）。
  /// @param sda_gpio SDA の GPIO
  /// @param scl_gpio SCL の GPIO
  EspI2cBus(int sda_gpio, int scl_gpio) : sda_(sda_gpio), scl_(scl_gpio) {}

  /// @brief バスを作る。
  /// @return 作れなければ失敗
  support::Result<void> init();

  support::Result<void> write_read(std::uint8_t addr, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx,
                                   std::uint32_t timeout_ms) override;
  support::Result<void> write(std::uint8_t addr, std::span<const std::uint8_t> tx, std::uint32_t timeout_ms) override;
  support::Result<void> recover() override;

 private:
  struct Device {
    std::uint8_t addr{0};
    i2c_master_dev_handle_t handle{nullptr};
  };
  i2c_master_dev_handle_t device(std::uint8_t addr);
  void delete_bus();

  int sda_;
  int scl_;
  i2c_master_bus_handle_t bus_{nullptr};
  support::StaticVector<Device, kMaxDevices> devices_{};
};

/// @brief ADC1 のワンショット（減衰 12dB・既定の分解能。#113）。
class EspAdc final : public hal::IAdc {
 public:
  /// @brief 論理名と GPIO の表を受け取る（ユニットは init で作る）。
  /// @param pins 論理名と GPIO
  explicit EspAdc(std::span<const config::esp32::PinMap> pins) : pins_(pins) {}
  /// @brief ADC1 のユニットとチャネルを作る。
  /// @return 作れなければ失敗
  support::Result<void> init();
  support::Result<std::uint16_t> read_raw(hal::Pin channel) override;

 private:
  std::span<const config::esp32::PinMap> pins_;
  adc_oneshot_unit_handle_t unit_{nullptr};
};

/// @brief LEDC（低速モード。PWM のチャネルごとにタイマーを1つ。#44・#58・#113）。
class EspPwm final : public hal::IPwm {
 public:
  /// @brief 論理名（LEDC のチャネル）と GPIO の表を受け取る。
  /// @param pins 論理名と GPIO
  explicit EspPwm(std::span<const config::esp32::PinMap> pins) : pins_(pins) {}
  support::Result<void> configure(hal::Pin channel, std::uint32_t freq_hz, std::uint8_t resolution_bits) override;
  support::Result<void> set_duty(hal::Pin channel, std::uint32_t duty) override;

 private:
  std::span<const config::esp32::PinMap> pins_;
};

/// @brief GPIO（論理名を表で番号に変える）。
class EspGpio final : public hal::IGpio {
 public:
  /// @brief 論理名と GPIO の表を受け取る。
  /// @param pins 論理名と GPIO
  explicit EspGpio(std::span<const config::esp32::PinMap> pins) : pins_(pins) {}
  support::Result<void> configure_input_pullup(hal::Pin pin) override;
  support::Result<void> configure_output(hal::Pin pin) override;
  support::Result<bool> read(hal::Pin pin) override;
  support::Result<void> write(hal::Pin pin, bool high) override;

 private:
  [[nodiscard]] int gpio_of(hal::Pin pin) const;
  std::span<const config::esp32::PinMap> pins_;
};

/// @brief FreeRTOS のミューテックス（静的に確保）。
class EspMutex final : public hal::IMutex {
 public:
  EspMutex() : handle_(rtos::mutex_create_static(&storage_)) {}
  EspMutex(const EspMutex&) = delete;
  EspMutex& operator=(const EspMutex&) = delete;
  EspMutex(EspMutex&&) = delete;
  EspMutex& operator=(EspMutex&&) = delete;
  ~EspMutex() override = default;

  bool try_lock_for(std::uint32_t timeout_ms) override;
  void unlock() override;

 private:
  StaticSemaphore_t storage_{};
  SemaphoreHandle_t handle_;
};

/// @brief esp_timer の時刻と vTaskDelay の待ち。
class EspTimer final : public hal::ITimer {
 public:
  [[nodiscard]] std::uint64_t now_ms() const override;
  void delay_ms(std::uint32_t ms) override;
};

/// @brief ms を FreeRTOS の tick に変える（pdMS_TO_TICKS と同じ計算。マクロの古い形のキャストを避ける）。
/// @param ms ms
/// @return tick
TickType_t ticks_of(std::uint32_t ms);

}  // namespace node::platform::esp32
