#pragma once

#include <cstdint>

#include "app/app_types.h"

namespace meshcore {

void ui_create(const AppSnapshot& snapshot);
ScreenId ui_current_screen();
void ui_show(ScreenId screen);
void ui_request_show(ScreenId screen);
void ui_tick();
void ui_tick(uint32_t now_ms);

}  // namespace meshcore
