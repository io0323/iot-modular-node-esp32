// FreeRTOS のタスクとタスク間の受け渡し（P6 の決定 #96〜#98）。
//
// | タスク  | 優先度 | コア | 周期             | 呼ぶもの |
// |---------|--------|------|------------------|----------|
// | control | 6      | 1    | 10ms             | Node::tick（app のクラスはここからだけ）、スタック・ヒープのログ |
// | sense   | 5      | 1    | 依頼を待つ       | drivers::SensorIo（センサーの read・init・回復） |
// | comm    | 4      | 0    | 依頼を待つ（20ms）| EspMqttComm（esp-mqtt の API） |
// | ui      | 3      | 1    | 20ms             | drivers::UiIo（ボタンの poll・OLED） |
//
// control と sense・ui のあいだは、control の側の代わり（QueueSensorIo・QueueUiIo）がキューで受け渡す。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "domain/io.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "support/static_vector.hpp"

namespace node::app {
class Node;
}
namespace node::drivers {
class SensorIo;
class UiIo;
}  // namespace node::drivers
namespace node::infra {
class ILogSink;
}

namespace node::platform::esp32 {

class EspMqttComm;

/// @brief control と sense・ui のあいだのキュー（静的に確保。app_main から1回だけ作る）。
struct IoQueues {
  QueueHandle_t sensor_requests{nullptr};  ///< control → sense：domain::SensorRequest
  QueueHandle_t sensor_reports{nullptr};   ///< sense → control：domain::SensorReport
  QueueHandle_t ui_inputs{nullptr};        ///< ui → control：domain::UiInput
  QueueHandle_t display_frames{nullptr};   ///< control → ui：domain::DisplayFrame（長さ1。最新で上書き）
};

/// @brief キューを作る。
/// @return キュー
IoQueues create_io_queues();

/// @brief control の側のセンサーの代わり。読む・再初期化は sense へ送り、故障中の周期はその場で返す。
class QueueSensorIo final : public domain::ISensorIo {
 public:
  /// @brief キューを受け取る。
  /// @param q キュー
  explicit QueueSensorIo(const IoQueues& q) : q_(q) {}
  bool submit(const domain::SensorRequest& request) override;
  bool poll_report(domain::SensorReport& out) override;

 private:
  IoQueues q_;
  support::StaticVector<domain::SensorReport, 8> local_{};
};

/// @brief control の側の UI の代わり。入力は ui から受け、表示の内容は ui へ送る。
class QueueUiIo final : public domain::IUiIo {
 public:
  /// @brief キューを受け取る。
  /// @param q キュー
  explicit QueueUiIo(const IoQueues& q) : q_(q) {}
  /// @brief 何もしない（UI の機器は app_main がタスクを起こす前に初期化する）。
  /// @return いつも成功
  support::Result<void> init() override { return support::ok(); }
  bool poll_input(std::uint64_t now_ms, domain::UiInput& out) override;
  void show(const domain::DisplayFrame& frame) override;

 private:
  IoQueues q_;
};

/// @brief タスクに渡すもの（どれも app_main が静的に作り、寿命はプログラム全体）。
struct TaskContext {
  app::Node* node{nullptr};           ///< control
  infra::ILogSink* log{nullptr};      ///< control（スタック・ヒープのログ）
  drivers::SensorIo* sensors{nullptr};  ///< sense
  drivers::UiIo* ui{nullptr};         ///< ui
  EspMqttComm* comm{nullptr};         ///< comm
  IoQueues queues{};                  ///< sense・ui のキュー
  /// sense・comm・ui のタスク（control より先に作り、control はスタックの残りのログにだけ使う）
  std::array<TaskHandle_t, 3> others{};
};

/// @brief 4つのタスクを起こす（スタックは静的に確保。#96・#97）。
/// @param ctx タスクに渡すもの（寿命はプログラム全体）
void start_tasks(TaskContext& ctx);

}  // namespace node::platform::esp32
