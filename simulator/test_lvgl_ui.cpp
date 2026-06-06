#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <lvgl.h>

#include "app/app_controller.h"
#include "app/mock_data.h"
#include "app/navigation.h"
#include "ui/app_ui.h"

namespace {

constexpr int taskbar_y = meshcore::screen_height - meshcore::bottom_bar_height;

bool ok = true;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << '\n';
        ok = false;
    }
}

void flush_cb(lv_display_t* display, const lv_area_t*, uint8_t*) {
    lv_display_flush_ready(display);
}

void pump(uint32_t start_ms = 0, int steps = 12) {
    for (int i = 0; i < steps; ++i) {
        const uint32_t now = start_ms + static_cast<uint32_t>(i * 5);
        lv_tick_inc(5);
        meshcore::ui_tick(now);
        lv_timer_handler();
    }
    lv_obj_update_layout(lv_screen_active());
}

meshcore::AppSnapshot stressful_snapshot() {
    auto snapshot = meshcore::make_mock_snapshot();
    snapshot.state.current_epoch_seconds = 12U * 60U * 60U;
    snapshot.state.firmware_version = "host-lvgl-test";
    snapshot.state.last_event = "alpha-7: ETA 5 minutes. This overlay text is intentionally long.";
    snapshot.state.compose_text = "This is a long compose draft used to force wrapping and clipping in the chat composer.";
    snapshot.state.compose_recipient = "broadcast";

    snapshot.messages.clear();
    for (int i = 0; i < 24; ++i) {
        meshcore::MeshMessage msg;
        msg.id = "m" + std::to_string(i);
        msg.sender = i % 2 == 0 ? "alpha-7" : "T-Deck Plus";
        msg.subject = i % 3 == 0 ? "Channel test" : "dm";
        msg.body = "Message " + std::to_string(i) +
                   " with a deliberately long body that should remain clipped or scrollable instead of overlapping the taskbar or toolbar.";
        msg.timestamp = 1700000000U + static_cast<unsigned>(i * 60);
        msg.outgoing = i % 2 != 0;
        msg.delivered = true;
        msg.acked = i % 2 == 0;
        msg.persisted = true;
        msg.status = msg.outgoing ? "sent" : "received";
        snapshot.messages.push_back(msg);
    }

    snapshot.nodes.clear();
    for (int i = 0; i < 18; ++i) {
        meshcore::NodeInfo node;
        node.name = "Node-" + std::to_string(i) + " very long display name";
        node.short_id = "0x" + std::to_string(0xA700 + i);
        node.rssi = -40 - i;
        node.snr = 8.5f - static_cast<float>(i) * 0.3f;
        node.last_seen_seconds = static_cast<unsigned>(i * 37);
        node.has_position = i % 3 == 0;
        node.latitude = 37.77 + static_cast<double>(i) * 0.001;
        node.longitude = -122.41 - static_cast<double>(i) * 0.001;
        node.public_key[0] = static_cast<unsigned char>(0x10 + i);
        snapshot.nodes.push_back(node);
    }
    snapshot.state.node_count = static_cast<int>(snapshot.nodes.size());

    snapshot.channels.clear();
    for (int i = 0; i < 16; ++i) {
        meshcore::ChannelInfo channel;
        channel.name = i == 0 ? "public" : (i == 1 ? "test" : "team-channel-" + std::to_string(i));
        channel.active = i == 1;
        channel.users = 1 + i;
        channel.secret[0] = static_cast<unsigned char>(0x80 + i);
        snapshot.channels.push_back(channel);
    }
    return snapshot;
}

lv_obj_t* find_clickable_at(lv_obj_t* obj, int x, int y) {
    if (obj == nullptr || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return nullptr;
    }
    const uint32_t count = lv_obj_get_child_count(obj);
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
        if (lv_obj_t* child = find_clickable_at(lv_obj_get_child(obj, i), x, y)) {
            return child;
        }
    }
    lv_area_t coords{};
    lv_obj_get_coords(obj, &coords);
    if (x >= coords.x1 && x <= coords.x2 && y >= coords.y1 && y <= coords.y2 &&
        lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) {
        return obj;
    }
    return nullptr;
}

void check_clickable_layout(lv_obj_t* obj, meshcore::ScreenId screen, int& clickable_count) {
    if (obj == nullptr || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_area_t coords{};
    lv_obj_get_coords(obj, &coords);
    const bool is_root = obj == lv_screen_active();
    const bool clickable = lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    if (!is_root && clickable) {
        ++clickable_count;
        const std::string prefix = std::string(meshcore::screen_title(screen)) + " clickable object ";
        expect(coords.x1 >= 0 && coords.y1 >= 0 && coords.x2 < meshcore::screen_width && coords.y2 < meshcore::screen_height,
               prefix + "is outside the 320x240 screen");
        expect(!(coords.y1 < taskbar_y && coords.y2 > taskbar_y),
               prefix + "crosses into the bottom taskbar at (" +
                   std::to_string(coords.x1) + "," + std::to_string(coords.y1) + ")-(" +
                   std::to_string(coords.x2) + "," + std::to_string(coords.y2) + ")");
    }
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i) {
        check_clickable_layout(lv_obj_get_child(obj, i), screen, clickable_count);
    }
}

void assert_screen_layout(meshcore::ScreenId screen) {
    meshcore::app_set_active_screen(screen);
    meshcore::ui_show(screen);
    pump(1000U + static_cast<uint32_t>(static_cast<int>(screen)) * 100U);
    expect(meshcore::ui_current_screen() == screen,
           std::string("ui_show did not select ") + meshcore::screen_title(screen));

    int clickable_count = 0;
    check_clickable_layout(lv_screen_active(), screen, clickable_count);
    expect(clickable_count > 0,
           std::string(meshcore::screen_title(screen)) + " has no clickable controls");

    if (screen != meshcore::ScreenId::Home && screen != meshcore::ScreenId::Boot) {
        lv_obj_t* close = find_clickable_at(lv_screen_active(), 307, 12);
        expect(close != nullptr, std::string(meshcore::screen_title(screen)) + " has no close button at the titlebar X");
        if (close != nullptr) {
            lv_obj_send_event(close, LV_EVENT_CLICKED, nullptr);
            pump(2000U + static_cast<uint32_t>(static_cast<int>(screen)) * 100U);
            expect(meshcore::ui_current_screen() == meshcore::ScreenId::Home,
                   std::string(meshcore::screen_title(screen)) + " close button did not return to Home");
        }
    }
}

struct DesktopTarget {
    int x;
    int y;
    meshcore::ScreenId target;
};

void assert_desktop_icons() {
    constexpr DesktopTarget targets[] = {
        {37, 37, meshcore::ScreenId::Inbox},
        {99, 37, meshcore::ScreenId::Contacts},
        {161, 37, meshcore::ScreenId::ChannelEditor},
        {223, 37, meshcore::ScreenId::Radio},
        {285, 37, meshcore::ScreenId::Map},
        {37, 109, meshcore::ScreenId::Nodes},
        {99, 109, meshcore::ScreenId::RadioAdvanced},
        {161, 109, meshcore::ScreenId::Identity},
        {223, 109, meshcore::ScreenId::Ble},
        {285, 109, meshcore::ScreenId::Settings},
        {37, 181, meshcore::ScreenId::Servers},
        {99, 181, meshcore::ScreenId::Tools},
        {161, 181, meshcore::ScreenId::Diagnostics},
    };

    for (const auto& target : targets) {
        meshcore::ui_show(meshcore::ScreenId::Home);
        pump(3000U + static_cast<uint32_t>(static_cast<int>(target.target)) * 50U);
        lv_obj_t* icon = find_clickable_at(lv_screen_active(), target.x, target.y);
        expect(icon != nullptr, std::string("desktop icon missing for ") + meshcore::screen_title(target.target));
        if (icon == nullptr) {
            continue;
        }
        lv_obj_send_event(icon, LV_EVENT_CLICKED, nullptr);
        pump(4000U + static_cast<uint32_t>(static_cast<int>(target.target)) * 50U);
        expect(meshcore::ui_current_screen() == target.target,
               std::string("desktop icon did not open ") + meshcore::screen_title(target.target));
    }
}

}  // namespace

int main() {
    lv_init();
    static std::vector<lv_color_t> draw_buffer(meshcore::screen_width * 32);
    lv_display_t* display = lv_display_create(meshcore::screen_width, meshcore::screen_height);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_buffers(display,
                           draw_buffer.data(),
                           nullptr,
                           static_cast<uint32_t>(draw_buffer.size() * sizeof(lv_color_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const auto snapshot = stressful_snapshot();
    meshcore::app_set_snapshot(snapshot);
    meshcore::ui_create(snapshot);
    pump();

    assert_desktop_icons();
    for (const auto screen : meshcore::all_screens()) {
        if (screen == meshcore::ScreenId::Boot) {
            continue;
        }
        assert_screen_layout(screen);
    }

    if (!ok) {
        return 1;
    }
    std::cout << "LVGL host UI regression test passed for "
              << meshcore::all_screens().size() - 1 << " screens\n";
    return 0;
}
