#include "platform/esp32/esp32_tasks.hpp"

#include "app/node.hpp"
#include "drivers/sensor_io.hpp"
#include "drivers/ui_io.hpp"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "infra/json_writer.hpp"
#include "platform/esp32/esp32_hal.hpp"
#include "platform/esp32/esp32_mqtt.hpp"
#include "platform/esp32/rtos.hpp"

namespace node::platform::esp32 {

namespace {

// ---- キューの深さ（#113）
constexpr UBaseType_t kSensorQueueDepth = 8;
constexpr UBaseType_t kUiQueueDepth = 8;

// ---- タスク（#96・#97）。スタックの大きさはバイト（ESP-IDF の FreeRTOS は StackType_t が1バイト）
constexpr std::uint32_t kControlStack = 8 * 1024;
constexpr std::uint32_t kSenseStack = 4 * 1024;
constexpr std::uint32_t kCommStack = 6 * 1024;
constexpr std::uint32_t kUiStack = 4 * 1024;
constexpr UBaseType_t kControlPriority = 6;
constexpr UBaseType_t kSensePriority = 5;
constexpr UBaseType_t kCommPriority = 4;
constexpr UBaseType_t kUiPriority = 3;
constexpr BaseType_t kCore0 = 0;
constexpr BaseType_t kCore1 = 1;
constexpr std::uint32_t kControlPeriodMs = 10;
constexpr std::uint32_t kUiPeriodMs = 20;
constexpr std::uint32_t kCommWaitMs = 20;
constexpr std::uint32_t kSenseWaitMs = 100;
constexpr std::uint64_t kStatsIntervalMs = 60000;  ///< スタックの残りとヒープのログ（#97・NFR-03）

std::uint64_t now_ms() { return static_cast<std::uint64_t>(esp_timer_get_time()) / 1000U; }

void log_stats(const TaskContext& ctx) {
  static constexpr std::array<const char*, 4> kNames{"control", "sense", "comm", "ui"};
  const std::array<TaskHandle_t, 4> handles{xTaskGetCurrentTaskHandle(), ctx.others[0], ctx.others[1], ctx.others[2]};
  std::array<char, 256> buf{};
  infra::JsonWriter w(buf);
  w.begin_object();
  w.key("stack_free_min");
  w.begin_object();
  for (std::size_t i = 0; i < handles.size(); ++i) {
    w.key(kNames[i]).uinteger(handles[i] != nullptr ? uxTaskGetStackHighWaterMark(handles[i]) : 0U);
  }
  w.end_object();
  w.key("heap_free").uinteger(esp_get_free_heap_size());
  w.key("heap_min").uinteger(esp_get_minimum_free_heap_size());
  w.end_object();
  if (w.ok()) {
    ctx.log->write("stats", w.view());
  }
}

void control_task(void* arg) {
  auto& ctx = *static_cast<TaskContext*>(arg);
  (void)esp_task_wdt_add(nullptr);
  TickType_t wake = xTaskGetTickCount();
  std::uint64_t next_stats = now_ms();  // 起動の直後に1回、その後は1分ごと（#97・#114）
  for (;;) {
    (void)xTaskDelayUntil(&wake, ticks_of(kControlPeriodMs));
    (void)esp_task_wdt_reset();
    ctx.node->tick();
    if (now_ms() >= next_stats) {
      next_stats += kStatsIntervalMs;
      log_stats(ctx);
    }
  }
}

void sense_task(void* arg) {
  auto& ctx = *static_cast<TaskContext*>(arg);
  (void)esp_task_wdt_add(nullptr);
  for (;;) {
    (void)esp_task_wdt_reset();
    domain::SensorRequest req{};
    if (rtos::queue_receive(ctx.queues.sensor_requests, &req, ticks_of(kSenseWaitMs))) {
      const domain::SensorReport report = ctx.sensors->execute(req);
      (void)rtos::queue_send(ctx.queues.sensor_reports, &report, ticks_of(kSenseWaitMs));
    }
  }
}

void comm_task(void* arg) {
  auto& ctx = *static_cast<TaskContext*>(arg);
  (void)esp_task_wdt_add(nullptr);
  for (;;) {
    (void)esp_task_wdt_reset();
    ctx.comm->step(ticks_of(kCommWaitMs));
  }
}

void ui_task(void* arg) {
  auto& ctx = *static_cast<TaskContext*>(arg);
  (void)esp_task_wdt_add(nullptr);
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    (void)xTaskDelayUntil(&wake, ticks_of(kUiPeriodMs));
    (void)esp_task_wdt_reset();
    for (domain::UiInput in{}; ctx.ui->poll_input(now_ms(), in);) {
      (void)rtos::queue_send(ctx.queues.ui_inputs, &in, 0);
    }
    domain::DisplayFrame frame{};
    if (rtos::queue_receive(ctx.queues.display_frames, &frame, 0)) {
      ctx.ui->show(frame);
    }
  }
}

}  // namespace

IoQueues create_io_queues() {
  static std::array<std::uint8_t, kSensorQueueDepth * sizeof(domain::SensorRequest)> req_buf{};
  static std::array<std::uint8_t, kSensorQueueDepth * sizeof(domain::SensorReport)> rep_buf{};
  static std::array<std::uint8_t, kUiQueueDepth * sizeof(domain::UiInput)> in_buf{};
  static std::array<std::uint8_t, sizeof(domain::DisplayFrame)> frame_buf{};
  static StaticQueue_t req_q{};
  static StaticQueue_t rep_q{};
  static StaticQueue_t in_q{};
  static StaticQueue_t frame_q{};
  IoQueues q{};
  q.sensor_requests = rtos::queue_create_static(kSensorQueueDepth, sizeof(domain::SensorRequest), req_buf.data(), &req_q);
  q.sensor_reports = rtos::queue_create_static(kSensorQueueDepth, sizeof(domain::SensorReport), rep_buf.data(), &rep_q);
  q.ui_inputs = rtos::queue_create_static(kUiQueueDepth, sizeof(domain::UiInput), in_buf.data(), &in_q);
  q.display_frames = rtos::queue_create_static(1, sizeof(domain::DisplayFrame), frame_buf.data(), &frame_q);
  return q;
}

bool QueueSensorIo::submit(const domain::SensorRequest& request) {
  if (request.op == domain::SensorOp::FaultDue) {
    // ハードに触らないので sense に送らない
    domain::SensorReport r{};
    r.index = request.index;
    r.op = request.op;
    r.at_ms = request.at_ms;
    return local_.push_back(r);
  }
  return rtos::queue_send(q_.sensor_requests, &request, 0);
}

bool QueueSensorIo::poll_report(domain::SensorReport& out) {
  if (!local_.empty()) {
    out = local_.front();
    local_.erase_at(0);
    return true;
  }
  return rtos::queue_receive(q_.sensor_reports, &out, 0);
}

bool QueueUiIo::poll_input(std::uint64_t /*now_ms*/, domain::UiInput& out) {
  return rtos::queue_receive(q_.ui_inputs, &out, 0);
}

void QueueUiIo::show(const domain::DisplayFrame& frame) { rtos::queue_overwrite(q_.display_frames, &frame); }

void start_tasks(TaskContext& ctx) {
  static std::array<StackType_t, kControlStack> control_stack{};
  static std::array<StackType_t, kSenseStack> sense_stack{};
  static std::array<StackType_t, kCommStack> comm_stack{};
  static std::array<StackType_t, kUiStack> ui_stack{};
  static std::array<StaticTask_t, 4> tcbs{};
  // control を最後に作る（ほかのタスクのハンドルを書き終えてから。control は ctx を読むだけ）
  ctx.others[0] = xTaskCreateStaticPinnedToCore(sense_task, "sense", kSenseStack, &ctx, kSensePriority,
                                                sense_stack.data(), &tcbs[1], kCore1);
  ctx.others[1] = xTaskCreateStaticPinnedToCore(comm_task, "comm", kCommStack, &ctx, kCommPriority, comm_stack.data(),
                                                &tcbs[2], kCore0);
  ctx.others[2] = xTaskCreateStaticPinnedToCore(ui_task, "ui", kUiStack, &ctx, kUiPriority, ui_stack.data(), &tcbs[3],
                                                kCore1);
  (void)xTaskCreateStaticPinnedToCore(control_task, "control", kControlStack, &ctx, kControlPriority,
                                      control_stack.data(), &tcbs[0], kCore1);
}

}  // namespace node::platform::esp32
