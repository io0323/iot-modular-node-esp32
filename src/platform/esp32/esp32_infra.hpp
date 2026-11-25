// infra の ESP-IDF v5.5.6 での実装：単調時計（esp_timer）・乱数（esp_random）・ネットワーク（esp_wifi の STA）・
// 時刻の同期（esp_netif_sntp）・ログ（ESP_LOG。ホスト版の JSON Lines と同じ項目）・リセット要因の変換（P6 の決定 #100〜#103）。
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "infra/clock.hpp"
#include "infra/log_sink.hpp"
#include "infra/network.hpp"
#include "infra/random.hpp"
#include "infra/reset_cause.hpp"
#include "infra/time_sync.hpp"

namespace node::platform::esp32 {

/// @brief 単調時計（esp_timer_get_time）。
class EspClock final : public infra::IClock {
 public:
  [[nodiscard]] std::uint64_t monotonic_ms() const override;
};

/// @brief 乱数（esp_fill_random）。RF が動いた後（esp_wifi_start の後）に使うと真の乱数になる（#101）。
class EspRandom final : public infra::IRandom {
 public:
  void fill(std::span<std::uint8_t> out) override;
};

/// @brief Wi-Fi の STA。状態の変化は ESP-IDF のイベントのタスクからキューで受け、control が poll_event で取り出す。
class EspNetwork final : public infra::INetwork {
 public:
  static constexpr std::size_t kQueueDepth = 16;  ///< 状態の変化のキュー（#113）

  EspNetwork();
  EspNetwork(const EspNetwork&) = delete;
  EspNetwork& operator=(const EspNetwork&) = delete;
  EspNetwork(EspNetwork&&) = delete;
  EspNetwork& operator=(EspNetwork&&) = delete;
  ~EspNetwork() override = default;

  /// @brief Wi-Fi を初期化して起動する（RF が動く。接続はしない）。app_main から1回だけ呼ぶ。
  /// @param ssid SSID
  /// @param password パスワード
  /// @return 起動できなければ失敗
  support::Result<void> start(std::string_view ssid, std::string_view password);

  support::Result<void> connect() override;
  [[nodiscard]] bool up() const override { return up_; }
  infra::NetworkEvent poll_event() override;

 private:
  static void on_event(void* arg, const char* base, std::int32_t id, void* data);

  StaticQueue_t queue_storage_{};
  std::array<std::uint8_t, kQueueDepth * sizeof(infra::NetworkEvent)> queue_buf_{};
  QueueHandle_t queue_;
  bool up_{false};          ///< control が poll_event で見た状態
  bool handler_up_{false};  ///< イベントのタスクだけが使う（Down と ConnectFailed を分ける）
};

/// @brief 時刻の同期（esp_netif_sntp）。一度同期したら以後も同期済み（#67）。
class EspTimeSync final : public infra::ITimeSync {
 public:
  /// @brief SNTP を始める（サーバー2つ。#103）。app_main から1回だけ呼ぶ。
  /// @param server1 1つ目（MQTT のブローカーと同じホスト）
  /// @param server2 2つ目（pool.ntp.org）
  /// @return 始められなければ失敗
  static support::Result<void> start(const char* server1, const char* server2);

  [[nodiscard]] bool synced() const override;
  [[nodiscard]] std::int64_t utc_offset_ms() const override;

 private:
  mutable bool synced_{false};
};

/// @brief ESP_LOG に JSON の1行で書く（{"ts":…,"ev":…, 中身の項目}。ホスト版の JSON Lines と同じ項目）。
class EspLogSink final : public infra::ILogSink {
 public:
  void write(std::string_view event, std::string_view fields_json) override;

 private:
  std::array<char, 2400> line_{};
};

/// @brief esp_reset_reason() を機器に依らない列挙に変える（#100・#112）。
/// @return リセット要因
infra::PlatformResetCause reset_cause();

/// @brief esp_reset_reason() の列挙の名前（BOOT の detail.platform_reason。例："ESP_RST_PANIC"）。
/// @return 名前
std::string_view reset_reason_name();

}  // namespace node::platform::esp32
