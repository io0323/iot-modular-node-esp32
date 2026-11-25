#pragma once

#include <array>
#include <string_view>

#include "infra/envelope.hpp"
#include "infra/mqtt_link.hpp"
#include "support/fixed_string.hpp"

namespace node::infra {

/// @brief トピック（iot/v1/{site}/{device_id}/{kind}）と、kind ごとの QoS・retain（共通仕様 第2章）。
class Topics {
 public:
  using TopicString = support::FixedString<96>;
  static constexpr std::string_view kDefaultSite = "home";

  /// @brief トピックを組み立てる。
  /// @param site サイト名（既定 "home"）
  /// @param device_id デバイス ID
  Topics(std::string_view site, std::string_view device_id);

  /// @brief 発行するトピック。
  [[nodiscard]] std::string_view of(Kind k) const { return topics_[index_of(k)].view(); }
  /// @brief 購読する cmd のトピック。
  [[nodiscard]] std::string_view cmd() const { return cmd_.view(); }
  /// @brief 組み立てられたか（長すぎる site などで失敗する）。
  [[nodiscard]] bool ok() const { return ok_; }

  /// @brief kind ごとの QoS（telemetry だけ 0）。
  static constexpr Qos qos_of(Kind k) { return k == Kind::Telemetry ? Qos::AtMostOnce : Qos::AtLeastOnce; }
  /// @brief kind ごとの retain（state だけ retain）。
  static constexpr bool retain_of(Kind k) { return k == Kind::State; }

 private:
  std::array<TopicString, kKindCount> topics_{};
  TopicString cmd_;
  bool ok_{true};
};

}  // namespace node::infra
