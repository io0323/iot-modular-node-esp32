#include "platform/esp32/app_main.hpp"

#include <string_view>

#include "app/node.hpp"
#include "config/esp32_pins.hpp"
#include "config/hw.hpp"
#include "drivers/sensor_io.hpp"
#include "drivers/ui_io.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "platform/esp32/esp32_eth.hpp"
#include "platform/esp32/esp32_hal.hpp"
#include "platform/esp32/esp32_infra.hpp"
#include "platform/esp32/esp32_mqtt.hpp"
#include "platform/esp32/esp32_tasks.hpp"
#include "sdkconfig.h"
#include "support/fixed_string.hpp"

namespace node::platform::esp32 {

namespace {

constexpr const char* kTag = "node";

/// "mqtt://host:port" のホストの部分（SNTP の1つ目のサーバー。#103）
support::FixedString<128> host_of(std::string_view uri) {
  if (const std::size_t p = uri.find("://"); p != std::string_view::npos) {
    uri.remove_prefix(p + 3);
  }
  uri = uri.substr(0, uri.find_first_of(":/"));
  support::FixedString<128> out;
  (void)out.assign(uri);
  return out;
}

void init_nvs() {
  // Wi-Fi のために呼ぶ（設定の保存には使わない。#108）
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    (void)nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_flash_init: %d", err);
  }
}

/// 1. 機器：hal を作り、各アクチュエーターを初期化して 0 を出す（#99。タスクを起こす前）
domain::DeviceRegistry init_devices() {
  static EspI2cBus i2c(config::esp32::kI2cSdaGpio, config::esp32::kI2cSclGpio);
  static EspMutex i2c_mutex;
  static EspAdc adc(config::esp32::kAdcPins);
  static EspPwm pwm(config::esp32::kPwmPins);
  static EspGpio gpio(config::esp32::kGpioPins);
  static EspTimer timer;
  if (!i2c.init()) {
    ESP_LOGE(kTag, "i2c init failed");
  }
  if (!adc.init()) {
    ESP_LOGE(kTag, "adc init failed");
  }
  static config::HwRig rig(config::HwInterfaces{i2c, i2c_mutex, adc, pwm, gpio, timer});
  const domain::DeviceRegistry registry = rig.registry();
  for (const domain::ActuatorBinding& b : registry.actuators()) {
    if (!b.actuator->init()) {
      ESP_LOGE(kTag, "actuator init failed: %.*s", static_cast<int>(b.actuator->id().size()), b.actuator->id().data());
    }
  }
  return registry;
}

/// 2. Wi-Fi を起動する（RF が動き、esp_random が真の乱数になる。#101）と SNTP（#103）。
/// 試験用の NODE_NET_QEMU_ETH では、Wi-Fi の代わりに QEMU の OpenCores Ethernet を使う（#120）
infra::INetwork& start_network() {
  (void)esp_netif_init();
  (void)esp_event_loop_create_default();
#if CONFIG_NODE_NET_QEMU_ETH
  static EspEthNetwork network;
  if (!network.start()) {
    ESP_LOGE(kTag, "ethernet start failed");
  }
#else
  static EspNetwork network;
  if (!network.start(CONFIG_NODE_WIFI_SSID, CONFIG_NODE_WIFI_PASSWORD)) {
    ESP_LOGE(kTag, "wifi start failed");
  }
#endif
  static const support::FixedString<128> broker_host = host_of(CONFIG_NODE_MQTT_URL);
  const char* const sntp1 =
      std::string_view(CONFIG_NODE_SNTP_SERVER1).empty() ? broker_host.c_str() : CONFIG_NODE_SNTP_SERVER1;
  if (!EspTimeSync::start(sntp1, CONFIG_NODE_SNTP_SERVER2)) {
    ESP_LOGE(kTag, "sntp start failed");
  }
  return network;
}

/// Node の設定（Kconfig とリセット要因。#100・#102）
app::NodeConfig node_config() {
  app::NodeConfig config{};
  config.site = std::string_view(CONFIG_NODE_SITE).empty() ? infra::Topics::kDefaultSite : CONFIG_NODE_SITE;
  config.password = CONFIG_NODE_MQTT_PASSWORD;
  config.reset_reason = infra::reset_reason_of(reset_cause());
  config.platform_reason = reset_reason_name();
  return config;
}

}  // namespace

void run() {
  init_nvs();
  const domain::DeviceRegistry registry = init_devices();
  infra::INetwork& network = start_network();

  // ---- 3. Node を作る（boot_id はここで esp_random から決まる。#101）
  static EspClock clock;
  static EspRandom rng;
  static EspTimeSync time_sync;
  static EspLogSink log;
  static const MqttChannels mqtt_channels = create_mqtt_channels();
  static EspMqttLink link(mqtt_channels);
  static const IoQueues io_queues = create_io_queues();
  static QueueSensorIo sensor_proxy(io_queues);
  static QueueUiIo ui_proxy(io_queues);
  static app::Node node(node_config(), registry, sensor_proxy, ui_proxy, link, network, time_sync, clock, rng, log);

  // ---- 4. UI の機器を初期化し、Node を起動する（センサーの初期化と BOOT。タスクを起こす前に1回だけ）
  static drivers::UiIo ui(registry.ui());
  if (!ui.init()) {
    ESP_LOGE(kTag, "ui init failed");
  }
  if (auto r = node.start(); !r) {
    ESP_LOGE(kTag, "start failed: %.*s", static_cast<int>(r.error().what.size()), r.error().what.data());
  }

  // ---- 5. タスクを起こす（#96）
  static drivers::SensorIo sensors(registry.sensors());
  static EspMqttComm comm(mqtt_channels, CONFIG_NODE_MQTT_URL);
  static TaskContext ctx{};
  ctx.node = &node;
  ctx.log = &log;
  ctx.sensors = &sensors;
  ctx.ui = &ui;
  ctx.comm = &comm;
  ctx.queues = io_queues;
  start_tasks(ctx);
}

}  // namespace node::platform::esp32
