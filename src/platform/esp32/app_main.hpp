// ターゲット版の入口（firmware/main/main.cpp の app_main から呼ぶ）。
#pragma once

namespace node::platform::esp32 {

/// @brief 機器の初期化・Wi-Fi の起動・Node の起動・タスクの起動（P6 の決定 #96〜#101）。戻らない。
void run();

}  // namespace node::platform::esp32
