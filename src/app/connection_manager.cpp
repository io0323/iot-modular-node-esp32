#include "app/connection_manager.hpp"

namespace node::app {

using infra::LinkEvent;
using infra::NetworkEvent;

void ConnectionManager::mqtt_down(std::uint64_t now_ms, bool failed_attempt, Actions& a) {
  const bool was_connected = mqtt_ == MqttState::Connected;
  mqtt_ = MqttState::Idle;
  // 接続できなかったら間隔を2倍に。接続していた後に切れたら1秒から（接続できた時点で1秒に戻している）
  mqtt_retry_at_ms_ = now_ms + mqtt_backoff_.next();
  if (was_connected) a.lost = true;
  (void)failed_attempt;
}

void ConnectionManager::handle_network_events(std::uint64_t now_ms, Actions& a) {
  for (NetworkEvent ev = network_.poll_event(); ev != NetworkEvent::None; ev = network_.poll_event()) {
    switch (ev) {
      case NetworkEvent::Up:
        net_up_ = true;
        net_connecting_ = false;
        net_backoff_.reset();
        break;
      case NetworkEvent::ConnectFailed:
        net_connecting_ = false;
        net_up_ = false;
        net_retry_at_ms_ = now_ms + net_backoff_.next();
        break;
      case NetworkEvent::Down:
        net_up_ = false;
        net_connecting_ = false;
        net_retry_at_ms_ = now_ms + net_backoff_.next();
        // ネットワークが落ちたら MQTT も切断として扱う
        if (mqtt_ != MqttState::Idle) {
          link_.disconnect(0);
          mqtt_down(now_ms, false, a);
        }
        break;
      case NetworkEvent::None: break;
    }
  }
}

void ConnectionManager::handle_link_events(std::uint64_t now_ms, Actions& a) {
  for (LinkEvent ev = link_.poll_event(); ev != LinkEvent::None; ev = link_.poll_event()) {
    switch (ev) {
      case LinkEvent::Connected:
        if (mqtt_ != MqttState::Connecting) break;  // 自分で切った後に届いた古い通知は無視する
        mqtt_ = MqttState::Connected;
        mqtt_backoff_.reset();
        a.connected = true;
        break;
      case LinkEvent::ConnectFailed:
        if (mqtt_ == MqttState::Connecting) mqtt_down(now_ms, true, a);
        break;
      case LinkEvent::Disconnected:
        if (mqtt_ != MqttState::Idle) mqtt_down(now_ms, false, a);
        break;
      case LinkEvent::None: break;
    }
  }
}

ConnectionManager::Actions ConnectionManager::tick(std::uint64_t now_ms) {
  Actions a{};
  handle_network_events(now_ms, a);
  handle_link_events(now_ms, a);

  // 1. ネットワーク
  if (!net_up_ && !net_connecting_ && now_ms >= net_retry_at_ms_) {
    ++net_attempts_;
    if (network_.connect()) {
      net_connecting_ = true;
      handle_network_events(now_ms, a);  // すぐにつながる実装なら、同じ周で MQTT に進める
    } else {
      net_retry_at_ms_ = now_ms + net_backoff_.next();
    }
  }
  // 2. 同期済みになるまで MQTT には接続しない。3. MQTT
  if (net_up_ && sync_.synced() && mqtt_ == MqttState::Idle && now_ms >= mqtt_retry_at_ms_) {
    a.want_mqtt_connect = true;
  }
  return a;
}

void ConnectionManager::connect_mqtt(const infra::ConnectOptions& options, std::uint64_t now_ms) {
  ++mqtt_attempts_;
  if (link_.connect(options)) {
    mqtt_ = MqttState::Connecting;
  } else {
    mqtt_retry_at_ms_ = now_ms + mqtt_backoff_.next();
  }
}

}  // namespace node::app
