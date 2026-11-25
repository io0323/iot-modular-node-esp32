#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/clock.hpp"
#include "infra/json_writer.hpp"
#include "infra/random.hpp"
#include "infra/time_sync.hpp"
#include "infra/ulid.hpp"
#include "support/fixed_string.hpp"

namespace node::infra {

/// @brief 作品02が発行する電文の種類（共通仕様 第2章）。
enum class Kind : std::uint8_t { Telemetry, Event, State, CmdAck };
inline constexpr std::size_t kKindCount = 4;

/// @brief kind の添字。
constexpr std::size_t index_of(Kind k) { return static_cast<std::size_t>(k); }

/// @brief エンベロープの schema（"iot.v1.{kind}"。cmd/ack は "iot.v1.cmd_ack"）。
constexpr std::string_view schema_of(Kind k) {
  switch (k) {
    case Kind::Telemetry: return "iot.v1.telemetry";
    case Kind::Event: return "iot.v1.event";
    case Kind::State: return "iot.v1.state";
    case Kind::CmdAck: return "iot.v1.cmd_ack";
  }
  return "";
}

/// @brief 電文の時刻。発生時の単調時計（ms）だけを持ち、ts と uptime_ms は送るときに求める（P4 の決定 #65）。
struct Stamp {
  std::uint64_t monotonic_ms{0};  ///< 発生時の単調時計（ms）
};

/// @brief 送った電文のエンベロープの値（送り直しで同じ電文を作り直すのに使う。P7 の決定 #116）。
struct EnvelopeFields {
  UlidString msg_id{};     ///< msg_id
  std::uint32_t seq{0};    ///< seq
  std::int64_t utc_ms{0};  ///< ts（UTC の ms）
  Stamp stamp{};           ///< 電文の時刻（uptime_ms を求める）
};

/// @brief 共通エンベロープ（共通仕様 第3章）を組み立てる。
///
/// seq は kind ごとに 0 から数え、uint32 で一周する。電文を送れたときだけ commit_seq で進める（送るときに振る。#66）。
/// ts は「発生時の単調時計 ＋ 同期で得た差（最新の値）」、uptime_ms は発生時の起動からの経過時間（#65）。
class EnvelopeFactory {
 public:
  /// @brief 起動時に作る。boot_id はここで乱数から決める。
  /// @param device_id 発信元（mcu-cpp-01）
  /// @param clock 時計
  /// @param sync 時刻同期
  /// @param rng 乱数
  EnvelopeFactory(std::string_view device_id, const IClock& clock, const ITimeSync& sync, IRandom& rng);

  /// @brief boot_id（16進小文字8桁）。
  /// @return boot_id
  [[nodiscard]] std::string_view boot_id() const { return boot_id_.view(); }
  /// @brief 発信元。
  /// @return device_id
  [[nodiscard]] std::string_view device_id() const { return device_id_; }
  /// @brief 次に使う seq。
  /// @param k 種類
  /// @return seq
  [[nodiscard]] std::uint32_t peek_seq(Kind k) const { return seq_[index_of(k)]; }
  /// @brief seq を1つ進める（uint32 で一周）。
  /// @param k 種類
  void commit_seq(Kind k) { ++seq_[index_of(k)]; }
  /// @brief seq を直接決める（一周のテスト用）。
  /// @param k 種類
  /// @param seq 次に使う seq
  void set_seq_for_test(Kind k, std::uint32_t seq) { seq_[index_of(k)] = seq; }
  /// @brief いまの時刻。
  /// @return いまの単調時計の Stamp
  [[nodiscard]] Stamp now() const { return Stamp{clock_.monotonic_ms()}; }
  /// @brief 時刻の UTC（単調時計＋同期の差）。
  /// @param stamp 時刻
  /// @return UTC の ms（1970-01-01 から）
  [[nodiscard]] std::int64_t utc_of(const Stamp& stamp) const {
    return static_cast<std::int64_t>(stamp.monotonic_ms) + sync_.utc_offset_ms();
  }
  /// @brief 時刻の uptime_ms（起動からの経過。uint32 で約49日で一周）。
  /// @param stamp 時刻
  /// @return uptime_ms
  [[nodiscard]] std::uint32_t uptime_of(const Stamp& stamp) const {
    const std::uint64_t up = stamp.monotonic_ms >= boot_monotonic_ms_ ? stamp.monotonic_ms - boot_monotonic_ms_ : 0;
    return static_cast<std::uint32_t>(up & 0xFFFFFFFFU);
  }

  /// @brief エンベロープの先頭を書く。"{"schema":…,"payload":" まで書くので、
  /// 呼び出し側が payload の値を書いてから end_object() で閉じる。
  /// @param w 書き込み先
  /// @param k 種類
  /// @param seq 使う seq
  /// @param stamp 電文の時刻
  /// @return 書いたエンベロープの値（msg_id・seq・ts）
  EnvelopeFields begin(JsonWriter& w, Kind k, std::uint32_t seq, const Stamp& stamp);
  /// @brief 前に送った電文と同じエンベロープの先頭を書く（msg_id・seq・ts・uptime_ms を変えない。#71・#116）。
  /// @param w 書き込み先
  /// @param k 種類
  /// @param fields 前に送った電文のエンベロープの値
  void begin_same(JsonWriter& w, Kind k, const EnvelopeFields& fields) const;

 private:
  std::string_view device_id_;
  const IClock& clock_;
  const ITimeSync& sync_;
  IRandom& rng_;
  std::uint64_t boot_monotonic_ms_;
  support::FixedString<8> boot_id_;
  std::array<std::uint32_t, kKindCount> seq_{};
};

}  // namespace node::infra
