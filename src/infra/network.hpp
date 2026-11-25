#pragma once

#include <cstdint>

#include "support/result.hpp"

namespace node::infra {

/// @brief ネットワークの状態の変化。
enum class NetworkEvent : std::uint8_t {
  None,           ///< 変化なし
  Up,             ///< 使えるようになった
  Down,           ///< 落ちた
  ConnectFailed,  ///< 接続できなかった
};

/// @brief ネットワーク（Wi-Fi）の取り出し口（P4 の決定 #64）。ESP-IDF の実装は P6。
class INetwork {
 public:
  virtual ~INetwork() = default;
  /// @brief 接続を始める（結果は poll_event で受け取る）。
  /// @return 始められなければ失敗
  virtual support::Result<void> connect() = 0;
  /// @brief 使えるか。
  /// @return 使えれば true
  [[nodiscard]] virtual bool up() const = 0;
  /// @brief 状態の変化（使えるようになった・落ちた・接続できなかった）を1件取り出す。
  /// @return 変化。なければ NetworkEvent::None
  virtual NetworkEvent poll_event() = 0;
};

}  // namespace node::infra
