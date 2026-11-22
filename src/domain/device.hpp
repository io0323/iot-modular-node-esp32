#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>

#include "domain/interlock.hpp"
#include "domain/rule.hpp"
#include "domain/specs.hpp"
#include "support/result.hpp"

namespace node::domain {

/// @brief センサーの共通の契約（基本設計書 第5章）。1台で複数のチャネルを持てる。
class ISensor {
 public:
  virtual ~ISensor() = default;
  /// @brief 機器の ID。
  [[nodiscard]] virtual std::string_view id() const = 0;
  /// @brief 初期化する（故障中の再初期化にも使う）。
  virtual support::Result<void> init() = 0;
  /// @brief 計測する。
  /// @param out 計測値の書き込み先（channels().size() 以上の長さ）
  /// @return 書いた件数
  virtual support::Result<std::size_t> read(std::span<Reading> out) = 0;
  /// @brief 計測の周期（通常のモード）。
  [[nodiscard]] virtual std::chrono::milliseconds period() const = 0;
  /// @brief この機器が持つチャネル。
  [[nodiscard]] virtual std::span<const ChannelSpec> channels() const = 0;
};

/// @brief アクチュエーターの共通の契約（基本設計書 第5章）。
class IActuator {
 public:
  virtual ~IActuator() = default;
  /// @brief 機器の ID。
  [[nodiscard]] virtual std::string_view id() const = 0;
  /// @brief 初期化する。
  virtual support::Result<void> init() = 0;
  /// @brief 出力する。出力は ControlArbiter だけが呼ぶ。
  /// @param level 0–1000‰
  virtual support::Result<void> apply(Level level) = 0;
  /// @brief いまの出力。
  [[nodiscard]] virtual Level current() const = 0;
  /// @brief 上限・下限・連続出力の上限などの仕様。
  [[nodiscard]] virtual const ActuatorSpec& spec() const = 0;
};

/// @brief アクチュエーターと、それにかけるインターロックの組（構成表の1行）。
struct ActuatorBinding {
  IActuator* actuator{nullptr};
  std::span<IInterlock* const> interlocks{};
};

/// @brief 機器の一覧。インスタンスは静的に確保し、ここは参照だけを持つ（基本設計書 第5章）。
class DeviceRegistry {
 public:
  /// @brief 一覧を作る。
  /// @param sensors センサーへのポインタの並び（寿命はプログラム全体）
  /// @param actuators アクチュエーターとインターロックの組の並び（寿命はプログラム全体）
  /// @param rules ローカルルールの並び（寿命はプログラム全体）
  DeviceRegistry(std::span<ISensor* const> sensors, std::span<const ActuatorBinding> actuators,
                 std::span<const Rule> rules = {})
      : sensors_(sensors), actuators_(actuators), rules_(rules) {}

  [[nodiscard]] std::span<ISensor* const> sensors() const { return sensors_; }
  [[nodiscard]] std::span<const ActuatorBinding> actuators() const { return actuators_; }
  [[nodiscard]] std::span<const Rule> rules() const { return rules_; }

 private:
  std::span<ISensor* const> sensors_;
  std::span<const ActuatorBinding> actuators_;
  std::span<const Rule> rules_;
};

}  // namespace node::domain
