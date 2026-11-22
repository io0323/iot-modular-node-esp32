#pragma once

#include <cstdint>
#include <string_view>

#include "domain/types.hpp"
#include "support/fixed_string.hpp"

namespace node::infra {

/// @brief 項目の状態。
enum class FieldState : std::uint8_t {
  Absent,   ///< ない
  Ok,       ///< 型と形が正しい
  Invalid,  ///< 型か形が違う
};

/// @brief cmd の op。
enum class OpKind : std::uint8_t { SetActuator, Release, SetMode, SetParam, GetStatus, Unknown };

/// @brief 読んだ cmd の payload。値の範囲（level の 0–1000 など）は CommandService が確かめる。
struct CmdRequest {
  support::FixedString<26> cmd_id{};

  FieldState op_state{FieldState::Absent};
  OpKind op{OpKind::Unknown};

  FieldState priority_state{FieldState::Absent};
  domain::Priority priority{domain::Priority::Auto};

  FieldState args_state{FieldState::Absent};   ///< args がオブジェクトか
  FieldState id_state{FieldState::Absent};     ///< args.id（snake_case の文字列）
  support::FixedString<32> id{};
  FieldState level_state{FieldState::Absent};  ///< args.level（整数）
  std::int64_t level{0};
  FieldState mode_state{FieldState::Absent};   ///< args.mode（空でない文字列）
  support::FixedString<32> mode{};

  FieldState lease_state{FieldState::Absent};  ///< lease_s（整数）
  std::int64_t lease_s{0};
  FieldState expires_state{FieldState::Absent};  ///< expires_at（ts の形）
  std::int64_t expires_at_ms{0};
  FieldState origin_state{FieldState::Absent};   ///< origin（kind・ref が文字列のオブジェクト）
};

/// @brief ack を返さずに破棄する理由。
enum class DiscardReason : std::uint8_t {
  NotJson,      ///< JSON として読めない、またはトークンが多すぎる
  BadEnvelope,  ///< エンベロープが Schema に合わない（open-questions #14）
  NoCmdId,      ///< payload.cmd_id が読めない
};

/// @brief 破棄の理由の文字列。
constexpr std::string_view to_string(DiscardReason r) {
  switch (r) {
    case DiscardReason::NotJson: return "not_json";
    case DiscardReason::BadEnvelope: return "bad_envelope";
    case DiscardReason::NoCmdId: return "no_cmd_id";
  }
  return "";
}

/// @brief parse_cmd の結果。
struct ParsedCmd {
  bool ok{false};
  DiscardReason discard{DiscardReason::NotJson};  ///< ok が false のときの理由
  CmdRequest request{};
};

/// @brief cmd の電文を jsmn でトークンに分け、エンベロープの形を確かめて CmdRequest に詰める。ヒープを使わない。
/// @param json 受信した電文
/// @return 読めたら ok=true
[[nodiscard]] ParsedCmd parse_cmd(std::string_view json);

}  // namespace node::infra
