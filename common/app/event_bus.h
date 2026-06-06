#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "app_types.h"

namespace meshcore {

enum class AppEventType {
    None,
    MessageReceived,
    MessageSent,
    NodeSeen,
    BatteryChanged,
    GpsChanged,
    RadioError,
    Navigate,
};

struct AppEvent {
    AppEventType type = AppEventType::None;
    std::string text;
    ScreenId screen = ScreenId::Home;
};

template <std::size_t Capacity = 16>
class EventBus {
public:
    bool publish(const AppEvent& event) {
        const std::size_t next_tail = (tail_ + 1) % Capacity;
        if (next_tail == head_) {
            return false;
        }
        events_[tail_] = event;
        tail_ = next_tail;
        return true;
    }

    bool poll(AppEvent& event) {
        if (head_ == tail_) {
            return false;
        }
        event = events_[head_];
        head_ = (head_ + 1) % Capacity;
        return true;
    }

    bool empty() const {
        return head_ == tail_;
    }

private:
    std::array<AppEvent, Capacity> events_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};

}  // namespace meshcore
