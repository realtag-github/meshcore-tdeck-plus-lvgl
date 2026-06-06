#include <Arduino.h>
#include <lvgl.h>
#include "app_config.h"
#include "ui.h"
#include "app/app_controller.h"
#include "tdeck_display.h"
#include "ui/app_ui.h"

#include "../../../common/app/app_controller.cpp"
#include "../../../common/ui/app_ui.cpp"

static uint32_t last_tick_ms = 0;
static unsigned last_rendered_version = 0;

void ui_begin() {
    Serial.println("boot: ui begin");
    lv_init();
    Serial.println("boot: display begin");
    tdeck_display_begin();
    Serial.println("boot: display ready");

    meshcore::ui_create(meshcore::app_snapshot());
    Serial.println("boot: ui desktop ready");
    last_rendered_version = meshcore::app_snapshot_version();
    last_tick_ms = millis();
}

void ui_loop() {
    uint32_t now = millis();
    lv_tick_inc(now - last_tick_ms);
    last_tick_ms = now;
    lv_timer_handler();
    meshcore::ui_tick(now);
    const unsigned version = meshcore::app_snapshot_version();
    if (version != last_rendered_version) {
        last_rendered_version = version;
    }
}
