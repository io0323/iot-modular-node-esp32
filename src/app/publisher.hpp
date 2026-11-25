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

/// @brief 発行の窓口。送るときにエンベロープ（seq・ts）を付け、送信量の上限と確認待ちの上限を守って IMqttLink に渡す。
///
/// 上限（kind ごとに平均毎秒10件・瞬間50件、QoS 1 の確認待ち32件）に当たったとき（open-questions #15）：
/// - telemetry：捨てて数える
/// - event：送らずに Busy を返す（呼び出し側の EventService が保持する）。seq は進めない
/// - cmd_ack・state：固定容量（8件）の送り直し待ちに入れて次の tick で送る。あふれたら捨てて数える
class Publisher {
 public:
  static constexpr std::size_t kMessageMax = 2048;  ///< 電文の最大長
  static constexpr std::size_t kRetryCapacity = 8;  ///< 送り直し待ちの容量（#15）
  /// 送り直し待ちの1件の大きさ。送り直し待ちに入るのは cmd_ack と state だけで、最悪の長さ（state で約 770 バイト、
  /// cmd_ack で約 340 バイト）が収まる大きさ（P7 の決定 #116。publisher_test の RetrySlotFitsWorstCase で確かめる）
  static constexpr std::size_t kRetryMessageMax = 1024;

  /// @brief 数えている件数（診断用）。
  struct Counters {
    std::uint32_t sent{0};                    ///< 発行した電文
    std::uint32_t telemetry_rate_dropped{0};  ///< 送信量の上限で捨てた telemetry
    std::uint32_t telemetry_busy_dropped{0};  ///< 確認待ちの上限で捨てた telemetry
    std::uint32_t event_deferred{0};  ///< 上限で送れず、EventService に戻した event
    std::uint32_t retry_queued{0};    ///< 送り直し待ちに入れた cmd_ack・state
    std::uint32_t retry_overflow{0};  ///< 送り直し待ちがあふれて捨てた cmd_ack・state
    std::uint32_t encode_errors{0};   ///< バッファに収まらず組み立てられなかった電文
  };

  /// @brief 送れた電文。
  struct Sent {
    std::uint32_t token{0};  ///< QoS 1 のトークン（送り直し待ちに入れたときは 0）
    std::uint32_t seq{0};    ///< 付けた seq
    infra::EnvelopeFields fields{};  ///< 送った電文のエンベロープの値（送り直しで同じ電文を作り直すのに使う）
  };

  /// @brief 窓口を作る。
  /// @param link MQTT の接続
  /// @param envelope エンベロープ（seq・ts）
  /// @param topics トピック
  /// @param clock 単調時計（送信量の上限）
  /// @param log 送った電文のログ
  Publisher(infra::IMqttLink& link, infra::EnvelopeFactory& envelope, const infra::Topics& topics,
            const infra::IClock& clock, infra::ILogSink& log);

  /// @brief 電文を組み立てて発行する。
  /// @tparam F void(infra::JsonWriter&) の形の関数オブジェクト。payload のオブジェクトを1つ書く
  /// @param kind 種類
  /// @param write_payload payload を書く関数
  /// @param stamp 電文の時刻（イベントの発生時刻など）。nullptr ならいま
  /// @return 送れた・送り直し待ちに入れたなら成功。未接続なら NotConnected、上限で送れなければ Busy
  template <typename F>
  support::Result<Sent> publish(infra::Kind kind, const F& write_payload, const infra::Stamp* stamp = nullptr) {
    if (!online()) {
      return support::make_error(support::ErrorCode::NotConnected, "not connected");
    }
    const infra::Stamp st = stamp ? *stamp : envelope_.now();
    infra::JsonWriter w(buf_);
    const infra::EnvelopeFields fields = envelope_.begin(w, kind, envelope_.peek_seq(kind), st);
    write_payload(w);
    w.end_object();
    if (!w.ok()) {
      ++counters_.encode_errors;
      return support::make_error(support::ErrorCode::BufferFull, "message too large");
    }
    auto r = send_built(kind, w.view(), fields.seq, true);
    if (r) {
      r.value().fields = fields;
    }
    return r;
  }

  /// @brief 前に送った電文を、同じ msg_id・seq・ts のまま作り直して送る（確認が取れなかった event。#71・#116）。
  /// payload を書く関数が同じ中身を書けば、前に送った電文とバイト単位で同じになる。seq のカウンタは進めない。
  /// @tparam F void(infra::JsonWriter&) の形の関数オブジェクト
  /// @param kind 種類
  /// @param write_payload payload を書く関数
  /// @param fields 前に送った電文のエンベロープの値
  /// @return 送れたら成功。未接続なら NotConnected、上限で送れなければ Busy
  template <typename F>
  support::Result<Sent> republish(infra::Kind kind, const F& write_payload, const infra::EnvelopeFields& fields) {
    if (!online()) {
      return support::make_error(support::ErrorCode::NotConnected, "not connected");
    }
    infra::JsonWriter w(buf_);
    envelope_.begin_same(w, kind, fields);
    write_payload(w);
    w.end_object();
    if (!w.ok()) {
      ++counters_.encode_errors;
      return support::make_error(support::ErrorCode::BufferFull, "message too large");
    }
    auto r = send_built(kind, w.view(), fields.seq, false);
    if (r) {
      r.value().fields = fields;
    }
    return r;
  }

  /// @brief seq を進めずに発行する（offline の state。共通仕様 v0.2.1 #29）。送信量の上限も数えない。
  /// @param kind 種類
  /// @param message 組み立て済みの電文
  /// @return 送れなければ失敗
  support::Result<void> publish_uncounted(infra::Kind kind, std::string_view message);

  /// @brief 送り直し待ちを送れるだけ送る。
  void flush_retries();
  /// @brief 送り直し待ちを捨てる（切断したとき。cmd_ack は送り直さない。state は再接続のたびに新しく送る。#71）。
  void clear_retries() { retries_.clear(); }

  /// @brief 発行してよいかを決める（ConnectionManager が接続を受け付けて true、切断で false）。
  /// @param online 発行してよいなら true
  void set_online(bool online) { online_ = online; }
  /// @brief 発行してよいか（set_online が true で、リンクも接続中）。
  /// @return 発行してよければ true
  [[nodiscard]] bool online() const { return online_ && link_.connected(); }

  /// @brief 数えている件数。
  /// @return Counters
  [[nodiscard]] const Counters& counters() const { return counters_; }
  /// @brief 送り直し待ちの件数。
  /// @return 件数
  [[nodiscard]] std::size_t retry_pending() const { return retries_.size(); }

 private:
  struct Pending {
    infra::Kind kind{infra::Kind::State};
    support::FixedString<kRetryMessageMax> message;
  };

  support::Result<Sent> send_built(infra::Kind kind, std::string_view message, std::uint32_t seq, bool commit);
  support::Result<std::uint32_t> send_now(infra::Kind kind, std::string_view message);
  support::Result<Sent> enqueue_retry(infra::Kind kind, std::string_view message, std::uint32_t seq);
  // 上限（rate_limited）か確認待ちがいっぱいで送れなかった：telemetry は捨てる、event は保持へ、
  // cmd_ack・state は送り直し待ちへ（#15）
  support::Result<Sent> defer(infra::Kind kind, std::string_view message, std::uint32_t seq, bool commit,
                              bool rate_limited);
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
