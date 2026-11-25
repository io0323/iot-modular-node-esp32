// MQTT（esp-mqtt。ESP-IDF v5.5.6 の esp-mqtt 0e9ec171）。P6 の決定 #96・#104。
//
// esp-mqtt の API を呼ぶのは comm のタスクだけ（EspMqttComm）。control の Node が使う IMqttLink（EspMqttLink）は、
// 送信の依頼をリングバッファに積み、接続状態の変化・PUBACK・受信をキューとリングバッファから取り出すだけ。
// - QoS 1 の確認待ちの件数は control の側で数える（MQTT_EVENT_PUBLISHED で減らす。上限32。共通仕様 第4章）
// - 接続のたびに stop → set_config（LWT を作り直す）→ start とする。esp-mqtt は stop のときに outbox を消すので
//   （mqtt_client.c の mqtt_task の終わり：outbox_delete_all_items）、切断の前の確認待ちを esp-mqtt が送り直すことはなく、
//   送り直しは EventService（#71）の1回だけになる。自動の再接続は切る（ConnectionManager が再接続する）
// - 接続中に PUBACK が遅れると、esp-mqtt は同じ電文を DUP で送り直す（message_retransmit_timeout。既定1秒）。
//   seq は変わらないので、受け手は共通仕様の重複排除で捨てる
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "infra/mqtt_link.hpp"
#include "mqtt_client.h"
#include "support/fixed_string.hpp"
#include "support/static_vector.hpp"

namespace node::platform::esp32 {

/// @brief control と comm のあいだの受け渡し口（どれも FreeRTOS のキューかリングバッファ。共有変数は持たない）。
struct MqttChannels {
  RingbufHandle_t commands{nullptr};    ///< control → comm：接続・切断・購読・発行の依頼（可変長）
  RingbufHandle_t inbound{nullptr};     ///< esp-mqtt → control：受信した電文（可変長）
  QueueHandle_t link_events{nullptr};   ///< comm → control：infra::LinkEvent
  QueueHandle_t deliveries{nullptr};    ///< comm → control：PUBACK を受けた（か、送れなかった）トークン
  QueueHandle_t mqtt_events{nullptr};   ///< esp-mqtt のタスク → comm：接続・切断・PUBACK
};

/// @brief 受け渡し口を作る（静的に確保。app_main から1回だけ呼ぶ）。
/// @return 受け渡し口
MqttChannels create_mqtt_channels();

/// @brief control の側の IMqttLink。
class EspMqttLink final : public infra::IMqttLink {
 public:
  /// @brief 受け渡し口を受け取る。
  /// @param ch 受け渡し口
  explicit EspMqttLink(const MqttChannels& ch) : ch_(ch) {}

  support::Result<void> connect(const infra::ConnectOptions& options) override;
  void disconnect(std::uint32_t timeout_ms) override;
  support::Result<std::uint32_t> publish(std::string_view topic, std::string_view payload, infra::Qos qos,
                                         bool retain) override;
  support::Result<void> subscribe(std::string_view topic, infra::Qos qos) override;
  bool poll_message(infra::InboundMessage& out) override;
  infra::LinkEvent poll_event() override;
  bool poll_delivery(std::uint32_t& token) override;
  [[nodiscard]] bool connected() const override { return connected_; }
  [[nodiscard]] std::size_t inflight() const override { return inflight_; }

 private:
  MqttChannels ch_;
  bool connected_{false};
  std::size_t inflight_{0};
  std::uint32_t next_token_{1};
};

/// @brief comm のタスクの側。esp-mqtt のクライアントを持ち、依頼を esp-mqtt に渡し、イベントを control へ送る。
class EspMqttComm {
 public:
  static constexpr int kBufferSize = 2048;  ///< esp-mqtt の送受信のバッファ（#113）
  static constexpr std::size_t kMaxPending = 64;  ///< PUBACK 待ちの msg_id とトークンの組

  /// @brief 受け渡し口とブローカーの URI を受け取る（クライアントは最初の接続の依頼で作る）。
  /// @param ch 受け渡し口
  /// @param broker_uri ブローカーの URI（例："mqtt://192.168.1.10:1883"。寿命はプログラム全体）
  EspMqttComm(const MqttChannels& ch, const char* broker_uri) : ch_(ch), uri_(broker_uri) {}

  /// @brief 1周分：依頼を1件待って処理し（最大 wait）、esp-mqtt のイベントをすべて control へ送る。
  /// @param wait 依頼を待つ tick
  void step(TickType_t wait);

 private:
  struct Pending {
    int msg_id{0};
    std::uint32_t token{0};
  };
  static void on_event(void* arg, esp_event_base_t base, std::int32_t id, void* data);
  void handle_command(const std::uint8_t* item, std::size_t len);
  void do_connect(const std::uint8_t* item, std::size_t len);
  void do_publish(const std::uint8_t* item, std::size_t len);
  bool keep_strings(const std::array<std::string_view, 5>& s);
  [[nodiscard]] esp_mqtt_client_config_t config_of(const void* header) const;
  bool apply_config(const esp_mqtt_client_config_t& cfg);
  void stop_client();
  void forward_events();
  void send_link_event(infra::LinkEvent ev) const;
  void send_delivery(std::uint32_t token, bool delivered) const;

  MqttChannels ch_;
  const char* uri_;
  esp_mqtt_client_handle_t client_{nullptr};
  bool started_{false};
  bool session_connected_{false};
  support::StaticVector<Pending, kMaxPending> pending_{};
  // 接続の設定の文字列（esp_mqtt_set_config に渡す。次の接続まで持つ）
  support::FixedString<64> client_id_{};
  support::FixedString<64> username_{};
  support::FixedString<128> password_{};
  support::FixedString<128> will_topic_{};
  support::FixedString<2048> will_payload_{};
  support::FixedString<128> topic_{};
};

}  // namespace node::platform::esp32
