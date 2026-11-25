#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/types.hpp"

namespace node::app {

/// @brief モードを切り替えた理由（MODE_CHANGED の detail.cause。P2 の決定 #33）。
enum class ModeCause : std::uint8_t { Button, Cmd, Timeout };

/// @brief 理由の文字列。
constexpr std::string_view to_string(ModeCause c) {
  switch (c) {
    case ModeCause::Button: return "button";
    case ModeCause::Cmd: return "cmd";
    case ModeCause::Timeout: return "timeout";
  }
  return "";
}

/// @brief モードの切り替え（MODE_CHANGED の元）。
struct ModeChange {
  domain::Mode from{domain::Mode::Auto};  ///< 前のモード
  domain::Mode to{domain::Mode::Auto};    ///< 新しいモード
  ModeCause cause{ModeCause::Cmd};        ///< 理由
};

/// @brief モードごとのふるまい（State パターンの状態）。
class ModeState {
 public:
  virtual ~ModeState() = default;
  /// @brief このモード。
  /// @return モード
  [[nodiscard]] virtual domain::Mode mode() const = 0;
  /// @brief ボタンの短押しで移る先（自動 → 手動 → 省電力 → 自動。P2 の決定 #26）。
  /// @return 次のモードの状態
  [[nodiscard]] virtual const ModeState& next_on_press() const = 0;
  /// @brief 本体の手動操作の段が有効か。
  /// @return 有効なら true
  [[nodiscard]] virtual bool manual_enabled() const = 0;
  /// @brief ローカルルールの段が有効か。
  /// @return 有効なら true
  [[nodiscard]] virtual bool rules_enabled() const = 0;
  /// @brief lighting の印のルールが有効か。
  /// @return 有効なら true
  [[nodiscard]] virtual bool lighting_enabled() const = 0;
  /// @brief 計測周期の倍率（省電力モードは5倍。P2 の決定 #28）。
  /// @return 倍率
  [[nodiscard]] virtual std::uint32_t period_factor() const = 0;
  /// @brief 無操作で自動に戻るか（手動モードだけ）。
  /// @return 戻るなら true
  [[nodiscard]] virtual bool times_out() const = 0;
};

/// @brief 自動・手動・省電力の3モードの管理（基本設計書 FR-04、第6章「モード」）。
///
/// 手動モードは、入った時刻か最後の手動操作の時刻から30分ちょうどで自動に戻る（P2 の決定 #27）。
class ModeManager {
 public:
  static constexpr std::uint64_t kManualTimeoutMs = std::uint64_t{30} * 60U * 1000U;  ///< 手動モードの無操作の上限（30分）

  /// @brief 自動モードで始める。
  ModeManager();

  /// @brief いまのモード。
  /// @return モード
  [[nodiscard]] domain::Mode mode() const { return state_->mode(); }
  /// @brief いまのモードの状態（ふるまい）。
  /// @return 状態
  [[nodiscard]] const ModeState& state() const { return *state_; }

  /// @brief ボタンの短押し。
  /// @param now_ms いまの単調時計
  /// @return 切り替えた内容
  std::optional<ModeChange> press(std::uint64_t now_ms);
  /// @brief cmd（set_mode）による切り替え。いまと同じモードなら何もしない（#18）。
  /// @param to 切り替え先
  /// @param now_ms いまの単調時計
  /// @return 切り替えた内容。同じモードなら std::nullopt
  std::optional<ModeChange> request(domain::Mode to, std::uint64_t now_ms);
  /// @brief 手動モードのタイムアウトを見る。周期的に呼ぶ。
  /// @param now_ms いまの単調時計
  /// @return タイムアウトで自動に戻した内容。戻さなければ std::nullopt
  std::optional<ModeChange> tick(std::uint64_t now_ms);
  /// @brief 手動操作があった（タイムアウトを数え直す）。
  /// @param now_ms いまの単調時計
  void touch_manual(std::uint64_t now_ms) { last_activity_ms_ = now_ms; }

 private:
  std::optional<ModeChange> go(const ModeState& to, ModeCause cause, std::uint64_t now_ms);
  static const ModeState& state_of(domain::Mode m);

  const ModeState* state_;
  std::uint64_t last_activity_ms_{0};
};

}  // namespace node::app
