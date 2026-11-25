#include "platform/esp32/esp32_mqtt.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>

#include "esp_log.h"
#include "platform/esp32/rtos.hpp"

namespace node::platform::esp32 {

using support::ErrorCode;
using support::make_error;
using support::Result;

namespace {


// ---- 受け渡し口の大きさ（#113）
constexpr std::size_t kCommandRingBytes = 16 * 1024;
constexpr std::size_t kInboundRingBytes = 8 * 1024;
constexpr UBaseType_t kLinkEventDepth = 16;
constexpr UBaseType_t kDeliveryDepth = 32;
constexpr UBaseType_t kMqttEventDepth = 32;

/// 依頼の種類
enum class Cmd : std::uint8_t { Connect, Disconnect, Subscribe, Publish };

/// 依頼の頭（この後に、count 個の文字列を NUL つきで並べる）
struct CmdHeader {
  Cmd cmd{Cmd::Publish};
  std::uint8_t qos{0};
  std::uint8_t retain{0};
  std::uint8_t count{0};
  std::uint32_t token{0};
  std::uint16_t keepalive_s{0};
  std::array<std::uint16_t, 5> lens{};
};

/// esp-mqtt のタスクから comm へのイベント
struct MqttEvent {
  enum class Kind : std::uint8_t { Connected, Disconnected, Published } kind{Kind::Connected};
  int msg_id{0};
};

/// comm から control への PUBACK の知らせ
struct Delivery {
  std::uint32_t token{0};
  bool delivered{false};  ///< false：送れなかった（確認待ちの数だけ減らす）
};

/// 受信の頭（この後に topic と payload を並べる）
struct InboundHeader {
  std::uint16_t topic_len{0};
  std::uint16_t payload_len{0};
};

bool send_command(RingbufHandle_t ring, CmdHeader h, std::initializer_list<std::string_view> strs) {
  std::size_t total = sizeof(CmdHeader);
  std::size_t i = 0;
  for (std::string_view s : strs) {
    if (i >= h.lens.size() || s.size() > 0xFFFFU) {
      return false;
    }
    h.lens[i++] = static_cast<std::uint16_t>(s.size());
    total += s.size() + 1;
  }
  h.count = static_cast<std::uint8_t>(i);
  void* p = nullptr;
  if (xRingbufferSendAcquire(ring, &p, total, 0) != rtos::kTrue || p == nullptr) {
    return false;  // いっぱい
  }
  auto* out = static_cast<std::uint8_t*>(p);
  std::memcpy(out, &h, sizeof(h));
  out += sizeof(h);
  for (std::string_view s : strs) {
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = 0;
    out += s.size() + 1;
  }
  return xRingbufferSendComplete(ring, p) == rtos::kTrue;
}

using Strings = std::array<std::string_view, 5>;

/// 受信した電文を control へのリングバッファに積む（esp-mqtt のタスクで動く）。分割された電文（バッファより大きい）と、
/// いっぱいのときは捨てる。cmd はバッファ（2048）に収まる
void push_inbound(RingbufHandle_t ring, const esp_mqtt_event_t& e) {
  if (e.current_data_offset != 0 || e.data_len != e.total_data_len || e.topic_len < 0 || e.data_len < 0) {
    return;
  }
  const InboundHeader h{static_cast<std::uint16_t>(e.topic_len), static_cast<std::uint16_t>(e.data_len)};
  void* p = nullptr;
  const std::size_t total = sizeof(h) + h.topic_len + h.payload_len;
  if (xRingbufferSendAcquire(ring, &p, total, 0) != rtos::kTrue || p == nullptr) {
    return;
  }
  auto* out = static_cast<std::uint8_t*>(p);
  std::memcpy(out, &h, sizeof(h));
  std::memcpy(out + sizeof(h), e.topic, h.topic_len);
  std::memcpy(out + sizeof(h) + h.topic_len, e.data, h.payload_len);
  (void)xRingbufferSendComplete(ring, p);
}

/// 依頼の文字列を取り出す（out は item の中を指す）
void strings_of(const CmdHeader& h, const std::uint8_t* item, std::size_t len, Strings& out) {
  out = {};
  std::size_t off = sizeof(CmdHeader);
  for (std::size_t i = 0; i < h.count && i < out.size(); ++i) {
    if (off + h.lens[i] + 1 > len) {
      break;
    }
    out[i] = std::string_view(reinterpret_cast<const char*>(item + off), h.lens[i]);
    off += h.lens[i] + 1U;
  }
}

}  // namespace

MqttChannels create_mqtt_channels() {
  alignas(4) static std::array<std::uint8_t, kCommandRingBytes> command_buf{};
  alignas(4) static std::array<std::uint8_t, kInboundRingBytes> inbound_buf{};
  static StaticRingbuffer_t command_ring{};
  static StaticRingbuffer_t inbound_ring{};
  static std::array<std::uint8_t, kLinkEventDepth * sizeof(infra::LinkEvent)> link_buf{};
  static std::array<std::uint8_t, kDeliveryDepth * sizeof(Delivery)> delivery_buf{};
  static std::array<std::uint8_t, kMqttEventDepth * sizeof(MqttEvent)> mqtt_buf{};
  static StaticQueue_t link_q{};
  static StaticQueue_t delivery_q{};
  static StaticQueue_t mqtt_q{};
  MqttChannels ch{};
  ch.commands = xRingbufferCreateStatic(command_buf.size(), RINGBUF_TYPE_NOSPLIT, command_buf.data(), &command_ring);
  ch.inbound = xRingbufferCreateStatic(inbound_buf.size(), RINGBUF_TYPE_NOSPLIT, inbound_buf.data(), &inbound_ring);
  ch.link_events = rtos::queue_create_static(kLinkEventDepth, sizeof(infra::LinkEvent), link_buf.data(), &link_q);
  ch.deliveries = rtos::queue_create_static(kDeliveryDepth, sizeof(Delivery), delivery_buf.data(), &delivery_q);
  ch.mqtt_events = rtos::queue_create_static(kMqttEventDepth, sizeof(MqttEvent), mqtt_buf.data(), &mqtt_q);
  return ch;
}

// ================= control の側

Result<void> EspMqttLink::connect(const infra::ConnectOptions& o) {
  CmdHeader h{};
  h.cmd = Cmd::Connect;
  h.qos = static_cast<std::uint8_t>(o.will.qos);
  h.retain = o.will.retain ? 1U : 0U;
  h.keepalive_s = o.keepalive_s;
  if (!send_command(ch_.commands, h, {o.client_id, o.username, o.password, o.will.topic, o.will.payload})) {
    return make_error(ErrorCode::Busy, "mqtt command ring full");
  }
  connected_ = false;
  inflight_ = 0;
  return support::ok();
}

void EspMqttLink::disconnect(std::uint32_t /*timeout_ms*/) {
  CmdHeader h{};
  h.cmd = Cmd::Disconnect;
  (void)send_command(ch_.commands, h, {});
  connected_ = false;
  inflight_ = 0;
}

Result<std::uint32_t> EspMqttLink::publish(std::string_view topic, std::string_view payload, infra::Qos qos,
                                           bool retain) {
  if (!connected_) {
    return make_error(ErrorCode::NotConnected, "not connected");
  }
  const bool qos1 = qos == infra::Qos::AtLeastOnce;
  if (qos1 && inflight_ >= kMaxInflight) {
    return make_error(ErrorCode::Busy, "inflight limit");
  }
  CmdHeader h{};
  h.cmd = Cmd::Publish;
  h.qos = static_cast<std::uint8_t>(qos);
  h.retain = retain ? 1U : 0U;
  h.token = qos1 ? next_token_ : 0U;
  if (!send_command(ch_.commands, h, {topic, payload})) {
    return make_error(ErrorCode::Busy, "mqtt command ring full");
  }
  if (!qos1) {
    return std::uint32_t{0};
  }
  ++inflight_;
  const std::uint32_t token = next_token_;
  next_token_ = next_token_ == 0xFFFFFFFFU ? 1U : next_token_ + 1U;
  return token;
}

Result<void> EspMqttLink::subscribe(std::string_view topic, infra::Qos qos) {
  CmdHeader h{};
  h.cmd = Cmd::Subscribe;
  h.qos = static_cast<std::uint8_t>(qos);
  if (!send_command(ch_.commands, h, {topic})) {
    return make_error(ErrorCode::Busy, "mqtt command ring full");
  }
  return support::ok();
}

bool EspMqttLink::poll_message(infra::InboundMessage& out) {
  std::size_t len = 0;
  void* p = xRingbufferReceive(ch_.inbound, &len, 0);
  if (p == nullptr) {
    return false;
  }
  const auto* item = static_cast<const std::uint8_t*>(p);
  InboundHeader h{};
  bool ok = len >= sizeof(h);
  if (ok) {
    std::memcpy(&h, item, sizeof(h));
    ok = sizeof(h) + h.topic_len + h.payload_len <= len;
  }
  if (ok) {
    const auto* text = reinterpret_cast<const char*>(item + sizeof(h));
    ok = out.topic.assign(std::string_view(text, h.topic_len)) &&
         out.payload.assign(std::string_view(text + h.topic_len, h.payload_len));
  }
  vRingbufferReturnItem(ch_.inbound, p);
  return ok;
}

infra::LinkEvent EspMqttLink::poll_event() {
  infra::LinkEvent ev = infra::LinkEvent::None;
  if (!rtos::queue_receive(ch_.link_events, &ev, 0)) {
    return infra::LinkEvent::None;
  }
  connected_ = ev == infra::LinkEvent::Connected;
  if (!connected_) {
    inflight_ = 0;  // clean session：切れたら確認待ちは捨てる（確認の取れなかった電文は EventService が送り直す）
  }
  return ev;
}

bool EspMqttLink::poll_delivery(std::uint32_t& token) {
  Delivery d{};
  while (rtos::queue_receive(ch_.deliveries, &d, 0)) {
    if (inflight_ > 0) {
      --inflight_;
    }
    if (d.delivered) {
      token = d.token;
      return true;
    }
  }
  return false;
}

// ================= comm の側

// 引数の型は esp_event_handler_t のとおり（const にできない。docs/quality.md の抑止の表）
// cppcheck-suppress constParameterCallback
void EspMqttComm::on_event(void* arg, esp_event_base_t /*base*/, std::int32_t id, void* data) {
  // esp-mqtt のタスクで動く。comm・control とはキューとリングバッファだけで受け渡す
  const auto* self = static_cast<const EspMqttComm*>(arg);
  const auto* e = static_cast<const esp_mqtt_event_t*>(data);
  MqttEvent ev{};
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED: ev.kind = MqttEvent::Kind::Connected; break;
    case MQTT_EVENT_DISCONNECTED: ev.kind = MqttEvent::Kind::Disconnected; break;
    case MQTT_EVENT_PUBLISHED:
      ev.kind = MqttEvent::Kind::Published;
      ev.msg_id = e->msg_id;
      break;
    case MQTT_EVENT_DATA: push_inbound(self->ch_.inbound, *e); return;
    default: return;
  }
  (void)rtos::queue_send(self->ch_.mqtt_events, &ev, 0);
}

void EspMqttComm::send_link_event(infra::LinkEvent ev) const { (void)rtos::queue_send(ch_.link_events, &ev, 0); }

void EspMqttComm::send_delivery(std::uint32_t token, bool delivered) const {
  const Delivery d{token, delivered};
  (void)rtos::queue_send(ch_.deliveries, &d, 0);
}

void EspMqttComm::stop_client() {
  if (client_ != nullptr && started_) {
    (void)esp_mqtt_client_stop(client_);  // 接続中なら DISCONNECT を送る（LWT は出ない）。outbox を消す
  }
  started_ = false;
  session_connected_ = false;
  pending_.clear();
  // 止めた後に残っている前の接続のイベントは捨てる（新しい接続のものと混ぜない）
  MqttEvent stale{};
  while (rtos::queue_receive(ch_.mqtt_events, &stale, 0)) {
  }
}

void EspMqttComm::do_connect(const std::uint8_t* item, std::size_t len) {
  CmdHeader h{};
  std::memcpy(&h, item, sizeof(h));
  Strings s{};
  strings_of(h, item, len, s);
  stop_client();
  if (!keep_strings(s)) {
    send_link_event(infra::LinkEvent::ConnectFailed);
    return;
  }
  const esp_mqtt_client_config_t cfg = config_of(item);
  if (!apply_config(cfg) || esp_mqtt_client_start(client_) != ESP_OK) {
    send_link_event(infra::LinkEvent::ConnectFailed);
    return;
  }
  started_ = true;
}

bool EspMqttComm::keep_strings(const std::array<std::string_view, 5>& s) {
  return client_id_.assign(s[0]) && username_.assign(s[1]) && password_.assign(s[2]) && will_topic_.assign(s[3]) &&
         will_payload_.assign(s[4]);
}

esp_mqtt_client_config_t EspMqttComm::config_of(const void* header) const {
  CmdHeader h{};
  std::memcpy(&h, header, sizeof(h));
  esp_mqtt_client_config_t cfg{};
  cfg.broker.address.uri = uri_;
  cfg.credentials.client_id = client_id_.c_str();
  cfg.credentials.username = username_.c_str();
  cfg.credentials.authentication.password = password_.c_str();
  cfg.session.last_will.topic = will_topic_.c_str();
  cfg.session.last_will.msg = will_payload_.c_str();
  cfg.session.last_will.msg_len = static_cast<int>(will_payload_.size());
  cfg.session.last_will.qos = h.qos;
  cfg.session.last_will.retain = h.retain;
  cfg.session.disable_clean_session = false;  // clean session（#104）
  cfg.session.keepalive = h.keepalive_s;
  cfg.network.disable_auto_reconnect = true;  // ConnectionManager が再接続する（#104）
  cfg.buffer.size = kBufferSize;
  return cfg;
}

bool EspMqttComm::apply_config(const esp_mqtt_client_config_t& cfg) {
  if (client_ != nullptr) {
    return esp_mqtt_set_config(client_, &cfg) == ESP_OK;
  }
  client_ = esp_mqtt_client_init(&cfg);
  return client_ != nullptr &&
         esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &EspMqttComm::on_event, this) == ESP_OK;
}

void EspMqttComm::do_publish(const std::uint8_t* item, std::size_t len) {
  CmdHeader h{};
  std::memcpy(&h, item, sizeof(h));
  Strings s{};
  strings_of(h, item, len, s);
  if (!started_ || !session_connected_ || !topic_.assign(s[0])) {
    if (h.token != 0) {
      send_delivery(h.token, false);
    }
    return;
  }
  const int msg_id = esp_mqtt_client_publish(client_, topic_.c_str(), s[1].data(), static_cast<int>(s[1].size()), h.qos,
                                             h.retain);
  if (h.token == 0) {
    return;  // QoS 0
  }
  if (msg_id <= 0 || !pending_.push_back(Pending{msg_id, h.token})) {
    send_delivery(h.token, false);
  }
}

void EspMqttComm::handle_command(const std::uint8_t* item, std::size_t len) {
  if (len < sizeof(CmdHeader)) {
    return;
  }
  CmdHeader h{};
  std::memcpy(&h, item, sizeof(h));
  switch (h.cmd) {
    case Cmd::Connect: do_connect(item, len); return;
    case Cmd::Disconnect: stop_client(); return;
    case Cmd::Subscribe: {
      Strings s{};
  strings_of(h, item, len, s);
      if (started_ && session_connected_ && topic_.assign(s[0])) {
        (void)esp_mqtt_client_subscribe_single(client_, topic_.c_str(), h.qos);
      }
      return;
    }
    case Cmd::Publish: do_publish(item, len); return;
  }
}

void EspMqttComm::forward_events() {
  MqttEvent ev{};
  while (rtos::queue_receive(ch_.mqtt_events, &ev, 0)) {
    switch (ev.kind) {
      case MqttEvent::Kind::Connected:
        session_connected_ = true;
        send_link_event(infra::LinkEvent::Connected);
        break;
      case MqttEvent::Kind::Disconnected:
        // 接続できなかった（MQTT_EVENT_ERROR の後の DISCONNECTED）か、接続の後に切れたか
        send_link_event(session_connected_ ? infra::LinkEvent::Disconnected : infra::LinkEvent::ConnectFailed);
        session_connected_ = false;
        pending_.clear();
        break;
      case MqttEvent::Kind::Published: {
        const auto* it = std::ranges::find_if(pending_, [&ev](const Pending& p) { return p.msg_id == ev.msg_id; });
        if (it != pending_.end()) {
          send_delivery(it->token, true);
          pending_.erase_at(static_cast<std::size_t>(it - pending_.begin()));
        }
        break;
      }
    }
  }
}

void EspMqttComm::step(TickType_t wait) {
  std::size_t len = 0;
  void* p = xRingbufferReceive(ch_.commands, &len, wait);
  if (p != nullptr) {
    handle_command(static_cast<const std::uint8_t*>(p), len);
    vRingbufferReturnItem(ch_.commands, p);
  }
  forward_events();
}

}  // namespace node::platform::esp32
