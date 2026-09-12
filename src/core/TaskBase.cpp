// ============================================================================
//  TaskBase.cpp
// ============================================================================
#include "core/TaskBase.hpp"
#include "core/Logger.hpp"
#include <esp_heap_caps.h>

namespace core {

namespace {
    constexpr const char* kTag = "TaskBase";
}

bool TaskBase::start(const char* name, uint32_t stackBytes, UBaseType_t prio,
                     BaseType_t core, UBaseType_t queueDepth) {
    queue_ = xQueueCreate(queueDepth, sizeof(Event));
    if (!queue_) return false;

    // Stack in words (FreeRTOS API) — convert from bytes, 4 bytes per word.
    const UBaseType_t words = static_cast<UBaseType_t>(stackBytes / 4);
    if (xTaskCreatePinnedToCore(&TaskBase::trampoline, name, words, this,
                                prio, &handle_, core) != pdPASS) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        return false;
    }
    LOGI(kTag, "%s started (core=%d prio=%u stack=%uB)",
         name, static_cast<int>(core), static_cast<unsigned>(prio),
         static_cast<unsigned>(stackBytes));
    return true;
}

bool TaskBase::startExternal(const char* name, uint32_t stackBytes,
                             UBaseType_t prio, BaseType_t core,
                             UBaseType_t queueDepth) {
    queue_ = xQueueCreate(queueDepth, sizeof(Event));
    if (!queue_) return false;

    ext_stack_ = heap_caps_malloc(stackBytes, MALLOC_CAP_SPIRAM);
    ext_tcb_ = static_cast<StaticTask_t*>(heap_caps_malloc(
        sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!ext_stack_ || !ext_tcb_) {
        if (ext_stack_) heap_caps_free(ext_stack_);
        if (ext_tcb_) heap_caps_free(ext_tcb_);
        ext_stack_ = nullptr;
        ext_tcb_ = nullptr;
        vQueueDelete(queue_);
        queue_ = nullptr;
        return false;
    }
    handle_ = xTaskCreateStaticPinnedToCore(
        &TaskBase::trampoline, name, stackBytes / sizeof(StackType_t), this,
        prio, static_cast<StackType_t*>(ext_stack_), ext_tcb_, core);
    if (!handle_) {
        heap_caps_free(ext_stack_);
        heap_caps_free(ext_tcb_);
        ext_stack_ = nullptr;
        ext_tcb_ = nullptr;
        vQueueDelete(queue_);
        queue_ = nullptr;
        return false;
    }
    LOGI(kTag, "%s started (core=%d prio=%u stack=%uB in PSRAM)",
         name, static_cast<int>(core), static_cast<unsigned>(prio),
         static_cast<unsigned>(stackBytes));
    return true;
}

TaskBase::~TaskBase() {
    if (handle_) {
        vTaskDelete(handle_);
        handle_ = nullptr;
    }
    if (queue_) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
}

void TaskBase::trampoline(void* arg) {
    static_cast<TaskBase*>(arg)->run();
}

bool TaskBase::waitFor(Event& out, uint32_t timeoutMs) {
    return queue_ &&
           xQueueReceive(queue_, &out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

}  // namespace core
