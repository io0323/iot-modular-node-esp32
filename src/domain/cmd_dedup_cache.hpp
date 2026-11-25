#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "domain/types.hpp"
#include "support/fixed_string.hpp"

namespace node::domain {

/// @brief ack の payload を作り直すのに要る値（P7 の決定 #116。payload の写しの代わりに持つ）。
struct AckRecord {
  bool rejected{false};    ///< rejected か（false なら done）
  std::uint8_t reason{0};  ///< rejected の理由（app の RejectReason の値）
  bool output{false};      ///< 出力を扱う op（limited・applied を付ける）
  bool limited{false};     ///< 安全制限で丸めたか
  Level applied{0};        ///< 出力した値
};

/// @brief 重複した cmd_id を見分けるための、直近の cmd_id と ack の値の記憶
/// （共通仕様 第4章「区間ごとの約束」：直近64件・10分）。
///
/// 64件を超えたら古いものから消す。rejected の結果も覚える。ack の payload は覚えた値から作り直すので、
/// 前回と同じ payload になる（#28・#116）。
class CmdDedupCache {
 public:
  static constexpr std::size_t kCapacity = 64;                                   ///< 覚える件数
  static constexpr std::uint64_t kRetentionMs = std::uint64_t{10} * 60U * 1000U;  ///< 覚えておく時間（10分）
  static constexpr std::size_t kCmdIdLen = 26;                                    ///< cmd_id の長さ（ULID）

  /// @brief 覚えている ack の値を探す。
  /// @param cmd_id cmd_id
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 10分以内に覚えたものがあればその値、なければ std::nullopt
  [[nodiscard]] std::optional<AckRecord> find(std::string_view cmd_id, std::uint64_t now_ms) const;

  /// @brief ack の値を覚える。いっぱいなら最も古いものを消す。
  /// @param cmd_id cmd_id（26文字以内）
  /// @param ack ack の値
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return cmd_id が長すぎて覚えられなければ false
  [[nodiscard]] bool insert(std::string_view cmd_id, const AckRecord& ack, std::uint64_t now_ms);

  /// @brief 覚えている件数（期限切れを含む）。
  /// @return 件数
  [[nodiscard]] std::size_t size() const { return count_; }

 private:
  struct Entry {
    support::FixedString<kCmdIdLen> cmd_id;
    std::uint64_t stored_ms{0};
    AckRecord ack{};
  };
  std::array<Entry, kCapacity> entries_{};
  std::size_t next_{0};   ///< 次に書く位置（最も古いもの）
  std::size_t count_{0};
};

}  // namespace node::domain
