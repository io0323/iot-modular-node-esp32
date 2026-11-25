// テストとホスト版の --devices fakehw 用の hal のフェイク（I2C バス・ADC・PWM・GPIO・ミューテックス・タイマー）。
// 実機の代わりにレジスタの応答を返し、故障（NACK・タイムアウト・バスのハング）を注入できる。ヒープを使わない。
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/hal.hpp"
#include "support/static_vector.hpp"

namespace node::drivers::fake {

/// @brief フェイクの I2C 機器（FakeI2cBus がアドレスで振り分ける）。
class FakeI2cDevice {
 public:
  virtual ~FakeI2cDevice() = default;
  /// @brief マスターが書いたバイト列を受ける。
  /// @param data 書かれたバイト列
  virtual void on_write(std::span<const std::uint8_t> data) = 0;
  /// @brief マスターが読むバイト列を返す。
  /// @param out 読ませるバイトの書き込み先
  virtual void on_read(std::span<std::uint8_t> out) = 0;
};

/// @brief フェイクの I2C バス。
class FakeI2cBus final : public hal::II2cBus {
 public:
  static constexpr std::size_t kMaxDevices = 4;  ///< つなげる機器の数

  /// @brief 機器をつなぐ。
  /// @param addr 7 ビットのアドレス
  /// @param dev 機器
  /// @return つなげる数を超えたら false
  bool attach(std::uint8_t addr, FakeI2cDevice& dev) {
    return devices_.push_back(Entry{addr, &dev});
  }

  /// @brief このアドレスへの通信を NACK にする（true の間）。
  /// @param addr 7 ビットのアドレス
  /// @param nack NACK にするなら true
  void set_nack(std::uint8_t addr, bool nack) { set_flag(addr, nack, false); }
  /// @brief このアドレスへの通信をタイムアウトにする（true の間）。
  /// @param addr 7 ビットのアドレス
  /// @param timeout タイムアウトにするなら true
  void set_timeout(std::uint8_t addr, bool timeout) { set_flag(addr, timeout, true); }
  /// @brief バスのハング（recover() が呼ばれるまで全アドレスの通信が失敗する）。
  void hang() { hung_ = true; }
  /// @brief 次の n 回の通信を失敗させる。
  /// @param n 回数
  void fail_next(std::uint32_t n) { fail_next_ = n; }

  /// @brief recover() が呼ばれた回数。
  /// @return 回数
  [[nodiscard]] std::uint32_t recover_count() const { return recover_count_; }
  /// @brief 通信の回数（失敗も数える）。
  /// @return 回数
  [[nodiscard]] std::uint32_t transfer_count() const { return transfers_; }
  /// @brief 最後の通信のタイムアウト。
  /// @return タイムアウト（ms）
  [[nodiscard]] std::uint32_t last_timeout_ms() const { return last_timeout_ms_; }

  support::Result<void> write_read(std::uint8_t addr, std::span<const std::uint8_t> tx, std::span<std::uint8_t> rx,
                                   std::uint32_t timeout_ms) override {
    Entry* e = check(addr, timeout_ms);
    if (e == nullptr) {
      return support::make_error(support::ErrorCode::IoError, "i2c nack or timeout");
    }
    if (!tx.empty()) {
      e->dev->on_write(tx);
    }
    if (!rx.empty()) {
      e->dev->on_read(rx);
    }
    return support::ok();
  }
  support::Result<void> write(std::uint8_t addr, std::span<const std::uint8_t> tx,
                              std::uint32_t timeout_ms) override {
    return write_read(addr, tx, {}, timeout_ms);
  }
  support::Result<void> recover() override {
    ++recover_count_;
    hung_ = false;
    return support::ok();
  }

 private:
  struct Entry {
    std::uint8_t addr{0};
    FakeI2cDevice* dev{nullptr};
    bool nack{false};
    bool timeout{false};
  };

  void set_flag(std::uint8_t addr, bool v, bool timeout) {
    for (Entry& e : devices_) {
      if (e.addr != addr) {
        continue;
      }
      (timeout ? e.timeout : e.nack) = v;
    }
  }
  Entry* check(std::uint8_t addr, std::uint32_t timeout_ms) {
    ++transfers_;
    last_timeout_ms_ = timeout_ms;
    if (hung_) {
      return nullptr;
    }
    if (fail_next_ > 0) {
      --fail_next_;
      return nullptr;
    }
    auto* it = std::ranges::find_if(devices_, [addr](const Entry& e) { return e.addr == addr; });
    if (it == devices_.end()) {
      return nullptr;  // つながっていないアドレスは NACK
    }
    return (it->nack || it->timeout) ? nullptr : it;
  }

  support::StaticVector<Entry, kMaxDevices> devices_{};
  bool hung_{false};
  std::uint32_t fail_next_{0};
  std::uint32_t recover_count_{0};
  std::uint32_t transfers_{0};
  std::uint32_t last_timeout_ms_{0};
};

/// @brief フェイクの ADC。
class FakeAdc final : public hal::IAdc {
 public:
  static constexpr std::size_t kChannels = 8;  ///< チャネルの数
  /// @brief 読ませる生値を決める。
  /// @param ch チャネル
  /// @param raw 生値
  void set_raw(hal::Pin ch, std::uint16_t raw) { raw_[ch % kChannels] = raw; }
  /// @brief 読めなくする・戻す。
  /// @param fault 読めなくするなら true
  void set_fault(bool fault) { fault_ = fault; }
  support::Result<std::uint16_t> read_raw(hal::Pin ch) override {
    if (fault_) {
      return support::make_error(support::ErrorCode::IoError, "adc fault");
    }
    return raw_[ch % kChannels];
  }

 private:
  std::array<std::uint16_t, kChannels> raw_{};
  bool fault_{false};
};

/// @brief フェイクの PWM。チャネルごとの設定とデューティを覚える。
class FakePwm final : public hal::IPwm {
 public:
  static constexpr std::size_t kChannels = 8;  ///< チャネルの数
  /// @brief チャネルの設定とデューティ。
  struct Channel {
    std::uint32_t freq_hz{0};          ///< 周波数（Hz）
    std::uint8_t resolution_bits{0};   ///< 分解能（ビット）
    std::uint32_t duty{0};             ///< デューティ
    bool configured{false};            ///< configure を呼んだか
  };
  support::Result<void> configure(hal::Pin ch, std::uint32_t freq_hz, std::uint8_t bits) override {
    Channel& c = ch_[ch % kChannels];
    c.freq_hz = freq_hz;
    c.resolution_bits = bits;
    c.configured = true;
    return support::ok();
  }
  support::Result<void> set_duty(hal::Pin ch, std::uint32_t duty) override {
    Channel& c = ch_[ch % kChannels];
    if (!c.configured) {
      return support::make_error(support::ErrorCode::InvalidArgument, "pwm not configured");
    }
    c.duty = duty;
    return support::ok();
  }
  /// @brief チャネルの設定とデューティ。
  /// @param ch チャネル
  /// @return 設定とデューティ
  [[nodiscard]] const Channel& channel(hal::Pin ch) const { return ch_[ch % kChannels]; }

 private:
  std::array<Channel, kChannels> ch_{};
};

/// @brief フェイクの GPIO。入力の値はテストから決め、出力は覚える。
class FakeGpio final : public hal::IGpio {
 public:
  static constexpr std::size_t kPins = 16;  ///< ピンの数
  /// @brief ピンの向き。
  enum class Mode : std::uint8_t {
    Unset,        ///< 設定していない
    InputPullup,  ///< 内部プルアップつきの入力
    Output,       ///< 出力
  };

  support::Result<void> configure_input_pullup(hal::Pin p) override {
    pins_[p % kPins].mode = Mode::InputPullup;
    pins_[p % kPins].level = true;  // プルアップで High
    return support::ok();
  }
  support::Result<void> configure_output(hal::Pin p) override {
    pins_[p % kPins].mode = Mode::Output;
    return support::ok();
  }
  support::Result<bool> read(hal::Pin p) override { return pins_[p % kPins].level; }
  support::Result<void> write(hal::Pin p, bool high) override {
    if (pins_[p % kPins].mode != Mode::Output) {
      return support::make_error(support::ErrorCode::InvalidArgument);
    }
    pins_[p % kPins].level = high;
    ++pins_[p % kPins].writes;
    return support::ok();
  }
  /// @brief 入力の値を決める（ボタンを押す・離す）。
  /// @param p ピン
  /// @param high High にするなら true
  void set_level(hal::Pin p, bool high) { pins_[p % kPins].level = high; }
  /// @brief ピンの値（入力に決めた値か、最後に出力した値）。
  /// @param p ピン
  /// @return High なら true
  [[nodiscard]] bool level(hal::Pin p) const { return pins_[p % kPins].level; }
  /// @brief ピンの向き。
  /// @param p ピン
  /// @return 向き
  [[nodiscard]] Mode mode(hal::Pin p) const { return pins_[p % kPins].mode; }

 private:
  struct PinState {
    Mode mode{Mode::Unset};
    bool level{false};
    std::uint32_t writes{0};
  };
  std::array<PinState, kPins> pins_{};
};

/// @brief フェイクのミューテックス（1スレッドで使う。取れないようにもできる）。
class FakeMutex final : public hal::IMutex {
 public:
  bool try_lock_for(std::uint32_t timeout_ms) override {
    last_timeout_ms_ = timeout_ms;
    if (locked_ || always_busy_) {
      return false;
    }
    locked_ = true;
    return true;
  }
  void unlock() override { locked_ = false; }
  /// @brief いつも取れないようにする・戻す。
  /// @param v 取れないようにするなら true
  void set_always_busy(bool v) { always_busy_ = v; }
  /// @brief 取られているか。
  /// @return 取られていれば true
  [[nodiscard]] bool locked() const { return locked_; }
  /// @brief 最後の try_lock_for のタイムアウト。
  /// @return タイムアウト（ms）
  [[nodiscard]] std::uint32_t last_timeout_ms() const { return last_timeout_ms_; }

 private:
  bool locked_{false};
  bool always_busy_{false};
  std::uint32_t last_timeout_ms_{0};
};

/// @brief フェイクのタイマー。delay_ms は待たずに時刻を進める。
class FakeTimer final : public hal::ITimer {
 public:
  [[nodiscard]] std::uint64_t now_ms() const override { return now_ms_; }
  void delay_ms(std::uint32_t ms) override {
    now_ms_ += ms;
    total_delay_ms_ += ms;
  }
  /// @brief delay_ms で待った時間の合計。
  /// @return 合計（ms）
  [[nodiscard]] std::uint64_t total_delay_ms() const { return total_delay_ms_; }

 private:
  std::uint64_t now_ms_{0};
  std::uint64_t total_delay_ms_{0};
};

}  // namespace node::drivers::fake
