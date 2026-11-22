#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/clock.hpp"
#include "infra/json_writer.hpp"
#include "infra/random.hpp"
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

/// @brief 電文の時刻（ts と uptime_ms）。イベントは発生時の時刻を持つ。
struct Stamp {
  std::int64_t utc_ms{0};
  std::uint32_t uptime_ms{0};
};

/// @brief 共通エンベロープ（共通仕様 第3章）を組み立てる。
///
/// seq は kind ごとに 0 から数え、uint32 で一周する。電文を送れたときだけ commit_seq で進める。
class EnvelopeFactory {
 public:
  /// @brief 起動時に作る。boot_id はここで乱数から決める。
  /// @param device_id 発信元（mcu-cpp-01）
  /// @param clock 時計
  /// @param rng 乱数
  EnvelopeFactory(std::string_view device_id, const IClock& clock, IRandom& rng);

  /// @brief boot_id（16進小文字8桁）。
  [[nodiscard]] std::string_view boot_id() const { return boot_id_.view(); }
  [[nodiscard]] std::string_view device_id() const { return device_id_; }
  /// @brief 次に使う seq。
  [[nodiscard]] std::uint32_t peek_seq(Kind k) const { return seq_[index_of(k)]; }
  /// @brief seq を1つ進める（uint32 で一周）。
  void commit_seq(Kind k) { ++seq_[index_of(k)]; }
  /// @brief seq を直接決める（一周のテスト用）。
  void set_seq_for_test(Kind k, std::uint32_t seq) { seq_[index_of(k)] = seq; }
  /// @brief いまの時刻。
  [[nodiscard]] Stamp now() const;

  /// @brief エンベロープの先頭を書く。"{"schema":…,"payload":" まで書くので、
  /// 呼び出し側が payload の値を書いてから end_object() で閉じる。
  /// @param w 書き込み先
  /// @param k 種類
  /// @param seq 使う seq
  /// @param stamp 電文の時刻
  void begin(JsonWriter& w, Kind k, std::uint32_t seq, const Stamp& stamp);

 private:
  std::string_view device_id_;
  const IClock& clock_;
  IRandom& rng_;
  std::uint64_t boot_monotonic_ms_;
  support::FixedString<8> boot_id_{};
  std::array<std::uint32_t, kKindCount> seq_{};
};

}  // namespace node::infra
