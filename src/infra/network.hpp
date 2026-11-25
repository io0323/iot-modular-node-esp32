#pragma once

#include <cstdint>

#include "support/result.hpp"

namespace node::infra {

/// @brief ネットワークの状態の変化。
enum class NetworkEvent : std::uint8_t { None, Up, Down, ConnectFailed };

/// @brief ネットワーク（Wi-Fi）の取り出し口（P4 の決定 #64）。ESP-IDF の実装は P6。
class INetwork {
 public:
  virtual ~INetwork() = default;
  /// @brief 接続を始める（結果は poll_event で受け取る）。
  virtual support::Result<void> connect() = 0;
  /// @brief 使えるか。
  [[nodiscard]] virtual bool up() const = 0;
  /// @brief 状態の変化（使えるようになった・落ちた・接続できなかった）を1件取り出す。なければ None。
  virtual NetworkEvent poll_event() = 0;
};

}  // namespace node::infra
