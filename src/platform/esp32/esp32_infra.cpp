#include "platform/esp32/esp32_infra.hpp"

#include <sys/time.h>

#include <algorithm>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "infra/formats.hpp"
#include "infra/json_writer.hpp"
#include "platform/esp32/rtos.hpp"

namespace node::platform::esp32 {

using support::ErrorCode;
using support::make_error;
using support::Result;

std::uint64_t EspClock::monotonic_ms() const { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U; }

void EspRandom::fill(std::span<std::uint8_t> out) { esp_fill_random(out.data(), out.size()); }

// ---------------- Wi-Fi

EspNetwork::EspNetwork()
    : queue_(rtos::queue_create_static(kQueueDepth, sizeof(infra::NetworkEvent), queue_buf_.data(), &queue_storage_)) {}

void EspNetwork::on_event(void* arg, const char* base, std::int32_t id, void* /*data*/) {
  // ESP-IDF のイベントのタスクで動く。control とはキューだけで受け渡す
  auto* self = static_cast<EspNetwork*>(arg);
  infra::NetworkEvent ev = infra::NetworkEvent::None;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    ev = self->handler_up_ ? infra::NetworkEvent::Down : infra::NetworkEvent::ConnectFailed;
    self->handler_up_ = false;
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ev = infra::NetworkEvent::Up;
    self->handler_up_ = true;
  }
  if (ev != infra::NetworkEvent::None) {
    (void)rtos::queue_send(self->queue_, &ev, 0);
  }
}

Result<void> EspNetwork::start(std::string_view ssid, std::string_view password) {
  if (esp_netif_create_default_wifi_sta() == nullptr) {
    return make_error(ErrorCode::Internal, "esp_netif_create_default_wifi_sta");
  }
  const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) {
    return make_error(ErrorCode::Internal, "esp_wifi_init");
  }
  (void)esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &EspNetwork::on_event, this,
                                            nullptr);
  (void)esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &EspNetwork::on_event, this, nullptr);
  wifi_config_t cfg{};
  std::memcpy(cfg.sta.ssid, ssid.data(), std::min(ssid.size(), sizeof(cfg.sta.ssid) - 1));
  std::memcpy(cfg.sta.password, password.data(), std::min(password.size(), sizeof(cfg.sta.password) - 1));
  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;  // WPA2-PSK 以上（#113）
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    return make_error(ErrorCode::Internal, "esp_wifi_set_config");
  }
  if (esp_wifi_start() != ESP_OK) {
    return make_error(ErrorCode::Internal, "esp_wifi_start");
  }
  return support::ok();
}

Result<void> EspNetwork::connect() {
  if (esp_wifi_connect() != ESP_OK) {
    return make_error(ErrorCode::IoError, "esp_wifi_connect");
  }
  return support::ok();
}

infra::NetworkEvent EspNetwork::poll_event() {
  infra::NetworkEvent ev = infra::NetworkEvent::None;
  if (!rtos::queue_receive(queue_, &ev, 0)) {
    return infra::NetworkEvent::None;
  }
  up_ = ev == infra::NetworkEvent::Up;
  return ev;
}

// ---------------- SNTP

Result<void> EspTimeSync::start(const char* server1, const char* server2) {
  esp_sntp_config_t cfg{};
  cfg.wait_for_sync = true;
  cfg.start = true;
  cfg.num_of_servers = 2;
  cfg.servers[0] = server1;
  cfg.servers[1] = server2;
  if (esp_netif_sntp_init(&cfg) != ESP_OK) {
    return make_error(ErrorCode::Internal, "esp_netif_sntp_init");
  }
  return support::ok();
}

bool EspTimeSync::synced() const {
  // 同期のたびに与えられるセマフォを、待たずに取る。一度取れたら以後は同期済み（#67）
  if (!synced_ && esp_netif_sntp_sync_wait(0) == ESP_OK) {
    synced_ = true;
  }
  return synced_;
}

std::int64_t EspTimeSync::utc_offset_ms() const {
  timeval tv{};
  (void)gettimeofday(&tv, nullptr);
  const std::int64_t utc = static_cast<std::int64_t>(tv.tv_sec) * 1000 + static_cast<std::int64_t>(tv.tv_usec) / 1000;
  return utc - esp_timer_get_time() / 1000;
}

// ---------------- ログ

void EspLogSink::write(std::string_view event, std::string_view fields_json) {
  timeval tv{};
  (void)gettimeofday(&tv, nullptr);
  const infra::TsString ts =
      infra::format_ts(static_cast<std::int64_t>(tv.tv_sec) * 1000 + static_cast<std::int64_t>(tv.tv_usec) / 1000);
  std::string_view body = fields_json;
  if (!body.empty() && body.front() == '{') {
    body.remove_prefix(1);
  }
  infra::JsonWriter w(line_);
  w.begin_object();
  w.key("ts").string(ts.view());
  w.key("ev").string(event);
  w.end_object();
  if (!w.ok()) {
    return;
  }
  // JsonWriter が閉じた "}" を外して、中身の項目をつなぐ
  std::string_view head = w.view();
  head.remove_suffix(1);
  const bool empty_body = body == "}";
  ESP_LOGI("node", "%.*s%s%.*s", static_cast<int>(head.size()), head.data(), empty_body ? "" : ",",
           static_cast<int>(body.size()), body.data());
}

// ---------------- リセット要因

infra::PlatformResetCause reset_cause() {
  using C = infra::PlatformResetCause;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return C::PowerOn;
    case ESP_RST_EXT: return C::External;
    case ESP_RST_SW: return C::Software;
    case ESP_RST_PANIC: return C::Panic;
    case ESP_RST_INT_WDT: return C::InterruptWatchdog;
    case ESP_RST_TASK_WDT: return C::TaskWatchdog;
    case ESP_RST_WDT: return C::OtherWatchdog;
    case ESP_RST_BROWNOUT: return C::Brownout;
    case ESP_RST_PWR_GLITCH: return C::PowerGlitch;
    case ESP_RST_DEEPSLEEP: return C::DeepSleep;
    default: return C::Unknown;  // ESP_RST_UNKNOWN・SDIO・USB・JTAG・EFUSE・CPU_LOCKUP など
  }
}

std::string_view reset_reason_name() {
  switch (esp_reset_reason()) {
    case ESP_RST_UNKNOWN: return "ESP_RST_UNKNOWN";
    case ESP_RST_POWERON: return "ESP_RST_POWERON";
    case ESP_RST_EXT: return "ESP_RST_EXT";
    case ESP_RST_SW: return "ESP_RST_SW";
    case ESP_RST_PANIC: return "ESP_RST_PANIC";
    case ESP_RST_INT_WDT: return "ESP_RST_INT_WDT";
    case ESP_RST_TASK_WDT: return "ESP_RST_TASK_WDT";
    case ESP_RST_WDT: return "ESP_RST_WDT";
    case ESP_RST_DEEPSLEEP: return "ESP_RST_DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "ESP_RST_BROWNOUT";
    case ESP_RST_SDIO: return "ESP_RST_SDIO";
    case ESP_RST_USB: return "ESP_RST_USB";
    case ESP_RST_JTAG: return "ESP_RST_JTAG";
    case ESP_RST_EFUSE: return "ESP_RST_EFUSE";
    case ESP_RST_PWR_GLITCH: return "ESP_RST_PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "ESP_RST_CPU_LOCKUP";
    default: return "ESP_RST_UNKNOWN";
  }
}

}  // namespace node::platform::esp32
