#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/reading_store.hpp"
#include "domain/types.hpp"

namespace node::domain {

/// @brief インターロックへの入力。
struct InterlockInput {
  const ReadingStore& store;  ///< 最新の計測値と品質
  Level requested{0};         ///< インターロックをかける前の、そのアクチュエーターへの要求（調停の段の結果）
  std::uint64_t now_ms{0};    ///< いまの時刻（単調時計の ms）
};

/// @brief インターロックの判定。強制値は上限より優先する（P2 の決定 #20）。
struct InterlockVerdict {
  std::optional<Level> force;  ///< あれば、出力をこの値に固定する
  std::optional<Level> upper;  ///< あれば、出力をこの値より大きくしない
};

/// @brief 安全インターロックの共通の契約（P2 の決定 #19）。構成表で対象のアクチュエーターに結び付ける。
///
/// ControlArbiter は具体的なクラスを知らず、この契約だけで扱う。新しいインターロックも「新しいクラス＋構成表1行」で足せる。
class IInterlock {
 public:
  virtual ~IInterlock() = default;
  /// @brief イベントの detail.rule に入れる ID（例："max_on_s"）。
  /// @return ID
  [[nodiscard]] virtual std::string_view rule() const = 0;
  /// @brief 状態を進めて判定する。10ms ごとなど周期的に呼ぶ。
  /// @param in 計測値・要求・時刻
  /// @return 強制値と上限（なければ空）
  virtual InterlockVerdict evaluate(const InterlockInput& in) = 0;
  /// @brief 条件が成り立っているか（直前の evaluate の結果）。
  /// @return 成り立っていれば true
  [[nodiscard]] virtual bool engaged() const = 0;
  /// @brief このアクチュエーターへの set_actuator を rejected（unsafe）にするか（禁止中）。
  /// @return 禁止中なら true（既定は false）
  [[nodiscard]] virtual bool rejects_commands(const ReadingStore& /*store*/, std::uint64_t /*now_ms*/) const {
    return false;
  }
};

}  // namespace node::domain
