#include "tdeck_display.h"

#include <Arduino.h>
#include <lvgl.h>

#include "app/app_types.h"
#include "board_pins.h"

#ifndef APP_ENABLE_TDECK_DISPLAY
#define APP_ENABLE_TDECK_DISPLAY 0
#endif

#ifndef APP_TOUCH_DIAGNOSTICS
#define APP_TOUCH_DIAGNOSTICS 0
#endif

#if APP_ENABLE_TDECK_DISPLAY
#include <LovyanGFX.hpp>
#include <Wire.h>
#endif

#if APP_ENABLE_TDECK_DISPLAY
namespace {

constexpr uint8_t gt911_addr_low = 0x5d;
constexpr uint8_t gt911_addr_high = 0x14;
constexpr uint16_t gt911_status_reg = 0x814e;
constexpr uint16_t gt911_point1_reg = 0x8150;

class TDeckPanel : public lgfx::LGFX_Device {
  public:
    TDeckPanel() {
        {
            auto cfg = bus_.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = PIN_DISPLAY_SCLK;
            cfg.pin_mosi = PIN_DISPLAY_MOSI;
            cfg.pin_miso = PIN_DISPLAY_MISO;
            cfg.pin_dc = PIN_DISPLAY_DC;
            bus_.config(cfg);
            panel_.setBus(&bus_);
        }

        {
            auto cfg = panel_.config();
            cfg.pin_cs = PIN_DISPLAY_CS;
            cfg.pin_rst = PIN_DISPLAY_RST;
            cfg.pin_busy = -1;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 9;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            panel_.config(cfg);
        }

#if PIN_DISPLAY_BL >= 0
        {
            auto cfg = light_.config();
            cfg.pin_bl = PIN_DISPLAY_BL;
            cfg.invert = false;
            light_.config(cfg);
            panel_.setLight(&light_);
        }
#endif

        setPanel(&panel_);
    }

  private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

TDeckPanel tft;
lv_display_t* display = nullptr;
lv_indev_t* touch_indev = nullptr;
uint8_t gt911_addr = 0;
lv_point_t last_touch = {0, 0};
uint32_t last_touch_log_ms = 0;
uint32_t last_touch_idle_log_ms = 0;
uint32_t touch_read_count = 0;

alignas(4) static lv_color_t draw_buffer[meshcore::screen_width * 40];

bool i2c_write_reg(uint8_t addr, uint16_t reg, const uint8_t* data, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xff));
    for (uint8_t i = 0; i < len; ++i) {
        Wire.write(data[i]);
    }
    return Wire.endTransmission() == 0;
}

bool i2c_read_reg(uint8_t addr, uint16_t reg, uint8_t* data, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xff));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    const uint8_t read = Wire.requestFrom(addr, len);
    if (read != len) {
        return false;
    }
    for (uint8_t i = 0; i < len; ++i) {
        data[i] = Wire.read();
    }
    return true;
}

uint8_t detect_gt911() {
    uint8_t value = 0;
    if (i2c_read_reg(gt911_addr_low, gt911_status_reg, &value, 1)) {
        return gt911_addr_low;
    }
    if (i2c_read_reg(gt911_addr_high, gt911_status_reg, &value, 1)) {
        return gt911_addr_high;
    }
    return 0;
}

void flush_display(lv_display_t* disp, const lv_area_t* area, uint8_t* pixels) {
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;
    tft.pushImage(area->x1, area->y1, width, height, reinterpret_cast<uint16_t*>(pixels));
    lv_display_flush_ready(disp);
}

bool read_touch_point(lv_point_t* point) {
    ++touch_read_count;
    const uint32_t now = millis();
    if (gt911_addr == 0) {
        return false;
    }
    uint8_t status = 0;
    if (!i2c_read_reg(gt911_addr, gt911_status_reg, &status, 1)) {
#if APP_TOUCH_DIAGNOSTICS
        if (now - last_touch_idle_log_ms > 2000U) {
            last_touch_idle_log_ms = now;
            Serial.printf("touch poll=%lu i2c-read-failed addr=0x%02x\n",
                          static_cast<unsigned long>(touch_read_count),
                          gt911_addr);
        }
#endif
        return false;
    }
    if ((status & 0x80) == 0 || (status & 0x0f) == 0) {
        if ((status & 0x80) != 0) {
            const uint8_t clear = 0;
            i2c_write_reg(gt911_addr, gt911_status_reg, &clear, 1);
        }
#if APP_TOUCH_DIAGNOSTICS
        if (now - last_touch_idle_log_ms > 2000U) {
            last_touch_idle_log_ms = now;
#if PIN_TOUCH_INT >= 0
            const int int_level = digitalRead(PIN_TOUCH_INT);
#else
            const int int_level = -1;
#endif
            Serial.printf("touch poll=%lu idle status=0x%02x int=%d\n",
                          static_cast<unsigned long>(touch_read_count),
                          status,
                          int_level);
        }
#endif
        return false;
    }

    uint8_t data[8] = {};
    const bool ok = i2c_read_reg(gt911_addr, gt911_point1_reg, data, sizeof(data));
    const uint8_t clear = 0;
    i2c_write_reg(gt911_addr, gt911_status_reg, &clear, 1);
    if (!ok) {
        return false;
    }

    const int raw_x = static_cast<int>(data[1] << 8 | data[0]);
    const int raw_y = static_cast<int>(data[3] << 8 | data[2]);
    int x = raw_y;
    int y = meshcore::screen_height - raw_x;
    if (x < 0) {
        x = 0;
    }
    if (x >= meshcore::screen_width) {
        x = meshcore::screen_width - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y >= meshcore::screen_height) {
        y = meshcore::screen_height - 1;
    }
    point->x = static_cast<lv_coord_t>(x);
    point->y = static_cast<lv_coord_t>(y);
#if APP_TOUCH_DIAGNOSTICS
    if (now - last_touch_log_ms > 250U) {
        last_touch_log_ms = now;
        Serial.printf("touch raw=%d,%d mapped=%d,%d status=0x%02x\n", raw_x, raw_y, x, y, status);
    }
#endif
    return true;
}

void read_touch(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    lv_point_t point;
    if (read_touch_point(&point)) {
        last_touch = point;
        data->point = point;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }
    data->point = last_touch;
    data->state = LV_INDEV_STATE_RELEASED;
}

void select_spi_devices() {
#if PIN_SDCARD_CS >= 0
    pinMode(PIN_SDCARD_CS, OUTPUT);
    digitalWrite(PIN_SDCARD_CS, HIGH);
#endif
#if PIN_LORA_CS >= 0
    pinMode(PIN_LORA_CS, OUTPUT);
    digitalWrite(PIN_LORA_CS, HIGH);
#endif
#if PIN_DISPLAY_CS >= 0
    pinMode(PIN_DISPLAY_CS, OUTPUT);
    digitalWrite(PIN_DISPLAY_CS, HIGH);
#endif
}

}  // namespace
#endif

bool tdeck_display_begin() {
    Serial.printf("boot: tdeck display driver stableui-blemtu-20260527-1 enabled=%d\n", APP_ENABLE_TDECK_DISPLAY);
#if APP_ENABLE_TDECK_DISPLAY
    select_spi_devices();
    Serial.println("boot: tft init begin");
    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);
    tft.fillScreen(0x0000);
    Serial.println("boot: tft init done");

    display = lv_display_create(meshcore::screen_width, meshcore::screen_height);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_buffers(display, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    Serial.println("boot: lv display registered");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setTimeOut(20);
#if PIN_TOUCH_INT >= 0
    pinMode(PIN_TOUCH_INT, INPUT);
#endif
    Serial.println("boot: GT911 detect begin");
    gt911_addr = detect_gt911();
    if (gt911_addr != 0) {
        Serial.printf("GT911 touch controller detected addr=0x%02x\n", gt911_addr);
        touch_indev = lv_indev_create();
        lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(touch_indev, display);
        lv_indev_set_read_cb(touch_indev, read_touch);
    } else {
        Serial.println("GT911 touch controller not detected");
    }
    return display != nullptr;
#else
    return true;
#endif
}
