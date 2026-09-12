// ============================================================================
//  EventBus.cpp
// ============================================================================
#include "core/Event.hpp"
#include "core/Logger.hpp"

namespace core {

namespace {
    constexpr const char* kTag = "EventBus";
}

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

bool EventBus::subscribe(QueueHandle_t queue) {
    if (!queue || subscriberCount_ >= kMaxSubscribers) return false;
    subscribers_[subscriberCount_++] = queue;
    return true;
}

bool EventBus::unsubscribe(QueueHandle_t queue) {
    for (size_t i = 0; i < subscriberCount_; ++i) {
        if (subscribers_[i] == queue) {
            subscribers_[i] = subscribers_[--subscriberCount_];
            return true;
        }
    }
    return false;
}

size_t EventBus::publish(const Event& e) {
    size_t delivered = 0;
    for (size_t i = 0; i < subscriberCount_; ++i) {
        if (xQueueSend(subscribers_[i], &e, 0) == pdTRUE) {
            ++delivered;
        } else if (e.type == EventType::Battery) {
            // Only the most diagnostic-worthy overflow is worth logging;
            // everything else is dropped silently by design.
            Logger::throttled(LogLevel::Warn, kTag, 5000, "queue %u full, event dropped",
                              static_cast<unsigned>(i));
        }
    }
    return delivered;
}

}  // namespace core
