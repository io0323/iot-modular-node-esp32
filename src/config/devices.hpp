#pragma once

#include "domain/device.hpp"

namespace node::config {

/// @brief 構成表の機器の一覧（profiles/mcu-cpp-01.yaml と同じ5チャネル・4アクチュエーター）。
/// インスタンスは静的に確保してあり、一覧は参照だけを持つ。
domain::DeviceRegistry devices();

}  // namespace node::config
