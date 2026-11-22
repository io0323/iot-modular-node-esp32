#include "platform/host/paho_mqtt_link.hpp"

#include <MQTTAsync.h>

#include <string>

namespace node::platform {

using support::ErrorCode;
using support::make_error;
using support::Result;

// Paho の *_initializer マクロは一部の項目だけを書く集成体初期化なので、ここだけ警告を外す
#if defined(__GNUC__)
#define NODE_PAHO_INIT_BEGIN \
  _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#define NODE_PAHO_INIT_END _Pragma("GCC diagnostic pop")
#else
#define NODE_PAHO_INIT_BEGIN
#define NODE_PAHO_INIT_END
#endif

namespace {
MQTTAsync handle_of(void* p) { return static_cast<MQTTAsync>(p); }
}  // namespace

PahoMqttLink::PahoMqttLink(const std::string& server_uri, const std::string& client_id) {
  MQTTAsync client = nullptr;
  if (MQTTAsync_create(&client, server_uri.c_str(), client_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr) !=
      MQTTASYNC_SUCCESS) {
    return;
  }
  client_ = client;
  MQTTAsync_setCallbacks(
      client, this, &PahoMqttLink::on_connection_lost,
      [](void* ctx, char* topic, int len, MQTTAsync_message* m) { return on_message(ctx, topic, len, m); },
      nullptr);
}

PahoMqttLink::~PahoMqttLink() {
  if (client_ != nullptr) {
    MQTTAsync client = handle_of(client_);
    MQTTAsync_destroy(&client);
    client_ = nullptr;
  }
}

Result<void> PahoMqttLink::connect(const infra::ConnectOptions& options) {
  if (client_ == nullptr) return make_error(ErrorCode::Internal, "client not created");
  will_topic_.assign(options.will.topic);
  will_payload_.assign(options.will.payload);
  username_.assign(options.username);
  password_.assign(options.password);

  NODE_PAHO_INIT_BEGIN
  MQTTAsync_willOptions will = MQTTAsync_willOptions_initializer;
  MQTTAsync_connectOptions opts = MQTTAsync_connectOptions_initializer;
  NODE_PAHO_INIT_END
  will.topicName = will_topic_.c_str();
  will.message = will_payload_.c_str();
  will.retained = options.will.retain ? 1 : 0;
  will.qos = static_cast<int>(options.will.qos);

  opts.keepAliveInterval = options.keepalive_s;
  opts.cleansession = 1;
  opts.maxInflight = static_cast<int>(kMaxInflight);
  opts.will = &will;
  opts.username = username_.empty() ? nullptr : username_.c_str();
  opts.password = password_.empty() ? nullptr : password_.c_str();
  opts.automaticReconnect = 0;
  opts.connectTimeout = 5;
  opts.onSuccess = [](void* ctx, MQTTAsync_successData* r) { on_connect_success(ctx, r); };
  opts.onFailure = [](void* ctx, MQTTAsync_failureData* r) { on_connect_failure(ctx, r); };
  opts.context = this;
  if (MQTTAsync_connect(handle_of(client_), &opts) != MQTTASYNC_SUCCESS) {
    return make_error(ErrorCode::IoError, "MQTTAsync_connect failed");
  }
  return support::ok();
}

void PahoMqttLink::disconnect(std::uint32_t timeout_ms) {
  if (client_ == nullptr) return;
  NODE_PAHO_INIT_BEGIN
  MQTTAsync_disconnectOptions opts = MQTTAsync_disconnectOptions_initializer;
  NODE_PAHO_INIT_END
  opts.timeout = static_cast<int>(timeout_ms);
  (void)MQTTAsync_disconnect(handle_of(client_), &opts);
  connected_.store(false);
}

Result<void> PahoMqttLink::publish(std::string_view topic, std::string_view payload, infra::Qos qos, bool retain) {
  if (client_ == nullptr || !connected_.load()) return make_error(ErrorCode::NotConnected, "not connected");
  const bool qos1 = qos == infra::Qos::AtLeastOnce;
  if (qos1 && inflight_.load() >= static_cast<int>(kMaxInflight)) {
    return make_error(ErrorCode::Busy, "inflight limit");
  }
  const std::string topic_s(topic);

  NODE_PAHO_INIT_BEGIN
  MQTTAsync_message msg = MQTTAsync_message_initializer;
  MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
  NODE_PAHO_INIT_END
  msg.payload = const_cast<char*>(payload.data());  // Paho は送る前に写しを取る（const を受けない API）
  msg.payloadlen = static_cast<int>(payload.size());
  msg.qos = static_cast<int>(qos);
  msg.retained = retain ? 1 : 0;
  if (qos1) {
    ropts.onSuccess = [](void* ctx, MQTTAsync_successData* r) { on_publish_qos1_done(ctx, r); };
    ropts.onFailure = [](void* ctx, MQTTAsync_failureData* r) { on_publish_qos1_done(ctx, r); };
    ropts.context = this;
    inflight_.fetch_add(1);
  }
  const int rc = MQTTAsync_sendMessage(handle_of(client_), topic_s.c_str(), &msg, &ropts);
  if (rc != MQTTASYNC_SUCCESS) {
    if (qos1) inflight_.fetch_sub(1);
    return make_error(rc == MQTTASYNC_DISCONNECTED ? ErrorCode::NotConnected : ErrorCode::IoError,
                      "MQTTAsync_sendMessage failed");
  }
  return support::ok();
}

Result<void> PahoMqttLink::subscribe(std::string_view topic, infra::Qos qos) {
  if (client_ == nullptr || !connected_.load()) return make_error(ErrorCode::NotConnected, "not connected");
  const std::string topic_s(topic);
  NODE_PAHO_INIT_BEGIN
  MQTTAsync_responseOptions ropts = MQTTAsync_responseOptions_initializer;
  NODE_PAHO_INIT_END
  if (MQTTAsync_subscribe(handle_of(client_), topic_s.c_str(), static_cast<int>(qos), &ropts) != MQTTASYNC_SUCCESS) {
    return make_error(ErrorCode::IoError, "MQTTAsync_subscribe failed");
  }
  return support::ok();
}

bool PahoMqttLink::poll_message(infra::InboundMessage& out) {
  std::lock_guard<std::mutex> lock(mu_);
  if (inbound_count_ == 0) return false;
  out = inbound_[inbound_head_];
  inbound_head_ = (inbound_head_ + 1) % kInboundCapacity;
  --inbound_count_;
  return true;
}

infra::LinkEvent PahoMqttLink::poll_event() {
  std::lock_guard<std::mutex> lock(mu_);
  if (events_count_ == 0) return infra::LinkEvent::None;
  const infra::LinkEvent ev = events_[events_head_];
  events_head_ = (events_head_ + 1) % kEventCapacity;
  --events_count_;
  return ev;
}

void PahoMqttLink::wait_for_activity(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mu_);
  cv_.wait_for(lock, timeout, [this] { return inbound_count_ > 0 || events_count_ > 0; });
}

void PahoMqttLink::push_event(infra::LinkEvent ev) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (events_count_ < kEventCapacity) {
      events_[(events_head_ + events_count_) % kEventCapacity] = ev;
      ++events_count_;
    }
  }
  cv_.notify_one();
}

int PahoMqttLink::on_message(void* context, char* topic, int topic_len, void* message) {
  auto* self = static_cast<PahoMqttLink*>(context);
  auto* m = static_cast<MQTTAsync_message*>(message);
  const std::size_t tlen = topic_len > 0 ? static_cast<std::size_t>(topic_len) : std::char_traits<char>::length(topic);
  const std::string_view t(topic, tlen);
  const std::string_view p(static_cast<const char*>(m->payload), static_cast<std::size_t>(m->payloadlen));
  bool stored = false;
  bool oversized = false;
  {
    std::lock_guard<std::mutex> lock(self->mu_);
    if (self->inbound_count_ < kInboundCapacity) {
      infra::InboundMessage& slot = self->inbound_[(self->inbound_head_ + self->inbound_count_) % kInboundCapacity];
      if (slot.topic.assign(t) && slot.payload.assign(p)) {
        ++self->inbound_count_;
        stored = true;
      } else {
        oversized = true;
      }
    }
  }
  if (oversized) {
    self->oversized_.fetch_add(1);
  } else if (!stored) {
    self->inbound_dropped_.fetch_add(1);
  }
  MQTTAsync_freeMessage(&m);
  MQTTAsync_free(topic);
  self->cv_.notify_one();
  return 1;
}

void PahoMqttLink::on_connection_lost(void* context, char* /*cause*/) {
  auto* self = static_cast<PahoMqttLink*>(context);
  self->connected_.store(false);
  self->inflight_.store(0);
  self->push_event(infra::LinkEvent::Disconnected);
}

void PahoMqttLink::on_connect_success(void* context, void* /*response*/) {
  auto* self = static_cast<PahoMqttLink*>(context);
  self->inflight_.store(0);
  self->connected_.store(true);
  self->push_event(infra::LinkEvent::Connected);
}

void PahoMqttLink::on_connect_failure(void* context, void* /*response*/) {
  auto* self = static_cast<PahoMqttLink*>(context);
  self->connected_.store(false);
  self->push_event(infra::LinkEvent::ConnectFailed);
}

void PahoMqttLink::on_publish_qos1_done(void* context, void* /*response*/) {
  auto* self = static_cast<PahoMqttLink*>(context);
  if (self->inflight_.fetch_sub(1) <= 0) self->inflight_.store(0);
}

}  // namespace node::platform
