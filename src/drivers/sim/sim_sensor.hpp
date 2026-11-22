#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "support/static_vector.hpp"

namespace node::drivers {

/// @brief ホスト用の模擬センサー。決まった値を返す。1台で複数のチャネルを持てる（BME280 に当たるものは3チャネル）。
class SimSensor final : public domain::ISensor {
 public:
  static constexpr std::size_t kMaxChannels = 4;

  /// @brief 模擬センサーを作る。
  /// @param id 機器の ID
  /// @param channels チャネル（寿命はプログラム全体）
  /// @param values_milli チャネルごとの値（1/1000 単位。channels と同じ長さ）
  /// @param period 計測の周期
  SimSensor(std::string_view id, std::span<const domain::ChannelSpec> channels,
            std::span<const std::int32_t> values_milli, std::chrono::milliseconds period);

  [[nodiscard]] std::string_view id() const override { return id_; }
  support::Result<void> init() override { return support::ok(); }
  support::Result<std::size_t> read(std::span<domain::Reading> out) override;
  [[nodiscard]] std::chrono::milliseconds period() const override { return period_; }
  [[nodiscard]] std::span<const domain::ChannelSpec> channels() const override { return channels_; }

  /// @brief 値を変える（テスト・シナリオ用）。
  void set_value(std::size_t index, std::int32_t value_milli);
  /// @brief 品質を変える（テスト・シナリオ用）。
  void set_quality(domain::Quality q) { quality_ = q; }

 private:
  std::string_view id_;
  std::span<const domain::ChannelSpec> channels_;
  support::StaticVector<std::int32_t, kMaxChannels> values_{};
  std::chrono::milliseconds period_;
  domain::Quality quality_{domain::Quality::Good};
};

/// @brief ホスト用の模擬アクチュエーター。受けた level を覚えて返すだけ。
class SimActuator final : public domain::IActuator {
 public:
  /// @brief 模擬アクチュエーターを作る。
  /// @param spec 仕様（寿命はプログラム全体）
  explicit SimActuator(const domain::ActuatorSpec& spec) : spec_(spec) {}

  [[nodiscard]] std::string_view id() const override { return spec_.id; }
  support::Result<void> init() override { return support::ok(); }
  support::Result<void> apply(domain::Level level) override {
    level_ = level;
    ++apply_count_;
    return support::ok();
  }
  [[nodiscard]] domain::Level current() const override { return level_; }
  [[nodiscard]] const domain::ActuatorSpec& spec() const override { return spec_; }
  [[nodiscard]] std::uint32_t apply_count() const { return apply_count_; }

 private:
  const domain::ActuatorSpec& spec_;
  domain::Level level_{0};
  std::uint32_t apply_count_{0};
};

}  // namespace node::drivers
