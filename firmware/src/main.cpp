#include <Arduino.h>
#include "app_config.h"
#include "ui/ui.h"
#include "mesh/mesh_service.h"
#include "drivers/wifi_ota_service.h"

static MeshService mesh;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println(APP_NAME " booting");
    Serial.println("build: blewake-20260527-1");

    mesh.begin();
    ui_begin();
    wifi_ota_begin();
    Serial.println("boot: setup complete");
}

void loop() {
    static uint32_t last_diag_ms = 0;
    const uint32_t now = millis();
    if (now < 30000U && (last_diag_ms == 0 || now - last_diag_ms >= 1000U)) {
        last_diag_ms = now;
        Serial.printf("diag: loop alive ms=%lu\n", static_cast<unsigned long>(now));
    }
    ui_loop();
    wifi_ota_loop();
    mesh.loop();
    delay(5);
}
