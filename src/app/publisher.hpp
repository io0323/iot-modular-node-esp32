#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/clock.hpp"
#include "infra/envelope.hpp"
#include "infra/json_writer.hpp"
#include "infra/log_sink.hpp"
#include "infra/mqtt_link.hpp"
#include "infra/rate_limiter.hpp"
#include "infra/topic.hpp"
#include "support/fixed_string.hpp"
#include "support/result.hpp"
#include "support/static_vector.hpp"

namespace node::app {

/// @brief 発行の窓口。エンベロープを付け、送信量の上限と確認待ちの上限を守って IMqttLink に渡す。
///
/// seq は送れたとき（または再送待ちに入れたとき）だけ進める。telemetry は上限を超えたら捨てて数える。
/// それ以外の kind は上限・確認待ちで送れなければ固定容量の再送待ちに入れる（open-questions #15、仮版。P4 で本版）。
class Publisher {
 public:
  static constexpr std::size_t kMessageMax = 2048;
  static constexpr std::size_t kRetryCapacity = 8;

  /// @brief 数えている件数（診断用）。
  struct Counters {
    std::uint32_t sent{0};
    std::uint32_t telemetry_rate_dropped{0};
    std::uint32_t telemetry_busy_dropped{0};
    std::uint32_t retry_queued{0};
    std::uint32_t retry_overflow{0};
    std::uint32_t encode_errors{0};
  };

  Publisher(infra::IMqttLink& link, infra::EnvelopeFactory& envelope, const infra::Topics& topics,
            const infra::IClock& clock, infra::ILogSink& log);

  /// @brief 電文を組み立てて発行する。
  /// @tparam F void(infra::JsonWriter&) の形の関数オブジェクト。payload のオブジェクトを1つ書く
  /// @param kind 種類
  /// @param write_payload payload を書く関数
  /// @param stamp 電文の時刻（イベントの発生時刻など）。nullptr ならいま
  /// @return 送れた・再送待ちに入れたなら成功。未接続なら NotConnected、捨てたなら Busy
  template <typename F>
  support::Result<void> publish(infra::Kind kind, F&& write_payload, const infra::Stamp* stamp = nullptr) {
    if (!online()) return support::make_error(support::ErrorCode::NotConnected, "not connected");
    const infra::Stamp st = stamp ? *stamp : envelope_.now();
    infra::JsonWriter w(buf_);
    envelope_.begin(w, kind, envelope_.peek_seq(kind), st);
    write_payload(w);
    w.end_object();
    if (!w.ok()) {
      ++counters_.encode_errors;
      return support::make_error(support::ErrorCode::BufferFull, "message too large");
    }
    return send_built(kind, w.view());
  }

  /// @brief seq を進めずに発行する（offline の state。共通仕様 v0.2.1 #29）。送信量の上限も数えない。
  support::Result<void> publish_uncounted(infra::Kind kind, std::string_view message);

  /// @brief 再送待ちを送れるだけ送る。
  void flush_retries();

  /// @brief 再送待ちを捨てる（切断したとき）。
  void clear_retries() { retries_.clear(); }

  /// @brief 発行してよいかを決める。Node が接続を受け付けて（購読と online の state の前に）true にし、
  /// 切断で false にする。接続の直後、state（online）より先に telemetry が出ないようにするため。
  void set_online(bool online) { online_ = online; }
  /// @brief 発行してよいか（set_online が true で、リンクも接続中）。
  [[nodiscard]] bool online() const { return online_ && link_.connected(); }

  [[nodiscard]] const Counters& counters() const { return counters_; }
  [[nodiscard]] std::size_t retry_pending() const { return retries_.size(); }

 private:
  struct Pending {
    infra::Kind kind{infra::Kind::Event};
    support::FixedString<kMessageMax> message{};
  };

  support::Result<void> send_built(infra::Kind kind, std::string_view message);
  support::Result<void> send_now(infra::Kind kind, std::string_view message);
  support::Result<void> enqueue_retry(infra::Kind kind, std::string_view message);
  void log_tx(infra::Kind kind, std::string_view message);

  infra::IMqttLink& link_;
  infra::EnvelopeFactory& envelope_;
  const infra::Topics& topics_;
  const infra::IClock& clock_;
  infra::ILogSink& log_;
  infra::RateLimiter limiter_{};
  std::array<char, kMessageMax> buf_{};
  std::array<char, kMessageMax + 256> log_buf_{};
  support::StaticVector<Pending, kRetryCapacity> retries_{};
  Counters counters_{};
  bool online_{false};
};

}  // namespace node::app
