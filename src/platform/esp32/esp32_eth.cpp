#include "platform/esp32/esp32_eth.hpp"

#include "esp_event.h"
#include "esp_netif.h"
#include "platform/esp32/rtos.hpp"
#include "sdkconfig.h"
#if CONFIG_NODE_NET_QEMU_ETH
#include "esp_eth.h"
#include "esp_eth_mac_openeth.h"
#endif

namespace node::platform::esp32 {

using support::ErrorCode;
using support::make_error;
using support::Result;

EspEthNetwork::EspEthNetwork()
    : queue_(rtos::queue_create_static(kQueueDepth, sizeof(infra::NetworkEvent), queue_buf_.data(), &queue_storage_)) {}

void EspEthNetwork::on_event(void* arg, const char* base, std::int32_t id, void* /*data*/) {
  // ESP-IDF のイベントのタスクで動く。control とはキューだけで受け渡す
  auto* self = static_cast<EspEthNetwork*>(arg);
  infra::NetworkEvent ev = infra::NetworkEvent::None;
#if CONFIG_NODE_NET_QEMU_ETH
  if (base == ETH_EVENT && id == ETHERNET_EVENT_DISCONNECTED) {
    ev = infra::NetworkEvent::Down;
  } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
    ev = infra::NetworkEvent::Up;
  }
#else
  (void)base;
  (void)id;
#endif
  if (ev != infra::NetworkEvent::None) {
    (void)rtos::queue_send(self->queue_, &ev, 0);
  }
}

Result<void> EspEthNetwork::start() {
#if CONFIG_NODE_NET_QEMU_ETH
  // ESP-IDF の examples/common_components/protocol_examples_common/eth_connect.c（CONFIG_EXAMPLE_USE_OPENETH）と同じ手順
  // ESP_NETIF_DEFAULT_ETH() と同じ（マクロの古い形のキャストを避け、esp_netif が公開する既定値をそのまま使う）
  esp_netif_config_t netif_cfg{};
  netif_cfg.base = ESP_NETIF_BASE_DEFAULT_ETH;
  netif_cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_ETH;
  esp_netif_t* netif = esp_netif_new(&netif_cfg);
  if (netif == nullptr) {
    return make_error(ErrorCode::Internal, "esp_netif_new");
  }
  const eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
  phy_cfg.autonego_timeout_ms = 100;
  esp_eth_mac_t* mac = esp_eth_mac_new_openeth(&mac_cfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_dp83848(&phy_cfg);
  const esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth = nullptr;
  if (mac == nullptr || phy == nullptr || esp_eth_driver_install(&eth_cfg, &eth) != ESP_OK) {
    return make_error(ErrorCode::Internal, "esp_eth_driver_install");
  }
  if (esp_netif_attach(netif, esp_eth_new_netif_glue(eth)) != ESP_OK) {
    return make_error(ErrorCode::Internal, "esp_netif_attach");
  }
  (void)esp_event_handler_instance_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &EspEthNetwork::on_event, this,
                                            nullptr);
  (void)esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &EspEthNetwork::on_event, this, nullptr);
  if (esp_eth_start(eth) != ESP_OK) {
    return make_error(ErrorCode::Internal, "esp_eth_start");
  }
  return support::ok();
#else
  return make_error(ErrorCode::Internal, "CONFIG_NODE_NET_QEMU_ETH is off");
#endif
}

infra::NetworkEvent EspEthNetwork::poll_event() {
  infra::NetworkEvent ev = infra::NetworkEvent::None;
  if (!rtos::queue_receive(queue_, &ev, 0)) {
    return infra::NetworkEvent::None;
  }
  up_ = ev == infra::NetworkEvent::Up;
  return ev;
}

}  // namespace node::platform::esp32
