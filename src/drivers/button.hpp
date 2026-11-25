// タクトスイッチ（GPIO）のドライバ。
//
// 出典：データシートのある部品ではない。判定の決まりは P3 の決定（open-questions #41・#42）：
// 20ms ごとに読み、同じ値が3回続いたら（60ms）押した・離したと判定する。押してから 1,000ms 未満で離したら短押し、
// 1,000ms 押し続けた時点で長押し（離すのを待たない。長押しの後に離しても短押しにしない）。
// 押すと Low（内部プルアップ）。構成で反転できる。
#pragma once

#include <cstdint>

#include "domain/ui.hpp"
#include "hal/hal.hpp"

namespace node::drivers {

/// @brief ボタン。poll を 20ms ごとに呼ぶ。
class Button final : public domain::IButton {
 public:
  static constexpr std::uint32_t kStableSamples = 3;
  static constexpr std::uint64_t kLongPressMs = 1000;

  /// @param active_low 押すと Low なら true（既定。内部プルアップ）
  Button(hal::IGpio& gpio, hal::Pin pin, bool active_low = true) : gpio_(gpio), pin_(pin), active_low_(active_low) {}

  support::Result<void> init() override { return gpio_.configure_input_pullup(pin_); }

  domain::ButtonEvent poll(std::uint64_t now_ms) override {
    const auto high = gpio_.read(pin_);
    if (!high) {
      return domain::ButtonEvent::None;  // 読めなければ判定を進めない
    }
    const bool raw = high.value() != active_low_;  // 押されているか
    if (debounce(raw)) {
      pressed_ = candidate_;
      if (const domain::ButtonEvent e = on_edge(now_ms); e != domain::ButtonEvent::None) {
        return e;
      }
    }
    if (pressed_ && !long_fired_ && now_ms - pressed_at_ms_ >= kLongPressMs) {
      long_fired_ = true;
      return domain::ButtonEvent::Long;
    }
    return domain::ButtonEvent::None;
  }

 private:
  // 同じ値が kStableSamples 回続いて、押した・離したの判定が変わるなら true
  bool debounce(bool raw) {
    if (raw == candidate_) {
      if (samples_ < kStableSamples) {
        ++samples_;
      }
    } else {
      candidate_ = raw;
      samples_ = 1;
    }
    return samples_ >= kStableSamples && candidate_ != pressed_;
  }

  // 押した・離したの判定が変わった（pressed_ は新しい値）
  domain::ButtonEvent on_edge(std::uint64_t now_ms) {
    if (pressed_) {
      pressed_at_ms_ = now_ms;
      long_fired_ = false;
      return domain::ButtonEvent::None;
    }
    if (long_fired_) {
      return domain::ButtonEvent::None;  // 長押しの後に離しても短押しにしない
    }
    // 1,000ms 未満で離したら短押し。離した判定と 1,000ms が同じ読み取りに重なったら長押し
    return now_ms - pressed_at_ms_ < kLongPressMs ? domain::ButtonEvent::Short : domain::ButtonEvent::Long;
  }

  hal::IGpio& gpio_;
  hal::Pin pin_;
  bool active_low_;
  bool candidate_{false};
  std::uint32_t samples_{0};
  bool pressed_{false};
  std::uint64_t pressed_at_ms_{0};
  bool long_fired_{false};
};

}  // namespace node::drivers
