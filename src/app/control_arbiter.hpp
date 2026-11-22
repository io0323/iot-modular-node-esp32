#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "domain/lease.hpp"
#include "domain/pump_interlock.hpp"
#include "domain/types.hpp"
#include "support/static_vector.hpp"

namespace node::app {

/// @brief アクチュエーターへの出力を決める唯一の場所（基本設計書 第6章「制御の調停」）。
///
/// 仮版（P2 で5段にする）：アクチュエーターごとに「インターロック ＞ 上位指令（リース中）＞ 既定値（0）」で
/// 出力と owner を決める。同じ対象への新しい set_actuator は priority によらず古い指令を置き換える。
class ControlArbiter {
 public:
  static constexpr std::size_t kMaxActuators = 8;

  /// @brief インターロックの遷移（イベントの元）。
  struct InterlockChange {
    std::string_view id;
    domain::PumpInterlock::Transition transition{domain::PumpInterlock::Transition::None};
  };
  using InterlockChanges = support::StaticVector<InterlockChange, kMaxActuators>;

  /// @brief set_command の結果（ack の applied・limited）。
  struct CommandResult {
    domain::Level applied{0};
    bool limited{false};
  };

  /// @brief 調停の対象を登録する。全アクチュエーターを0にして始める（起動直後は停止）。
  /// @param actuators アクチュエーター（kMaxActuators 台まで）
  explicit ControlArbiter(std::span<domain::IActuator* const> actuators);

  /// @brief ID からアクチュエーターの番号を探す。
  [[nodiscard]] std::optional<std::size_t> find(std::string_view id) const;

  /// @brief 上位指令を登録し、すぐに出力を決める。
  /// @param index アクチュエーターの番号
  /// @param level 要求（min〜max に収める）
  /// @param priority cmd の priority（owner になる）
  /// @param lease_s リースの秒数
  /// @param now_ms 受信時刻（単調時計の ms）
  /// @param changes インターロックの遷移の書き込み先
  /// @return 出力した値と、安全制限で丸めたか
  CommandResult set_command(std::size_t index, domain::Level level, domain::Priority priority,
                            std::uint32_t lease_s, std::uint64_t now_ms, InterlockChanges& changes);

  /// @brief 上位指令を解除し、すぐに出力を決める。
  void release(std::size_t index, std::uint64_t now_ms, InterlockChanges& changes);

  /// @brief リースの期限とインターロックを見て、全アクチュエーターの出力を決め直す。
  void tick(std::uint64_t now_ms, InterlockChanges& changes);

  [[nodiscard]] std::size_t size() const { return slots_.size(); }
  [[nodiscard]] const domain::ActuatorSpec& spec(std::size_t i) const { return slots_[i].actuator->spec(); }
  [[nodiscard]] domain::Level output(std::size_t i) const { return slots_[i].output; }
  [[nodiscard]] domain::Owner owner(std::size_t i) const { return slots_[i].owner; }
  /// @brief 出力に失敗した回数（診断用）。
  [[nodiscard]] std::uint32_t apply_errors() const { return apply_errors_; }

 private:
  struct Slot {
    domain::IActuator* actuator{nullptr};
    std::optional<domain::PumpInterlock> interlock{};
    bool has_lease{false};
    domain::Level lease_level{0};
    domain::Priority priority{domain::Priority::Auto};
    domain::Lease lease{};
    domain::Level output{0};
    domain::Owner owner{domain::Owner::None};
  };

  void evaluate(Slot& s, std::uint64_t now_ms, InterlockChanges& changes);

  support::StaticVector<Slot, kMaxActuators> slots_{};
  std::uint32_t apply_errors_{0};
};

}  // namespace node::app
