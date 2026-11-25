#pragma once

#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>

#include "domain/interlock.hpp"
#include "domain/rule.hpp"
#include "domain/specs.hpp"
#include "domain/ui.hpp"
#include "support/result.hpp"

namespace node::domain {

/// @brief センサーの共通の契約（基本設計書 第5章）。1台で複数のチャネルを持てる。
class ISensor {
 public:
  virtual ~ISensor() = default;
  /// @brief 機器の ID。
  /// @return ID（例："bme280"）
  [[nodiscard]] virtual std::string_view id() const = 0;
  /// @brief 初期化する（故障中の再初期化にも使う）。
  /// @return 初期化できなければ失敗
  virtual support::Result<void> init() = 0;
  /// @brief 計測する。
  /// @param out 計測値の書き込み先（channels().size() 以上の長さ）
  /// @return 書いた件数。読めなければ失敗
  virtual support::Result<std::size_t> read(std::span<Reading> out) = 0;
  /// @brief 計測の周期（通常のモード）。
  /// @return 周期
  [[nodiscard]] virtual std::chrono::milliseconds period() const = 0;
  /// @brief この機器が持つチャネル。
  /// @return チャネルの仕様の並び
  [[nodiscard]] virtual std::span<const ChannelSpec> channels() const = 0;
  /// @brief 続けて読めなかったときの回復を要求する（I2C の機器はバスの回復。P3 の決定 #48、#55）。既定は何もしない。
  /// @return 回復できなければ失敗
  virtual support::Result<void> request_recovery() { return support::ok(); }
};

/// @brief アクチュエーターの共通の契約（基本設計書 第5章）。
class IActuator {
 public:
  virtual ~IActuator() = default;
  /// @brief 機器の ID。
  /// @return ID（例："fan_main"）
  [[nodiscard]] virtual std::string_view id() const = 0;
  /// @brief 初期化する。
  /// @return 初期化できなければ失敗
  virtual support::Result<void> init() = 0;
  /// @brief 出力する。出力は ControlArbiter だけが呼ぶ。
  /// @param level 0–1000‰
  /// @return 出力できなければ失敗
  virtual support::Result<void> apply(Level level) = 0;
  /// @brief いまの出力。
  /// @return 最後に出力した値（0–1000‰）
  [[nodiscard]] virtual Level current() const = 0;
  /// @brief 上限・下限・連続出力の上限などの仕様。
  /// @return 仕様
  [[nodiscard]] virtual const ActuatorSpec& spec() const = 0;
};

/// @brief アクチュエーターと、それにかけるインターロックの組（構成表の1行）。
struct ActuatorBinding {
  IActuator* actuator{nullptr};            ///< アクチュエーター
  std::span<IInterlock* const> interlocks;  ///< かけるインターロック（なければ空）
};

/// @brief 機器の一覧。インスタンスは静的に確保し、ここは参照だけを持つ（基本設計書 第5章）。
class DeviceRegistry {
 public:
  /// @brief 一覧を作る。
  /// @param sensors センサーへのポインタの並び（寿命はプログラム全体）
  /// @param actuators アクチュエーターとインターロックの組の並び（寿命はプログラム全体）
  /// @param rules ローカルルールの並び（寿命はプログラム全体）
  /// @param ui 本体の UI の機器（ボタン・OLED。なければ空）
  DeviceRegistry(std::span<ISensor* const> sensors, std::span<const ActuatorBinding> actuators,
                 std::span<const Rule> rules = {}, const UiDevices& ui = {})
      : sensors_(sensors), actuators_(actuators), rules_(rules), ui_(ui) {}

  /// @brief センサー。
  /// @return センサーへのポインタの並び
  [[nodiscard]] std::span<ISensor* const> sensors() const { return sensors_; }
  /// @brief アクチュエーター。
  /// @return アクチュエーターとインターロックの組の並び
  [[nodiscard]] std::span<const ActuatorBinding> actuators() const { return actuators_; }
  /// @brief ローカルルール。
  /// @return ルールの並び
  [[nodiscard]] std::span<const Rule> rules() const { return rules_; }
  /// @brief 本体の UI の機器。
  /// @return ボタン・OLED（なければ空）
  [[nodiscard]] const UiDevices& ui() const { return ui_; }

 private:
  std::span<ISensor* const> sensors_;
  std::span<const ActuatorBinding> actuators_;
  std::span<const Rule> rules_;
  UiDevices ui_;
};

}  // namespace node::domain
