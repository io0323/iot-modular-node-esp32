#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/command_service.hpp"
#include "app/connection_manager.hpp"
#include "app/control_arbiter.hpp"
#include "app/event_service.hpp"
#include "app/manual_control.hpp"
#include "app/mode_manager.hpp"
#include "app/publisher.hpp"
#include "app/rule_engine.hpp"
#include "app/sensor_poller.hpp"
#include "app/state_service.hpp"
#include "app/telemetry_service.hpp"
#include "app/ui_service.hpp"
#include "domain/device.hpp"
#include "domain/io.hpp"
#include "domain/reading_store.hpp"
#include "infra/clock.hpp"
#include "infra/envelope.hpp"
#include "infra/log_sink.hpp"
#include "infra/mqtt_link.hpp"
#include "infra/network.hpp"
#include "infra/random.hpp"
#include "infra/time_sync.hpp"
#include "infra/topic.hpp"

namespace node::app {

/// @brief Node の設定。文字列の寿命はプログラム全体。
struct NodeConfig {
  std::string_view device_id{"mcu-cpp-01"};             ///< device_id（MQTT の client id も同じ）
  std::string_view site{infra::Topics::kDefaultSite};  ///< トピックの site
  std::string_view username{"mcu-cpp-01"};  ///< MQTT のユーザー名は device_id と同じ（ACL が %u を使う）
  std::string_view password;           ///< MQTT のパスワード
  std::string_view fw_version{"0.1"};  ///< state の fw_version
  std::string_view reset_reason{"power_on"};  ///< BOOT の detail.reset_reason
  std::string_view platform_reason{"HOST"};   ///< BOOT の detail.platform_reason
  std::uint16_t keepalive_s{10};          ///< MQTT の keepalive（秒）
  std::uint32_t diag_interval_ms{10000};  ///< 診断のログ（diag）を出す間隔
};

/// @brief 作品02のアプリ全体。サービスを組み立て、tick で1周ずつ動かす。アプリのロジックは1つのスレッドだけで動かす。
///
/// tick の順番：接続（ConnectionManager）→ 計測（SensorPoller）→ 手動モードのタイムアウト → ボタン → 受信した cmd
/// → ローカルルール → 調停（ControlArbiter）→ telemetry → 送り直し待ちとイベントの送信。
class Node {
 public:
  static constexpr std::size_t kMaxInboundPerTick = 16;  ///< 1周で読む cmd の上限

  /// @brief サービスを組み立てる（機器の初期化と接続は start と tick で行う）。
  /// @param config 設定
  /// @param registry 機器の一覧
  /// @param sensor_io センサーのハードに触る部分（ホスト版は drivers::SensorIo、ターゲット版は sense のタスクへのキュー）
  /// @param ui_io UI のハードに触る部分（ホスト版は drivers::UiIo、ターゲット版は ui のタスクへのキュー）
  /// @param link MQTT の接続
  /// @param network ネットワーク（Wi-Fi）
  /// @param sync 時刻の同期
  /// @param clock 単調時計
  /// @param rng 乱数（boot_id・msg_id）
  /// @param log ログ
  Node(const NodeConfig& config, domain::DeviceRegistry registry, domain::ISensorIo& sensor_io, domain::IUiIo& ui_io,
       infra::IMqttLink& link, infra::INetwork& network,
       const infra::ITimeSync& sync, const infra::IClock& clock, infra::IRandom& rng, infra::ILogSink& log);

  /// @brief 機器を初期化し、BOOT をためる。
  /// @return 起動を続けられなければ失敗（機器の故障は起動を止めない）
  support::Result<void> start();
  /// @brief 1周分動かす（10ms ごとに呼ぶ）。
  void tick();
  /// @brief 正常終了の前に offline の state を送る（seq を進めない）。
  void shutdown();

  /// @brief ボタンの短押し（P3 で GPIO から呼ぶ。P2 はテストとデバッグ入力から）。
  void press_button();
  /// @brief 本体の手動操作（手動モードのときだけ受け付ける）。
  /// @param id アクチュエーターの ID
  /// @param level 出力（0–1000‰）
  /// @return 受け付けたか、受け付けなかった理由
  ManualControl::Outcome set_manual(std::string_view id, domain::Level level);

  /// @brief この起動の boot_id。
  /// @return 8 桁の16進数
  [[nodiscard]] std::string_view boot_id() const { return envelope_.boot_id(); }
  /// @brief いまのモード。
  /// @return モード
  [[nodiscard]] domain::Mode mode() const { return modes_.mode(); }
  /// @brief 調停（テスト・表示用）。
  /// @return ControlArbiter
  [[nodiscard]] const ControlArbiter& arbiter() const { return arbiter_; }
  /// @brief 最新の計測値。
  /// @return ReadingStore
  [[nodiscard]] const domain::ReadingStore& readings() const { return store_; }
  /// @brief 送信の窓口（カウンター）。
  /// @return Publisher
  [[nodiscard]] const Publisher& publisher() const { return publisher_; }
  /// @brief 読めずに捨てた cmd の件数。
  /// @param r 理由
  /// @return 件数
  [[nodiscard]] std::uint32_t discarded(infra::DiscardReason r) const {
    return discards_[static_cast<std::size_t>(r)];
  }
  /// @brief 送っていないイベントの件数。
  /// @return 件数
  [[nodiscard]] std::size_t pending_events() const { return events_.pending(); }
  /// @brief イベントの保持。
  /// @return EventService
  [[nodiscard]] const EventService& events() const { return events_; }
  /// @brief 接続の状態機械。
  /// @return ConnectionManager
  [[nodiscard]] const ConnectionManager& connection() const { return connection_; }
  /// @brief 本体の UI。
  /// @return UiService
  [[nodiscard]] const UiService& ui() const { return ui_; }
  /// @brief 本体の手動操作。
  /// @return ManualControl
  [[nodiscard]] const ManualControl& manual() const { return manual_; }

 private:
  void handle_connection(std::uint64_t now);
  void try_connect(std::uint64_t now);
  void on_connected(std::uint64_t now);
  void on_lost(std::uint64_t now);
  void handle_inbound(std::uint64_t now);
  void apply_outcome(const CommandOutcome& outcome);
  void on_mode_change(const ModeChange& change);
  void apply_mode_state();
  void run_rules(std::uint64_t now);
  void post_sensor_events(const SensorPoller::Result& r);
  void post_interlock_events(const ControlArbiter::InterlockChanges& changes);
  void publish_state();
  std::string_view build_offline_state(std::array<char, Publisher::kMessageMax>& buf);
  void log_fields(std::string_view event, std::string_view key, std::string_view value);
  void log_diag();

  NodeConfig config_;
  domain::DeviceRegistry registry_;
  domain::ISensorIo& sensor_io_;
  domain::IUiIo& ui_io_;
  infra::IMqttLink& link_;
  const infra::IClock& clock_;
  infra::ILogSink& log_;

  infra::EnvelopeFactory envelope_;
  infra::Topics topics_;
  Publisher publisher_;
  domain::ReadingStore store_{};
  ControlArbiter arbiter_;
  ModeManager modes_;
  ManualControl manual_;
  RuleEngine rules_;
  SensorPoller poller_;
  CommandService commands_;
  EventService events_{};
  StateService state_;
  TelemetryService telemetry_;
  UiService ui_;
  ConnectionManager connection_;

  std::uint64_t next_diag_ms_{0};
  bool reported_pre_connect_drops_{false};
  SensorPoller::Result poll_{};
  RuleEngine::Proposals proposals_{};
  std::array<char, Publisher::kMessageMax> will_buf_{};
  std::array<char, Publisher::kMessageMax> work_buf_{};
  std::array<char, Publisher::kMessageMax + 256> log_buf_{};
  infra::InboundMessage inbound_{};
  std::array<std::uint32_t, 3> discards_{};
  std::uint32_t wrong_topic_{0};
};

}  // namespace node::app
