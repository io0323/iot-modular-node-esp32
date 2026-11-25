#pragma once

#include <cstdint>

#include "infra/mqtt_link.hpp"
#include "infra/network.hpp"
#include "infra/time_sync.hpp"

namespace node::app {

/// @brief 再接続の間隔（1秒から失敗のたびに2倍、上限60秒。ジッターなし。接続できたら1秒に戻す。P4 の決定 #69）。
class Backoff {
 public:
  static constexpr std::uint32_t kInitialMs = 1000;  ///< 最初の待ち時間
  static constexpr std::uint32_t kMaxMs = 60000;     ///< 待ち時間の上限

  /// @brief 次に待つ時間（呼ぶたびに2倍になる）。
  /// @return 待ち時間（ms）
  std::uint32_t next() {
    const std::uint32_t d = delay_ms_;
    delay_ms_ = delay_ms_ >= kMaxMs / 2 ? kMaxMs : delay_ms_ * 2;
    return d;
  }
  /// @brief 1秒に戻す。
  void reset() { delay_ms_ = kInitialMs; }
  /// @brief 次の next() が返す値。
  /// @return 待ち時間（ms）
  [[nodiscard]] std::uint32_t peek() const { return delay_ms_; }

 private:
  std::uint32_t delay_ms_{kInitialMs};
};

/// @brief 接続の順序（ネットワーク → SNTP の同期 → MQTT）と再接続を受け持つ状態機械（P4 の決定 #64・#69）。
///
/// MQTT の接続を始めるときは、LWT を作る必要があるので Node に知らせる（Actions::want_mqtt_connect）。
/// Node が connect_mqtt を呼ぶ。ネットワークが落ちたら MQTT も切断として扱う。
class ConnectionManager {
 public:
  /// @brief tick の結果。
  struct Actions {
    bool want_mqtt_connect{false};  ///< MQTT に接続を始めてよい（Node が LWT を作って connect_mqtt を呼ぶ）
    bool connected{false};          ///< MQTT に接続した
    bool lost{false};               ///< MQTT の接続が切れた（ネットワークが落ちたときも）
  };

  /// @brief MQTT の状態。
  enum class MqttState : std::uint8_t { Idle, Connecting, Connected };

  /// @brief 状態機械を作る（接続は tick で始める）。
  /// @param network ネットワーク（Wi-Fi）
  /// @param sync 時刻の同期
  /// @param link MQTT の接続
  ConnectionManager(infra::INetwork& network, const infra::ITimeSync& sync, infra::IMqttLink& link)
      : network_(network), sync_(sync), link_(link) {}

  /// @brief 1周分進める。
  /// @param now_ms いまの単調時計
  /// @return Node がすること（MQTT の接続を始める・接続した・切れた）
  Actions tick(std::uint64_t now_ms);
  /// @brief MQTT の接続を始める（tick が want_mqtt_connect を返したとき）。
  /// @param options 接続の設定（LWT を含む）
  /// @param now_ms いまの単調時計
  void connect_mqtt(const infra::ConnectOptions& options, std::uint64_t now_ms);

  /// @brief ネットワークが使えるか。
  /// @return 使えれば true
  [[nodiscard]] bool network_up() const { return net_up_; }
  /// @brief MQTT の状態。
  /// @return 状態
  [[nodiscard]] MqttState mqtt_state() const { return mqtt_; }
  /// @brief MQTT に接続しているか。
  /// @return 接続していれば true
  [[nodiscard]] bool mqtt_connected() const { return mqtt_ == MqttState::Connected; }
  /// @brief 次に MQTT の接続を試す時刻（テスト・診断用）。
  /// @return 単調時計の ms
  [[nodiscard]] std::uint64_t next_mqtt_attempt_ms() const { return mqtt_retry_at_ms_; }
  /// @brief 次にネットワークの接続を試す時刻（テスト・診断用）。
  /// @return 単調時計の ms
  [[nodiscard]] std::uint64_t next_network_attempt_ms() const { return net_retry_at_ms_; }
  /// @brief MQTT の接続を試した回数（診断用）。
  /// @return 回数
  [[nodiscard]] std::uint32_t mqtt_attempts() const { return mqtt_attempts_; }
  /// @brief ネットワークの接続を試した回数（診断用）。
  /// @return 回数
  [[nodiscard]] std::uint32_t network_attempts() const { return net_attempts_; }

 private:
  void handle_network_events(std::uint64_t now_ms, Actions& a);
  void handle_link_events(std::uint64_t now_ms, Actions& a);
  void mqtt_down(std::uint64_t now_ms, bool failed_attempt, Actions& a);

  infra::INetwork& network_;
  const infra::ITimeSync& sync_;
  infra::IMqttLink& link_;

  bool net_up_{false};
  bool net_connecting_{false};
  std::uint64_t net_retry_at_ms_{0};
  Backoff net_backoff_{};
  std::uint32_t net_attempts_{0};

  MqttState mqtt_{MqttState::Idle};
  std::uint64_t mqtt_retry_at_ms_{0};
  Backoff mqtt_backoff_{};
  std::uint32_t mqtt_attempts_{0};
};

}  // namespace node::app
