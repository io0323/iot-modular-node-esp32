#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "support/fixed_string.hpp"
#include "support/result.hpp"

namespace node::infra {

/// @brief MQTT の QoS。
enum class Qos : std::uint8_t { AtMostOnce = 0, AtLeastOnce = 1 };

/// @brief LWT（接続が切れたときにブローカーが代わりに発行する電文）。
struct Will {
  std::string_view topic;
  std::string_view payload;
  Qos qos{Qos::AtLeastOnce};
  bool retain{true};
};

/// @brief 接続の設定。中身の文字列は connect の呼び出しの間だけ有効であればよい（実装が写しを持つ）。
struct ConnectOptions {
  std::string_view client_id;
  std::string_view username;
  std::string_view password;
  Will will;
  std::uint16_t keepalive_s{10};
};

/// @brief 接続状態の変化。
enum class LinkEvent : std::uint8_t { None, Connected, ConnectFailed, Disconnected };

/// @brief 受信した電文。
struct InboundMessage {
  static constexpr std::size_t kTopicMax = 128;
  static constexpr std::size_t kPayloadMax = 2048;
  support::FixedString<kTopicMax> topic{};
  support::FixedString<kPayloadMax> payload{};
};

/// @brief MQTT の接続（自作 MQTT アダプタの取り込み元が決まるまでの仮の抽象。open-questions #5）。
///
/// QoS 1 の確認待ち（PUBACK 待ち）の件数を実装が数え、kMaxInflight に達したら publish は Busy を返す
/// （共通仕様 第4章「送信量の上限」）。QoS 1 の publish はトークンを返し、PUBACK を受けたトークンを poll_delivery で返す。
/// 切断したら確認待ちは捨てる（clean session）。切断の時点で PUBACK を受けていないトークンは「確認が取れなかった」もの。
class IMqttLink {
 public:
  static constexpr std::size_t kMaxInflight = 32;

  virtual ~IMqttLink() = default;
  /// @brief 接続を始める（結果は poll_event で受け取る）。
  virtual support::Result<void> connect(const ConnectOptions& options) = 0;
  /// @brief 切断する（LWT は発行されない）。
  /// @param timeout_ms 切断を待つ上限
  virtual void disconnect(std::uint32_t timeout_ms) = 0;
  /// @brief 発行する。
  /// @return QoS 1 はトークン（1 以上）、QoS 0 は 0。確認待ちが上限なら Busy、未接続なら NotConnected
  virtual support::Result<std::uint32_t> publish(std::string_view topic, std::string_view payload, Qos qos,
                                                 bool retain) = 0;
  /// @brief 購読する。
  virtual support::Result<void> subscribe(std::string_view topic, Qos qos) = 0;
  /// @brief 受信した電文を1件取り出す。
  /// @param out 書き込み先
  /// @return 取り出せたら true
  virtual bool poll_message(InboundMessage& out) = 0;
  /// @brief 接続状態の変化を1件取り出す。なければ LinkEvent::None。
  virtual LinkEvent poll_event() = 0;
  /// @brief PUBACK を受けた QoS 1 の電文のトークンを1件取り出す。
  /// @return 取り出せたら true
  virtual bool poll_delivery(std::uint32_t& token) = 0;
  /// @brief 接続中か。
  [[nodiscard]] virtual bool connected() const = 0;
  /// @brief QoS 1 の確認待ちの件数。
  [[nodiscard]] virtual std::size_t inflight() const = 0;
};

}  // namespace node::infra
