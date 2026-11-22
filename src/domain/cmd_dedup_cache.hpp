#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "support/fixed_string.hpp"

namespace node::domain {

/// @brief 重複した cmd_id を見分けるための、直近の cmd_id と ack の payload の記憶
/// （共通仕様 第4章「区間ごとの約束」：直近64件・10分）。
///
/// 64件を超えたら古いものから消す。rejected の結果も覚える。
class CmdDedupCache {
 public:
  static constexpr std::size_t kCapacity = 64;
  static constexpr std::uint64_t kRetentionMs = 10U * 60U * 1000U;
  static constexpr std::size_t kCmdIdLen = 26;
  static constexpr std::size_t kPayloadMax = 256;

  using Payload = support::FixedString<kPayloadMax>;

  /// @brief 覚えている ack の payload を探す。
  /// @param cmd_id cmd_id
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 10分以内に覚えたものがあればその payload、なければ std::nullopt
  [[nodiscard]] std::optional<Payload> find(std::string_view cmd_id, std::uint64_t now_ms) const;

  /// @brief ack の payload を覚える。いっぱいなら最も古いものを消す。
  /// @param cmd_id cmd_id（26文字以内）
  /// @param payload ack の payload（kPayloadMax 文字以内）
  /// @param now_ms いまの時刻（単調時計の ms）
  /// @return 長すぎて覚えられなければ false
  [[nodiscard]] bool insert(std::string_view cmd_id, std::string_view payload, std::uint64_t now_ms);

  /// @brief 覚えている件数（期限切れを含む）。
  [[nodiscard]] std::size_t size() const { return count_; }

 private:
  struct Entry {
    support::FixedString<kCmdIdLen> cmd_id{};
    std::uint64_t stored_ms{0};
    Payload payload{};
  };
  std::array<Entry, kCapacity> entries_{};
  std::size_t next_{0};   ///< 次に書く位置（最も古いもの）
  std::size_t count_{0};
};

}  // namespace node::domain
