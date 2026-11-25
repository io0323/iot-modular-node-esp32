#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "domain/device.hpp"
#include "domain/interlock.hpp"
#include "domain/lease.hpp"
#include "domain/reading_store.hpp"
#include "domain/types.hpp"
#include "support/static_vector.hpp"

namespace node::app {

/// @brief アクチュエーターへの出力を決める唯一の場所（基本設計書 第6章「制御の調停」）。
///
/// アクチュエーターごとに「インターロック ＞ 上位指令（リース中）＞ 本体の手動操作（手動モードのときだけ）
/// ＞ ローカルルール（自動・省電力モードのときだけ）＞ 既定値（0）」で出力を決める。
/// インターロックの強制値・上限はどの段の結果にもかける。インターロックで値を変えたら owner は interlock。
/// 具体的なインターロックのクラスや機器 ID は知らない（IInterlock と構成表だけで扱う）。
class ControlArbiter {
 public:
  static constexpr std::size_t kMaxActuators = 8;              ///< 扱えるアクチュエーターの数
  static constexpr std::size_t kMaxInterlocksPerActuator = 4;  ///< 1台にかけられるインターロックの数

  /// @brief インターロックの遷移（INTERLOCK_ACTIVE／RELEASED の元）。
  struct InterlockChange {
    std::string_view id;    ///< アクチュエーター
    std::string_view rule;  ///< インターロックの rule
    bool active{false};     ///< true：効いて出力を変えた、false：解けた
  };
  /// @brief 1周分のインターロックの遷移。
  using InterlockChanges = support::StaticVector<InterlockChange, kMaxActuators * kMaxInterlocksPerActuator>;

  /// @brief set_command の結果（ack の applied・limited）。
  struct CommandResult {
    domain::Level applied{0};  ///< 出力した値
    bool limited{false};       ///< 安全制限で丸めたか
  };

  /// @brief 調停の対象を登録する。全アクチュエーターを0にして始める（起動直後は停止）。
  /// @param actuators アクチュエーターとインターロックの組（kMaxActuators 台まで）
  /// @param store 最新の計測値（インターロックの入力）
  ControlArbiter(std::span<const domain::ActuatorBinding> actuators, const domain::ReadingStore& store);

  /// @brief ID からアクチュエーターの番号を探す。
  /// @param id アクチュエーターの ID
  /// @return 番号。なければ std::nullopt
  [[nodiscard]] std::optional<std::size_t> find(std::string_view id) const;

  /// @brief 上位指令を登録し、すぐに出力を決める。
  /// @param index アクチュエーターの番号
  /// @param level 出力（0–1000‰。範囲外は丸める）
  /// @param priority 指令の priority
  /// @param lease_s リースの秒数
  /// @param now_ms いまの単調時計
  /// @param changes インターロックの遷移の書き込み先
  /// @return 出力した値と、安全制限で丸めたか（範囲外・インターロック・lease_s が max_on_s を超える）
  CommandResult set_command(std::size_t index, domain::Level level, domain::Priority priority,
                            std::uint32_t lease_s, std::uint64_t now_ms, InterlockChanges& changes);
  /// @brief 上位指令を解除し、すぐに出力を決める。
  /// @param index アクチュエーターの番号
  /// @param now_ms いまの単調時計
  /// @param changes インターロックの遷移の書き込み先
  void release(std::size_t index, std::uint64_t now_ms, InterlockChanges& changes);

  /// @brief 本体の手動操作の値を決める（std::nullopt で消す）。手動の段が有効なときだけ効く。
  /// @param index アクチュエーターの番号
  /// @param level 出力（0–1000‰）
  void set_manual(std::size_t index, std::optional<domain::Level> level);
  /// @brief 本体の手動操作の値をすべて消す。
  void clear_manual();
  /// @brief ローカルルールの提案を決める（std::nullopt で提案なし）。ルールの段が有効なときだけ効く。
  /// @param index アクチュエーターの番号
  /// @param level 提案の出力（0–1000‰）
  void set_rule_proposal(std::size_t index, std::optional<domain::Level> level);
  /// @brief どの段を有効にするか（モードで決まる）。
  /// @param manual_enabled 手動の段を有効にするか
  /// @param rules_enabled ルールの段を有効にするか
  void set_stages(bool manual_enabled, bool rules_enabled);

  /// @brief このアクチュエーターへの set_actuator を rejected（unsafe）にするか（禁止中のインターロックがある）。
  /// @param index アクチュエーターの番号
  /// @param now_ms いまの単調時計
  /// @return 禁止中なら true
  [[nodiscard]] bool rejects_commands(std::size_t index, std::uint64_t now_ms) const;

  /// @brief リースの期限・インターロック・各段を見て、全アクチュエーターの出力を決め直す。
  /// @param now_ms いまの単調時計
  /// @param changes インターロックの遷移の書き込み先
  void tick(std::uint64_t now_ms, InterlockChanges& changes);

  /// @brief アクチュエーターの台数。
  /// @return 台数
  [[nodiscard]] std::size_t size() const { return slots_.size(); }
  /// @brief アクチュエーターの仕様。
  /// @param i 番号
  /// @return 仕様
  [[nodiscard]] const domain::ActuatorSpec& spec(std::size_t i) const { return slots_[i].actuator->spec(); }
  /// @brief いまの出力。
  /// @param i 番号
  /// @return 出力（0–1000‰）
  [[nodiscard]] domain::Level output(std::size_t i) const { return slots_[i].output; }
  /// @brief いまの出力を決めた段。
  /// @param i 番号
  /// @return owner
  [[nodiscard]] domain::Owner owner(std::size_t i) const { return slots_[i].owner; }
  /// @brief 本体の手動操作の値。
  /// @param i 番号
  /// @return 値（なければ std::nullopt）
  [[nodiscard]] std::optional<domain::Level> manual(std::size_t i) const { return slots_[i].manual; }
  /// @brief 出力に失敗した回数（診断用）。
  /// @return 回数
  [[nodiscard]] std::uint32_t apply_errors() const { return apply_errors_; }

 private:
  struct Slot {
    domain::IActuator* actuator{nullptr};
    std::span<domain::IInterlock* const> interlocks;
    std::array<bool, kMaxInterlocksPerActuator> announced{};  ///< ACTIVE を出して、まだ RELEASED を出していない
    bool has_lease{false};
    domain::Level lease_level{0};
    domain::Priority priority{domain::Priority::Auto};
    domain::Lease lease;
    std::optional<domain::Level> manual;
    std::optional<domain::Level> rule;
    domain::Level output{0};
    domain::Owner owner{domain::Owner::None};
    std::uint64_t switched_at_ms{0};  ///< 最後に 0 と 0 より大きい値を切り替えた時刻（最小オン・オフ時間）
  };

  /// 段の結果（インターロックをかける前）
  struct StageResult {
    domain::Level level{0};
    domain::Owner owner{domain::Owner::None};
  };

  [[nodiscard]] StageResult stages(const Slot& s) const;
  domain::Level apply_interlocks(Slot& s, domain::Level pre, std::uint64_t now_ms, InterlockChanges& changes);
  // INTERLOCK_ACTIVE・RELEASED を changes に足す（changes_output：そのインターロックが出力を変えたか）
  static void announce(Slot& s, const std::array<bool, kMaxInterlocksPerActuator>& changes_output,
                       InterlockChanges& changes);
  [[nodiscard]] static domain::Level hold_for_min_time(const Slot& s, domain::Level wanted, std::uint64_t now_ms);
  void evaluate(Slot& s, std::uint64_t now_ms, InterlockChanges& changes);

  const domain::ReadingStore& store_;
  support::StaticVector<Slot, kMaxActuators> slots_{};
  bool manual_enabled_{false};
  bool rules_enabled_{true};
  std::uint32_t apply_errors_{0};
};

}  // namespace node::app
