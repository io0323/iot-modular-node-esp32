// 試験用の INetwork：QEMU の OpenCores Ethernet（P7 の決定 #120）。`CONFIG_NODE_NET_QEMU_ETH` のときだけ使う。
// QEMU は ESP32-S3 の Wi-Fi を再現しないので（esp-toolchain-docs の qemu/README.md）、エミュレーターでは Ethernet で
// ネットワークにつなぐ。実機では動かない（ESP-IDF の ETH_USE_OPENETH の説明）。
#pragma once

#include <array>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "infra/network.hpp"

namespace node::platform::esp32 {

/// @brief QEMU の OpenCores Ethernet。状態の変化はイベントのタスクからキューで受ける（EspNetwork と同じ形）。
class EspEthNetwork final : public infra::INetwork {
 public:
  static constexpr std::size_t kQueueDepth = 16;  ///< 状態の変化のキュー

  EspEthNetwork();
  EspEthNetwork(const EspEthNetwork&) = delete;
  EspEthNetwork& operator=(const EspEthNetwork&) = delete;
  EspEthNetwork(EspEthNetwork&&) = delete;
  EspEthNetwork& operator=(EspEthNetwork&&) = delete;
  ~EspEthNetwork() override = default;

  /// @brief Ethernet のドライバを作って始める。app_main から1回だけ呼ぶ。
  /// @return 始められなければ失敗
  support::Result<void> start();

  /// @brief 何もしない（Ethernet はリンクが上がれば DHCP で IP を得る）。
  /// @return いつも成功
  support::Result<void> connect() override { return support::ok(); }
  [[nodiscard]] bool up() const override { return up_; }
  infra::NetworkEvent poll_event() override;

 private:
  static void on_event(void* arg, const char* base, std::int32_t id, void* data);

  StaticQueue_t queue_storage_{};
  std::array<std::uint8_t, kQueueDepth * sizeof(infra::NetworkEvent)> queue_buf_{};
  QueueHandle_t queue_;
  bool up_{false};
};

}  // namespace node::platform::esp32
