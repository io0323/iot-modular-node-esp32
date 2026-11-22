// ハードのインターフェース（hal）。自作 iot-hal の取り込み元が決まったら置き換える（docs/open-questions.md #5）。
// 依存先は support だけ。実装は platform（ESP-IDF は P6、ホスト版）と drivers/fake（テスト）が持つ。
// P6 で ESP-IDF v5 の i2c_master・adc_oneshot・LEDC・gpio・esp_timer・FreeRTOS のミューテックスに素直に対応させる。
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "support/result.hpp"

namespace node::hal {

/// @brief 論理的なピン・チャネルの番号（実際のピン番号は P6 で構成表に決める）。
using Pin = std::uint8_t;

/// @brief 1回の I2C 通信のタイムアウトの既定（P3 の決定 #48）。
inline constexpr std::uint32_t kI2cTimeoutMs = 50;
/// @brief I2C バスのミューテックスの取得のタイムアウト（P3 の決定 #49）。
inline constexpr std::uint32_t kI2cLockTimeoutMs = 100;

/// @brief I2C バス（マスター）。アドレスは 7 ビット。
class II2cBus {
 public:
  virtual ~II2cBus() = default;
  /// @brief 書いてから読む（tx が空なら読むだけ、rx が空なら書くだけ）。
  /// @param addr 7 ビットのアドレス
  /// @param tx 書くバイト列
  /// @param rx 読んだバイトの書き込み先
  /// @param timeout_ms タイムアウト
  /// @return NACK・タイムアウトなどは失敗（ErrorCode::IoError）
  virtual support::Result<void> write_read(std::uint8_t addr, std::span<const std::uint8_t> tx,
                                           std::span<std::uint8_t> rx, std::uint32_t timeout_ms) = 0;
  /// @brief 書く。
  virtual support::Result<void> write(std::uint8_t addr, std::span<const std::uint8_t> tx,
                                      std::uint32_t timeout_ms) = 0;
  /// @brief バスを回復させる（SCL を送ってスレーブの止まった状態を解く。基本設計書 第7章）。
  virtual support::Result<void> recover() = 0;
};

/// @brief ADC（ワンショットで生値を読む）。
class IAdc {
 public:
  virtual ~IAdc() = default;
  /// @brief 生値を読む。
  virtual support::Result<std::uint16_t> read_raw(Pin channel) = 0;
};

/// @brief PWM（ESP32 の LEDC に当たる）。
class IPwm {
 public:
  virtual ~IPwm() = default;
  /// @brief チャネルの周波数と分解能を決める。
  virtual support::Result<void> configure(Pin channel, std::uint32_t freq_hz, std::uint8_t resolution_bits) = 0;
  /// @brief デューティを決める（0〜2^分解能−1）。
  virtual support::Result<void> set_duty(Pin channel, std::uint32_t duty) = 0;
};

/// @brief GPIO。
class IGpio {
 public:
  virtual ~IGpio() = default;
  virtual support::Result<void> configure_input_pullup(Pin pin) = 0;
  virtual support::Result<void> configure_output(Pin pin) = 0;
  /// @brief 読む（true が High）。
  virtual support::Result<bool> read(Pin pin) = 0;
  /// @brief 書く（true が High）。
  virtual support::Result<void> write(Pin pin, bool high) = 0;
};

/// @brief ミューテックス（I2C バスの排他に使う）。
class IMutex {
 public:
  virtual ~IMutex() = default;
  /// @brief 取る。timeout_ms のうちに取れなければ false。
  virtual bool try_lock_for(std::uint32_t timeout_ms) = 0;
  virtual void unlock() = 0;
};

/// @brief 時間の取り口（ドライバの待ち時間に使う。open-questions #54）。
class ITimer {
 public:
  virtual ~ITimer() = default;
  /// @brief 単調時計（ms）。
  [[nodiscard]] virtual std::uint64_t now_ms() const = 0;
  /// @brief ms だけ待つ（ESP-IDF では vTaskDelay。フェイクは待たずに時刻を進める）。
  virtual void delay_ms(std::uint32_t ms) = 0;
};

/// @brief I2C の1回の取引（基本設計書 第5章）。生成時にバスのミューテックスを取り、破棄時に放す（RAII）。
///
/// ミューテックスが100ms のうちに取れなければ locked() が false で、通信は ErrorCode::Busy で失敗する（#49）。
class I2cTransaction {
 public:
  /// @brief ミューテックスを取る。
  I2cTransaction(II2cBus& bus, IMutex& mutex, std::uint32_t lock_timeout_ms = kI2cLockTimeoutMs)
      : bus_(bus), mutex_(mutex), locked_(mutex.try_lock_for(lock_timeout_ms)) {}
  /// @brief 取っていれば放す。
  ~I2cTransaction() {
    if (locked_) mutex_.unlock();
  }
  I2cTransaction(const I2cTransaction&) = delete;
  I2cTransaction& operator=(const I2cTransaction&) = delete;
  I2cTransaction(I2cTransaction&&) = delete;
  I2cTransaction& operator=(I2cTransaction&&) = delete;

  /// @brief ミューテックスを取れたか。
  [[nodiscard]] bool locked() const { return locked_; }

  /// @brief 書いてから読む。
  support::Result<void> write_read(std::uint8_t addr, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx,
                                   std::uint32_t timeout_ms = kI2cTimeoutMs) {
    if (!locked_) return support::make_error(support::ErrorCode::Busy, "i2c bus lock timeout");
    return bus_.write_read(addr, tx, rx, timeout_ms);
  }
  /// @brief 書く。
  support::Result<void> write(std::uint8_t addr, std::span<const std::uint8_t> tx,
                              std::uint32_t timeout_ms = kI2cTimeoutMs) {
    if (!locked_) return support::make_error(support::ErrorCode::Busy, "i2c bus lock timeout");
    return bus_.write(addr, tx, timeout_ms);
  }
  /// @brief バスを回復させる。
  support::Result<void> recover() {
    if (!locked_) return support::make_error(support::ErrorCode::Busy, "i2c bus lock timeout");
    return bus_.recover();
  }

 private:
  II2cBus& bus_;
  IMutex& mutex_;
  bool locked_;
};

/// @brief ドライバが持つ I2C 機器の宛先（バス・ミューテックス・アドレス）。
struct I2cDevice {
  II2cBus* bus{nullptr};
  IMutex* mutex{nullptr};
  std::uint8_t addr{0};
};

}  // namespace node::hal
