#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "app/control_arbiter.hpp"
#include "domain/cmd_dedup_cache.hpp"
#include "domain/reading_store.hpp"
#include "domain/types.hpp"
#include "infra/cmd_parser.hpp"

namespace node::app {

/// @brief cmd/ack の reason（共通仕様 第3章）。
enum class RejectReason : std::uint8_t { Unsafe, InvalidArg, UnknownOp, UnknownTarget, Expired };

/// @brief reason の文字列。
constexpr std::string_view to_string(RejectReason r) {
  switch (r) {
    case RejectReason::Unsafe: return "unsafe";
    case RejectReason::InvalidArg: return "invalid_arg";
    case RejectReason::UnknownOp: return "unknown_op";
    case RejectReason::UnknownTarget: return "unknown_target";
    case RejectReason::Expired: return "expired";
  }
  return "";
}

/// @brief cmd を処理した結果。
struct CommandOutcome {
  enum class Kind : std::uint8_t { Discarded, Ack };
  Kind kind{Kind::Discarded};
  infra::DiscardReason discard{infra::DiscardReason::NotJson};  ///< Discarded のときの理由
  bool duplicate{false};                                       ///< 重複した cmd_id（実行していない）
  domain::CmdDedupCache::Payload ack_payload{};                ///< cmd/ack の payload（"{...}"）
  bool republish_state{false};                                 ///< state を出し直す
  std::optional<domain::Mode> mode_changed_to{};               ///< モードを切り替えた（MODE_CHANGED を送る）
  ControlArbiter::InterlockChanges interlock_changes{};        ///< 指令で起きたインターロックの遷移
};

/// @brief cmd の検証・実行・ack の生成（基本設計書 第6章「指令の受け付け」）。
///
/// 即時に終わるので accepted は送らず done を返す。重複した cmd_id は実行せず、前回の ack の payload を返す
/// （エンベロープは呼び出し側が新しく作る）。
class CommandService {
 public:
  /// @brief 依存を受け取る。
  /// @param arbiter 出力の調停
  /// @param store 最新の計測値（土壌水分の故障の判定に使う）
  CommandService(ControlArbiter& arbiter, const domain::ReadingStore& store) : arbiter_(arbiter), store_(store) {}

  /// @brief cmd の電文を処理する。
  /// @param json 受信した電文
  /// @param now_ms 受信時刻（単調時計の ms。リースの起点）
  /// @param now_utc_ms いまの UTC（expires_at の判定）
  /// @return 結果
  CommandOutcome handle(std::string_view json, std::uint64_t now_ms, std::int64_t now_utc_ms);

  [[nodiscard]] domain::Mode mode() const { return mode_; }

 private:
  struct Exec {
    std::optional<RejectReason> reject{};
    bool output{false};  ///< 出力を扱う op（applied・limited を付ける）
    ControlArbiter::CommandResult result{};
  };

  Exec execute(const infra::CmdRequest& req, std::uint64_t now_ms, std::int64_t now_utc_ms, CommandOutcome& out);
  Exec set_actuator(const infra::CmdRequest& req, std::uint64_t now_ms, CommandOutcome& out);
  Exec release(const infra::CmdRequest& req, std::uint64_t now_ms, CommandOutcome& out);
  Exec set_mode(const infra::CmdRequest& req, CommandOutcome& out);

  ControlArbiter& arbiter_;
  const domain::ReadingStore& store_;
  domain::CmdDedupCache cache_{};
  domain::Mode mode_{domain::Mode::Auto};
};

}  // namespace node::app
