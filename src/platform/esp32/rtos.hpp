// FreeRTOS のキュー・ミューテックスを使う小さな関数（ESP-IDF v5.5.6 の FreeRTOS-Kernel）。
// xQueueSend などの API はマクロで、展開すると古い形のキャスト（( BaseType_t ) 0 など）になり、自分のコンポーネントの
// -Wold-style-cast に当たる。マクロが呼ぶ公開の関数を、マクロと同じ引数で直接呼ぶ（queue.h・semphr.h の定義のとおり）。
#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

namespace node::platform::esp32::rtos {

inline constexpr BaseType_t kSendToBack = 0;  ///< queueSEND_TO_BACK（queue.h）
inline constexpr BaseType_t kOverwrite = 2;   ///< queueOVERWRITE（queue.h）
inline constexpr std::uint8_t kTypeBase = 0;  ///< queueQUEUE_TYPE_BASE（queue.h）
inline constexpr std::uint8_t kTypeMutex = 1; ///< queueQUEUE_TYPE_MUTEX（queue.h）
inline constexpr BaseType_t kTrue = 1;        ///< pdTRUE（projdefs.h）

/// @brief xQueueCreateStatic と同じ。
/// @param length 件数
/// @param item_size 1件の大きさ
/// @param storage 中身の置き場所（length × item_size バイト）
/// @param buffer キューの管理の置き場所
/// @return キュー
inline QueueHandle_t queue_create_static(UBaseType_t length, UBaseType_t item_size, std::uint8_t* storage,
                                         StaticQueue_t* buffer) {
  return xQueueGenericCreateStatic(length, item_size, storage, buffer, kTypeBase);
}

/// @brief xQueueSend と同じ（後ろに足す）。
/// @param q キュー
/// @param item 写す中身
/// @param ticks 待つ tick
/// @return 足せたら true
inline bool queue_send(QueueHandle_t q, const void* item, TickType_t ticks) {
  return xQueueGenericSend(q, item, ticks, kSendToBack) == kTrue;
}

/// @brief xQueueOverwrite と同じ（1件のキューの中身を置き換える）。
/// @param q キュー（長さ1）
/// @param item 写す中身
inline void queue_overwrite(QueueHandle_t q, const void* item) { (void)xQueueGenericSend(q, item, 0, kOverwrite); }

/// @brief xQueueReceive と同じ。
/// @param q キュー
/// @param out 書き込み先
/// @param ticks 待つ tick
/// @return 取り出せたら true
inline bool queue_receive(QueueHandle_t q, void* out, TickType_t ticks) { return xQueueReceive(q, out, ticks) == kTrue; }

/// @brief xSemaphoreCreateMutexStatic と同じ。
/// @param buffer ミューテックスの置き場所
/// @return ミューテックス
inline SemaphoreHandle_t mutex_create_static(StaticSemaphore_t* buffer) {
  return xQueueCreateMutexStatic(kTypeMutex, buffer);
}

/// @brief xSemaphoreTake と同じ。
/// @param m ミューテックス
/// @param ticks 待つ tick
/// @return 取れたら true
inline bool mutex_take(SemaphoreHandle_t m, TickType_t ticks) { return xQueueSemaphoreTake(m, ticks) == kTrue; }

/// @brief xSemaphoreGive と同じ。
/// @param m ミューテックス
inline void mutex_give(SemaphoreHandle_t m) { (void)xQueueGenericSend(m, nullptr, 0, kSendToBack); }

}  // namespace node::platform::esp32::rtos
