#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/command_service.hpp"
#include "app/control_arbiter.hpp"
#include "app/event_service.hpp"
#include "app/publisher.hpp"
#include "app/state_service.hpp"
#include "app/telemetry_service.hpp"
#include "domain/device.hpp"
#include "domain/reading_store.hpp"
#include "infra/clock.hpp"
#include "infra/envelope.hpp"
#include "infra/log_sink.hpp"
#include "infra/mqtt_link.hpp"
#include "infra/random.hpp"
#include "infra/topic.hpp"

namespace node::app {

/// @brief Node の設定。文字列の寿命はプログラム全体。
struct NodeConfig {
  std::string_view device_id{"mcu-cpp-01"};
  std::string_view site{infra::Topics::kDefaultSite};
  std::string_view username{"mcu-cpp-01"};  ///< MQTT のユーザー名は device_id と同じ（ACL が %u を使う）
  std::string_view password{};
  std::string_view fw_version{"0.1"};
  std::string_view reset_reason{"power_on"};  ///< BOOT の detail.reset_reason
  std::string_view platform_reason{"HOST"};   ///< BOOT の detail.platform_reason
  std::uint16_t keepalive_s{10};
  std::uint32_t reconnect_interval_ms{1000};  ///< 仮版：1秒ごとの単純な再試行（P4 で指数バックオフ）
  std::uint32_t diag_interval_ms{10000};
};

/// @brief 作品02のアプリ全体。サービスを組み立て、tick で1周ずつ動かす。アプリのロジックは1つのスレッドだけで動かす。
class Node {
 public:
  static constexpr std::size_t kMaxInboundPerTick = 16;

  Node(const NodeConfig& config, domain::DeviceRegistry registry, infra::IMqttLink& link,
       const infra::IClock& clock, infra::IRandom& rng, infra::ILogSink& log);

  /// @brief 機器を初期化し、BOOT をためる。
  support::Result<void> start();
  /// @brief 1周分動かす（10ms ごとに呼ぶ）。
  void tick();
  /// @brief 正常終了の前に offline の state を送る（seq を進めない）。
  void shutdown();

  [[nodiscard]] std::string_view boot_id() const { return envelope_.boot_id(); }
  [[nodiscard]] domain::Mode mode() const { return commands_.mode(); }
  [[nodiscard]] const ControlArbiter& arbiter() const { return arbiter_; }
  [[nodiscard]] const Publisher& publisher() const { return publisher_; }
  [[nodiscard]] std::uint32_t discarded(infra::DiscardReason r) const {
    return discards_[static_cast<std::size_t>(r)];
  }
  [[nodiscard]] std::size_t pending_events() const { return events_.pending(); }
  /// @brief いまの LWT の電文（接続のたびに作り直す）。
  [[nodiscard]] std::string_view will_payload() const { return {will_buf_.data(), will_len_}; }

 private:
  enum class LinkState : std::uint8_t { Disconnected, Connecting, Connected };

  void handle_link_events(std::uint64_t now);
  void try_connect(std::uint64_t now);
  void on_connected();
  void handle_inbound(std::uint64_t now);
  void apply_outcome(const CommandOutcome& outcome);
  void post_interlock_events(const ControlArbiter::InterlockChanges& changes);
  void publish_state();
  std::string_view build_offline_state(std::array<char, Publisher::kMessageMax>& buf);
  void log_fields(std::string_view event, std::string_view key, std::string_view value);
  void log_diag();

  NodeConfig config_;
  domain::DeviceRegistry registry_;
  infra::IMqttLink& link_;
  const infra::IClock& clock_;
  infra::ILogSink& log_;

  infra::EnvelopeFactory envelope_;
  infra::Topics topics_;
  Publisher publisher_;
  domain::ReadingStore store_{};
  ControlArbiter arbiter_;
  CommandService commands_;
  EventService events_{};
  StateService state_;
  TelemetryService telemetry_;

  LinkState link_state_{LinkState::Disconnected};
  std::uint64_t next_connect_ms_{0};
  std::uint64_t next_diag_ms_{0};
  std::array<char, Publisher::kMessageMax> will_buf_{};
  std::size_t will_len_{0};
  std::array<char, Publisher::kMessageMax> work_buf_{};
  std::array<char, Publisher::kMessageMax + 256> log_buf_{};
  infra::InboundMessage inbound_{};
  std::array<std::uint32_t, 3> discards_{};
  std::uint32_t wrong_topic_{0};
};

}  // namespace node::app
