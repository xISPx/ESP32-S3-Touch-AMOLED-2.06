// ============================================================================
//  TaskBase.hpp — RAII wrapper around a FreeRTOS task with its own queue.
//
//  Each derived task:
//    * owns one input Queue<Event> (the task's mailbox),
//    * implements run() which must be non-blocking (vTaskDelay / xQueueReceive
//      with timeout only — never busy-wait),
//    * is started pinned to a specific core with an explicit priority.
// ============================================================================
#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core/Event.hpp"

namespace core {

class TaskBase {
public:
    TaskBase(const TaskBase&) = delete;
    TaskBase& operator=(const TaskBase&) = delete;

    // stackWords — stack depth in BYTES (FreeRTOS uxTaskCreate takes words,
    // the wrapper converts); prio — 0..configMAX_PRIORITIES-1; core — pin.
    bool start(const char* name, uint32_t stackBytes, UBaseType_t prio,
               BaseType_t core, UBaseType_t queueDepth);

    // Same, but the task stack lives in PSRAM (CONFIG_FREERTOS_TASK_CREATE_
    // ALLOW_EXT_MEM=1 in the Arduino core).  Use for TLS-heavy workers to
    // keep internal SRAM free — the TLS handshake needs a large contiguous
    // internal block.
    bool startExternal(const char* name, uint32_t stackBytes, UBaseType_t prio,
                       BaseType_t core, UBaseType_t queueDepth);

    // Virtual so a stopped task is joined/deleted deterministically.
    virtual ~TaskBase();

    QueueHandle_t mailbox() const { return queue_; }
    TaskHandle_t handle() const { return handle_; }

protected:
    TaskBase() = default;

    virtual void run() = 0;

    // Convenience: block for the next mailbox event (with timeout in ms).
    bool waitFor(Event& out, uint32_t timeoutMs);

private:
    static void trampoline(void* arg);

    TaskHandle_t handle_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    void* ext_stack_ = nullptr;      // PSRAM stack buffer (startExternal)
    StaticTask_t* ext_tcb_ = nullptr;
};

}  // namespace core
