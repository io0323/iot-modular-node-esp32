#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <set>
#include <cstddef>
#include <mutex>
#include <string>

#include "infra/mqtt_link.hpp"

namespace node::platform {

/// @brief Eclipse Paho MQTT C（MQTTAsync）による IMqttLink。ホスト版でだけ使う。
///
/// Paho のコールバックは Paho のスレッドで来るので、受信と接続状態の変化は固定容量のキュー（mutex と condvar）に入れ、
/// アプリのロジックは main のスレッドだけで動かす。自動再接続は使わず、Node が LWT を作り直して接続し直す。
///
/// QoS 1 の確認待ちは自前の表（トークン）で持つ。PUBACK を受けたら表から消して poll_delivery で知らせる。
/// clean session の Paho は切断で未確認の送信を捨て、コールバックも呼ばない（MQTTAsync_cleanSession）ので、
/// 切断の時点で表に残っているものを「確認が取れなかった電文」として表を空にする（P4）。
class PahoMqttLink final : public infra::IMqttLink {
 public:
  static constexpr std::size_t kInboundCapacity = 32;
  static constexpr std::size_t kEventCapacity = 16;

  /// @brief クライアントを作る。
  /// @param server_uri "tcp://host:port"
  /// @param client_id クライアント ID
  PahoMqttLink(const std::string& server_uri, const std::string& client_id);
  ~PahoMqttLink() override;
  PahoMqttLink(const PahoMqttLink&) = delete;
  PahoMqttLink& operator=(const PahoMqttLink&) = delete;

  /// @brief クライアントを作れたか。
  [[nodiscard]] bool ok() const { return client_ != nullptr; }

  support::Result<void> connect(const infra::ConnectOptions& options) override;
  void disconnect(std::uint32_t timeout_ms) override;
  support::Result<std::uint32_t> publish(std::string_view topic, std::string_view payload, infra::Qos qos,
                                         bool retain) override;
  support::Result<void> subscribe(std::string_view topic, infra::Qos qos) override;
  bool poll_message(infra::InboundMessage& out) override;
  infra::LinkEvent poll_event() override;
  bool poll_delivery(std::uint32_t& token) override;
  [[nodiscard]] bool connected() const override { return connected_.load(); }
  [[nodiscard]] std::size_t inflight() const override;

  /// @brief 受信か接続状態の変化が来るまで、最大 timeout だけ待つ。
  void wait_for_activity(std::chrono::milliseconds timeout);

  /// @brief 大きすぎて捨てた受信の件数。
  [[nodiscard]] std::size_t oversized_dropped() const { return oversized_.load(); }
  /// @brief キューがいっぱいで捨てた受信の件数。
  [[nodiscard]] std::size_t inbound_dropped() const { return inbound_dropped_.load(); }

 private:
  static int on_message(void* context, char* topic, int topic_len, void* message);
  static void on_connection_lost(void* context, char* cause);
  static void on_connect_success(void* context, void* response);
  static void on_connect_failure(void* context, void* response);
  static void on_publish_qos1_success(void* context, int token);
  static void on_publish_qos1_failure(void* context, int token);
  void forget_inflight();

  void push_event(infra::LinkEvent ev);

  void* client_{nullptr};  // MQTTAsync
  std::atomic<bool> connected_{false};
  std::atomic<std::size_t> oversized_{0};
  std::atomic<std::size_t> inbound_dropped_{0};

  // connect の間だけでなく、Paho が参照しうる間は写しを持つ
  std::string will_topic_;
  std::string will_payload_;
  std::string username_;
  std::string password_;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::array<infra::InboundMessage, kInboundCapacity> inbound_{};
  std::size_t inbound_head_{0};
  std::size_t inbound_count_{0};
  std::array<infra::LinkEvent, kEventCapacity> events_{};
  std::set<int> inflight_tokens_;          // mu_ で守る
  std::set<int> early_acked_;              // mu_ で守る。publish が表に入れる前に届いた PUBACK
  std::deque<std::uint32_t> delivered_;    // mu_ で守る
  std::size_t events_head_{0};
  std::size_t events_count_{0};
};

}  // namespace node::platform
