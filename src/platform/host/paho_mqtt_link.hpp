#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>

#include "infra/mqtt_link.hpp"

namespace node::platform {

/// @brief Eclipse Paho MQTT C（MQTTAsync）による IMqttLink。ホスト版でだけ使う。
///
/// Paho のコールバックは Paho のスレッドで来るので、受信と接続状態の変化は固定容量のキュー（mutex と condvar）に入れ、
/// アプリのロジックは main のスレッドだけで動かす。自動再接続は使わず、Node が LWT を作り直して接続し直す。
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
  support::Result<void> publish(std::string_view topic, std::string_view payload, infra::Qos qos,
                                bool retain) override;
  support::Result<void> subscribe(std::string_view topic, infra::Qos qos) override;
  bool poll_message(infra::InboundMessage& out) override;
  infra::LinkEvent poll_event() override;
  [[nodiscard]] bool connected() const override { return connected_.load(); }
  [[nodiscard]] std::size_t inflight() const override {
    const int n = inflight_.load();
    return n > 0 ? static_cast<std::size_t>(n) : 0U;
  }

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
  static void on_publish_qos1_done(void* context, void* response);

  void push_event(infra::LinkEvent ev);

  void* client_{nullptr};  // MQTTAsync
  std::atomic<bool> connected_{false};
  std::atomic<int> inflight_{0};
  std::atomic<std::size_t> oversized_{0};
  std::atomic<std::size_t> inbound_dropped_{0};

  // connect の間だけでなく、Paho が参照しうる間は写しを持つ
  std::string will_topic_;
  std::string will_payload_;
  std::string username_;
  std::string password_;

  std::mutex mu_;
  std::condition_variable cv_;
  std::array<infra::InboundMessage, kInboundCapacity> inbound_{};
  std::size_t inbound_head_{0};
  std::size_t inbound_count_{0};
  std::array<infra::LinkEvent, kEventCapacity> events_{};
  std::size_t events_head_{0};
  std::size_t events_count_{0};
};

}  // namespace node::platform
