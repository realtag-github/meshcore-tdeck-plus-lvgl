#pragma once

#include <cstdint>

#include "app/app_types.h"

class SimService {
public:
    void begin();
    void loop(uint32_t now_ms);

private:
    meshcore::AppSnapshot snapshot_;
    uint32_t last_rx_ms_ = 0;
    uint32_t last_status_ms_ = 0;
    unsigned next_message_id_ = 900;

    void injectMessage();
};
