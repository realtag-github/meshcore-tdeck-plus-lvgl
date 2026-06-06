#include "app_ui.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <lvgl.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#endif

#include "app/app_controller.h"
#include "app/navigation.h"

namespace meshcore {

void ui_show(ScreenId screen);
void ui_tick();

namespace {

AppSnapshot data;
ScreenId current_screen = ScreenId::Boot;

void trace_ui(const char* message) {
#if defined(ARDUINO_ARCH_ESP32)
    Serial.println(message);
#else
    if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
        std::fprintf(stderr, "%s\n", message);
    }
#endif
}

void trace_ui_target(const char* prefix, ScreenId target) {
#if defined(ARDUINO_ARCH_ESP32)
    Serial.printf("%s %s\n", prefix, screen_title(target));
#else
    if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
        std::fprintf(stderr, "%s %s\n", prefix, screen_title(target));
    }
#endif
}

struct ButtonBinding {
    ButtonBinding() = default;
    ButtonBinding(ScreenId source_value, int action_index_value)
        : source(source_value), action_index(action_index_value) {}

    ScreenId source = ScreenId::Home;
    int action_index = 0;
};

struct DesktopIconBinding {
    DesktopIconBinding() = default;
    DesktopIconBinding(ScreenId target_value, ActionCommand command_value = ActionCommand::Navigate)
        : target(target_value), command(command_value) {}

    ScreenId target = ScreenId::Home;
    ActionCommand command = ActionCommand::Navigate;
};

struct PendingNavigation {
    bool active = false;
    bool run_action = false;
    bool start_app = false;
    ScreenId source = ScreenId::Home;
    int action_index = 0;
    ScreenId target = ScreenId::Home;
};

struct ChatTargetBinding {
    ChatTargetBinding() = default;
    ChatTargetBinding(bool channel_value, int index_value)
        : channel(channel_value), index(index_value) {}

    bool channel = false;
    int index = 0;
};

enum class DesktopIconKind {
    Inbox,
    Compose,
    Nodes,
    Contacts,
    Channels,
    ChannelEditor,
    Map,
    Settings,
    Radio,
    RadioAdvanced,
    RadioTuning,
    Identity,
    Ble,
    Servers,
    Tools,
    Diagnostics,
};

std::array<std::array<ButtonBinding, 4>, 19> button_bindings;
std::array<DesktopIconBinding, 16> desktop_icon_bindings;
std::array<ChatTargetBinding, 24> chat_target_bindings;
DesktopIconBinding taskbar_start_binding;

std::string text_buffer;
std::string detail_buffer;
std::vector<std::string> known_incoming_message_ids;
std::string notification_text;
uint32_t notification_started_ms = 0;
uint32_t ui_now_ms = 0;
lv_obj_t* notification_overlay = nullptr;
bool notification_visible = false;
lv_obj_t* active_window_toolbar = nullptr;
lv_obj_t* active_window_taskbar = nullptr;
PendingNavigation pending_navigation;
uint32_t desktop_click_blocked_until_ms = 0;

constexpr uint32_t ce_face = 0xc0c0c0;
constexpr uint32_t ce_light = 0xffffff;
constexpr uint32_t ce_shadow = 0x808080;
constexpr uint32_t ce_dark_shadow = 0x404040;
constexpr uint32_t ce_title = 0x000080;
constexpr uint32_t ce_title_alt = 0x1084d0;
constexpr uint32_t ce_title_text = 0xffffff;
constexpr uint32_t ce_text = 0x000000;
constexpr uint32_t ce_window = 0xffffff;
constexpr uint32_t ce_selected = 0x000080;
constexpr uint32_t ce_selected_text = 0xffffff;
constexpr int ce_taskbar_height = 28;
constexpr int ce_toolbar_height = 28;
constexpr int ce_taskbar_y = screen_height - ce_taskbar_height;
constexpr int ce_toolbar_y = top_bar_height;
constexpr int ce_window_content_y = top_bar_height + ce_toolbar_height;
constexpr int ce_window_content_height = ce_taskbar_y - ce_window_content_y;

std::string fmt_float(float value, int precision = 1) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string fmt_bytes(unsigned value) {
    std::ostringstream out;
    if (value >= 1024U * 1024U) {
        out << (value / (1024U * 1024U)) << " MB";
    } else if (value >= 1024U) {
        out << (value / 1024U) << " KB";
    } else {
        out << value << " B";
    }
    return out.str();
}

std::string bandwidth_text(unsigned bandwidth_hz) {
    if (bandwidth_hz % 1000U == 0) {
        return std::to_string(bandwidth_hz / 1000U) + " kHz";
    }
    return fmt_float(static_cast<float>(bandwidth_hz) / 1000.0f, 1) + " kHz";
}

const char* c_str(const std::string& value) {
    text_buffer = value;
    return text_buffer.c_str();
}

std::string clock_text(unsigned epoch_seconds) {
    const unsigned minutes = (epoch_seconds / 60U) % (24U * 60U);
    const unsigned hour_24 = minutes / 60U;
    const unsigned minute = minutes % 60U;
    unsigned hour_12 = hour_24 % 12U;
    if (hour_12 == 0) {
        hour_12 = 12;
    }
    char buffer[8] = {};
    std::snprintf(buffer, sizeof(buffer), "%u:%02u%s", hour_12, minute, hour_24 < 12U ? "AM" : "PM");
    return buffer;
}

std::string taskbar_battery_text() {
    return std::to_string(data.state.battery_percent) + "%";
}

std::string taskbar_time_text() {
    return clock_text(data.state.current_epoch_seconds);
}

void on_desktop_icon(lv_event_t* event);
lv_obj_t* make_start_button(lv_obj_t* parent, DesktopIconBinding* binding);
lv_obj_t* bottom(lv_obj_t* root);
lv_obj_t* toolbar(lv_obj_t* root);
void add_bevel(lv_obj_t* parent, int x, int y, int w, int h, bool raised);
lv_obj_t* make_label(lv_obj_t* parent, const std::string& text, int x, int y, int w, int font_size);
void draw_desktop_icon(lv_obj_t* parent, DesktopIconKind kind, DesktopIconBinding* binding);
void update_notification_from_snapshot();
void hide_notification_overlay();
bool process_pending_navigation();
void enable_vertical_scroll(lv_obj_t* obj);
void add_edit_overlay(lv_obj_t* root);

bool is_chat_screen(ScreenId screen) {
    return screen == ScreenId::Inbox || screen == ScreenId::MessageView || screen == ScreenId::Compose
        || screen == ScreenId::Channels;
}

std::string window_title(ScreenId screen) {
    return is_chat_screen(screen) ? "Chat" : screen_title(screen);
}

bool time_before(uint32_t left, uint32_t right) {
    return static_cast<int32_t>(left - right) < 0;
}

bool is_known_incoming_message(const std::string& id) {
    for (const auto& known_id : known_incoming_message_ids) {
        if (known_id == id) {
            return true;
        }
    }
    return false;
}

void remember_incoming_messages(const AppSnapshot& snapshot) {
    known_incoming_message_ids.clear();
    for (const auto& msg : snapshot.messages) {
        if (!msg.outgoing) {
            known_incoming_message_ids.push_back(msg.id);
        }
    }
}

void update_notification_from_snapshot() {
    for (const auto& msg : data.messages) {
        if (msg.outgoing) {
            continue;
        }
        if (!msg.id.empty() && !is_known_incoming_message(msg.id)) {
            notification_text = msg.sender + ": " + msg.body;
            notification_started_ms = ui_now_ms;
            notification_visible = true;
            if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
                std::fprintf(stderr, "notification show: %s\n", notification_text.c_str());
            }
        }
        remember_incoming_messages(data);
        return;
    }
    remember_incoming_messages(data);
}

void hide_notification_overlay() {
    if (notification_overlay != nullptr && lv_obj_is_valid(notification_overlay)) {
        lv_obj_delete(notification_overlay);
    }
    notification_overlay = nullptr;
    notification_visible = false;
    notification_text.clear();
}

int screen_index(ScreenId screen) {
    int index = 0;
    for (const auto known : all_screens()) {
        if (known == screen) {
            return index;
        }
        ++index;
    }
    return 0;
}

void set_plain(lv_obj_t* obj) {
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void set_panel(lv_obj_t* obj, uint32_t bg = ce_face) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(ce_shadow), 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void enable_vertical_scroll(lv_obj_t* obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void disable_scroll(lv_obj_t* obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void draw_scrollbar_indicator(lv_obj_t* parent,
                              int x,
                              int y,
                              int h,
                              std::size_t total,
                              std::size_t visible,
                              std::size_t first) {
    if (total <= visible || visible == 0 || h <= 4) {
        return;
    }
    lv_obj_t* track = lv_obj_create(parent);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, 7, h);
    set_plain(track);
    lv_obj_set_style_bg_color(track, lv_color_hex(0xe6e6e6), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(ce_shadow), 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_CLICKABLE);
    disable_scroll(track);

    const int inner_h = std::max(1, h - 2);
    const int thumb_h = std::min(inner_h, std::max<int>(8, (inner_h * static_cast<int>(visible)) / static_cast<int>(total)));
    const int max_offset = std::max<int>(1, static_cast<int>(total - visible));
    const int thumb_y = y + 1 + ((inner_h - thumb_h) * static_cast<int>(std::min(first, total - visible))) / max_offset;
    lv_obj_t* thumb = lv_obj_create(parent);
    lv_obj_set_pos(thumb, x + 1, thumb_y);
    lv_obj_set_size(thumb, 5, thumb_h);
    set_plain(thumb);
    lv_obj_set_style_bg_color(thumb, lv_color_hex(ce_shadow), 0);
    lv_obj_set_style_bg_opa(thumb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(thumb, 1, 0);
    lv_obj_set_style_border_color(thumb, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_clear_flag(thumb, LV_OBJ_FLAG_CLICKABLE);
    disable_scroll(thumb);
}

void add_notification_overlay(lv_obj_t* root) {
    if (!notification_visible || notification_text.empty()) {
        notification_overlay = nullptr;
        return;
    }
    if (ui_now_ms - notification_started_ms >= 5000U) {
        hide_notification_overlay();
        return;
    }

    lv_obj_t* overlay = lv_obj_create(root);
    notification_overlay = overlay;
    lv_obj_set_pos(overlay, 4, 2);
    lv_obj_set_size(overlay, screen_width - 8, 28);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0xffffcc), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 1, 0);
    lv_obj_set_style_border_color(overlay, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    add_bevel(overlay, 0, 0, screen_width - 8, 28, true);

    lv_obj_t* label = make_label(overlay, notification_text, 8, 7, screen_width - 24, 12);
    lv_obj_set_style_text_color(label, lv_color_hex(ce_text), 0);
    lv_obj_move_foreground(overlay);
}

void activate_screen_root(lv_obj_t* root) {
    if (lv_screen_active() != root) {
        lv_screen_load_anim(root, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
    }
}

void load_screen(lv_obj_t* root) {
    add_notification_overlay(root);
    add_edit_overlay(root);
    activate_screen_root(root);
}

void add_bevel(lv_obj_t* parent, int x, int y, int w, int h, bool raised = true) {
    lv_obj_t* top = lv_obj_create(parent);
    lv_obj_set_pos(top, x, y);
    lv_obj_set_size(top, w, 1);
    lv_obj_set_style_bg_color(top, lv_color_hex(raised ? ce_light : ce_shadow), 0);
    set_plain(top);

    lv_obj_t* left = lv_obj_create(parent);
    lv_obj_set_pos(left, x, y);
    lv_obj_set_size(left, 1, h);
    lv_obj_set_style_bg_color(left, lv_color_hex(raised ? ce_light : ce_shadow), 0);
    set_plain(left);

    lv_obj_t* bottom = lv_obj_create(parent);
    lv_obj_set_pos(bottom, x, y + h - 1);
    lv_obj_set_size(bottom, w, 1);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(raised ? ce_dark_shadow : ce_light), 0);
    set_plain(bottom);

    lv_obj_t* right = lv_obj_create(parent);
    lv_obj_set_pos(right, x + w - 1, y);
    lv_obj_set_size(right, 1, h);
    lv_obj_set_style_bg_color(right, lv_color_hex(raised ? ce_dark_shadow : ce_light), 0);
    set_plain(right);
}

const lv_font_t* font_for_size(int font_size) {
#if LV_FONT_MONTSERRAT_10
    if (font_size <= 10) {
        return &lv_font_montserrat_10;
    }
#endif
#if LV_FONT_MONTSERRAT_12
    if (font_size <= 12) {
        return &lv_font_montserrat_12;
    }
#endif
#if LV_FONT_MONTSERRAT_16
    if (font_size >= 16) {
        return &lv_font_montserrat_16;
    }
#endif
#if LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}

lv_obj_t* make_label(lv_obj_t* parent, const std::string& text, int x, int y, int w, int font_size = 12) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_height(label, font_size + 4);
    lv_obj_set_style_text_font(label, font_for_size(font_size), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ce_text), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_label_set_text(label, text.c_str());
    return label;
}

void add_edit_overlay(lv_obj_t* root) {
    if (data.state.edit_field == EditField::NoEdit) {
        return;
    }
    lv_obj_t* shade = lv_obj_create(root);
    lv_obj_set_pos(shade, 0, 0);
    lv_obj_set_size(shade, screen_width, screen_height);
    lv_obj_set_style_bg_color(shade, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(shade, LV_OPA_30, 0);
    set_plain(shade);
    lv_obj_clear_flag(shade, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* dialog = lv_obj_create(root);
    lv_obj_set_pos(dialog, 18, 58);
    lv_obj_set_size(dialog, 284, 118);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(ce_face), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_set_style_radius(dialog, 0, 0);
    lv_obj_set_style_pad_all(dialog, 0, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    add_bevel(dialog, 0, 0, 284, 118, true);

    lv_obj_t* title = lv_obj_create(dialog);
    lv_obj_set_pos(title, 2, 2);
    lv_obj_set_size(title, 280, 20);
    lv_obj_set_style_bg_color(title, lv_color_hex(ce_title), 0);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    set_plain(title);
    lv_obj_t* title_label = make_label(title, data.state.edit_title, 5, 4, 270, 12);
    lv_obj_set_style_text_color(title_label, lv_color_hex(ce_title_text), 0);

    make_label(dialog, "Type on keyboard. Enter=OK Esc=Cancel", 8, 30, 268, 10);
    lv_obj_t* value_box = lv_obj_create(dialog);
    lv_obj_set_pos(value_box, 8, 48);
    lv_obj_set_size(value_box, 268, 26);
    lv_obj_set_style_bg_color(value_box, lv_color_hex(ce_window), 0);
    lv_obj_set_style_bg_opa(value_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(value_box, 1, 0);
    lv_obj_set_style_border_color(value_box, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_set_style_radius(value_box, 0, 0);
    lv_obj_set_style_pad_all(value_box, 0, 0);
    lv_obj_clear_flag(value_box, LV_OBJ_FLAG_SCROLLABLE);
    make_label(value_box, data.state.edit_value + "_", 4, 6, 260, 12);
    const std::string status = data.state.edit_error.empty() ? "OK applies to firmware settings" : data.state.edit_error;
    lv_obj_t* status_label = make_label(dialog, status, 8, 82, 268, 10);
    lv_obj_set_style_text_color(status_label, lv_color_hex(data.state.edit_error.empty() ? 0x004000 : 0x800000), 0);
    lv_obj_move_foreground(shade);
    lv_obj_move_foreground(dialog);
}

void bind_desktop_click(lv_obj_t* obj, DesktopIconBinding* binding) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, on_desktop_icon, LV_EVENT_CLICKED, binding);
}

void on_action(lv_event_t* event) {
    const auto* binding = static_cast<ButtonBinding*>(lv_event_get_user_data(event));
    if (binding == nullptr) {
        return;
    }
    const auto actions = screen_actions(binding->source);
    if (binding->action_index >= 0 && binding->action_index < static_cast<int>(actions.size())) {
        trace_ui_target("ui: action", actions[static_cast<std::size_t>(binding->action_index)].target);
    }
    pending_navigation.active = true;
    pending_navigation.run_action = true;
    pending_navigation.start_app = false;
    pending_navigation.source = binding->source;
    pending_navigation.action_index = binding->action_index;
}

void on_chat_target(lv_event_t* event) {
    const auto* binding = static_cast<ChatTargetBinding*>(lv_event_get_user_data(event));
    if (binding == nullptr) {
        return;
    }
    if (binding->channel) {
        app_select_chat_channel(binding->index);
        ui_show(ScreenId::Channels);
    } else {
        app_select_chat_contact(binding->index);
        ui_show(ScreenId::Inbox);
    }
}

void on_desktop_icon(lv_event_t* event) {
    const auto* binding = static_cast<DesktopIconBinding*>(lv_event_get_user_data(event));
    if (binding == nullptr) {
        return;
    }
    if (current_screen == ScreenId::Home && time_before(lv_tick_get(), desktop_click_blocked_until_ms)) {
        trace_ui("ui: desktop click ignored after close");
        return;
    }
    trace_ui_target("ui: desktop", binding->target);
    pending_navigation.active = true;
    pending_navigation.run_action = false;
    pending_navigation.start_app = binding->command == ActionCommand::StartApp;
    pending_navigation.target = binding->target;
}

void on_close_window(lv_event_t*) {
    trace_ui("ui: close");
    desktop_click_blocked_until_ms = lv_tick_get() + 450U;
    pending_navigation.active = true;
    pending_navigation.run_action = false;
    pending_navigation.start_app = false;
    pending_navigation.target = ScreenId::Home;
}

lv_obj_t* make_button(lv_obj_t* parent, const char* text, int x, ButtonBinding* binding, int width = 44) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, width, 22);
    lv_obj_set_pos(btn, x, 3);
    lv_obj_set_style_bg_color(btn, lv_color_hex(ce_face), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0xe6e6e6), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, on_action, LV_EVENT_CLICKED, binding);
    add_bevel(btn, 0, 0, width, 22, true);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font_for_size(10), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ce_text), 0);
    lv_obj_center(label);
    return btn;
}

lv_obj_t* make_root(ScreenId screen) {
    lv_obj_t* root = lv_obj_create(NULL);
    lv_obj_set_size(root, screen_width, screen_height);
    lv_obj_set_style_bg_color(root, lv_color_hex(ce_face), 0);
    lv_obj_set_style_text_color(root, lv_color_hex(ce_text), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    set_plain(root);
    activate_screen_root(root);

    lv_obj_t* top = lv_obj_create(root);
    lv_obj_set_size(top, screen_width, top_bar_height);
    lv_obj_set_style_bg_color(top, lv_color_hex(ce_title), 0);
    lv_obj_set_style_bg_grad_color(top, lv_color_hex(ce_title_alt), 0);
    lv_obj_set_style_bg_grad_dir(top, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_text_color(top, lv_color_hex(ce_title_text), 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    set_plain(top);

    lv_obj_t* title = make_label(top, window_title(screen), 6, 5, 150, 14);
    lv_obj_set_style_text_color(title, lv_color_hex(ce_title_text), 0);

    lv_obj_t* close = lv_button_create(top);
    lv_obj_set_pos(close, 300, 4);
    lv_obj_set_size(close, 16, 14);
    lv_obj_set_style_bg_color(close, lv_color_hex(ce_face), 0);
    lv_obj_set_style_bg_opa(close, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(close, lv_color_hex(0xe6e6e6), 0);
    lv_obj_set_style_bg_grad_dir(close, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(close, 1, 0);
    lv_obj_set_style_border_color(close, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_set_style_radius(close, 0, 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_set_style_pad_all(close, 0, 0);
    lv_obj_clear_flag(close, LV_OBJ_FLAG_SCROLLABLE);
    add_bevel(close, 0, 0, 16, 14, true);
    lv_obj_add_event_cb(close, on_close_window, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* close_label = lv_label_create(close);
    lv_label_set_text(close_label, "x");
    lv_obj_set_style_text_font(close_label, font_for_size(10), 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(ce_text), 0);
    lv_obj_center(close_label);
    lv_obj_add_flag(close_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_label, on_close_window, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* actions = lv_obj_create(root);
    active_window_toolbar = actions;
    lv_obj_set_size(actions, screen_width, ce_toolbar_height);
    lv_obj_set_pos(actions, 0, ce_toolbar_y);
    lv_obj_set_style_bg_color(actions, lv_color_hex(ce_face), 0);
    lv_obj_set_style_bg_opa(actions, LV_OPA_COVER, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    set_plain(actions);
    add_bevel(actions, 0, 0, screen_width, ce_toolbar_height, true);

    lv_obj_t* bottom = lv_obj_create(root);
    active_window_taskbar = bottom;
    lv_obj_set_size(bottom, screen_width, ce_taskbar_height);
    lv_obj_set_pos(bottom, 0, ce_taskbar_y);
    lv_obj_set_style_bg_color(bottom, lv_color_hex(ce_face), 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    set_plain(bottom);
    add_bevel(bottom, 0, 0, screen_width, ce_taskbar_height, true);

    return root;
}

lv_obj_t* content(lv_obj_t* root) {
    lv_obj_t* area = lv_obj_create(root);
    lv_obj_set_size(area, screen_width, ce_window_content_height);
    lv_obj_set_pos(area, 0, ce_window_content_y);
    lv_obj_set_style_bg_color(area, lv_color_hex(ce_face), 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    set_plain(area);
    enable_vertical_scroll(area);
    if (active_window_taskbar != nullptr) {
        lv_obj_move_foreground(active_window_taskbar);
    }
    return area;
}

lv_obj_t* bottom(lv_obj_t* root) {
    if (active_window_taskbar != nullptr) {
        return active_window_taskbar;
    }
    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(root, static_cast<int32_t>(i));
        if (child != nullptr && lv_obj_get_y(child) == ce_taskbar_y
            && lv_obj_get_height(child) == ce_taskbar_height) {
            return child;
        }
    }
    return lv_obj_get_child(root, -1);
}

lv_obj_t* toolbar(lv_obj_t* root) {
    if (active_window_toolbar != nullptr) {
        return active_window_toolbar;
    }
    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* child = lv_obj_get_child(root, static_cast<int32_t>(i));
        if (child != nullptr && lv_obj_get_y(child) == ce_toolbar_y
            && lv_obj_get_height(child) == ce_toolbar_height) {
            return child;
        }
    }
    return root;
}

void add_actions(lv_obj_t* root, ScreenId screen) {
    const auto actions = screen_actions(screen);
    const int index = screen_index(screen);
    for (int i = 0; i < 4; ++i) {
        button_bindings[static_cast<std::size_t>(index)][static_cast<std::size_t>(i)] = {screen, i};
    }
    taskbar_start_binding = {ScreenId::Inbox};
    make_start_button(bottom(root), &taskbar_start_binding);
    make_button(toolbar(root), actions[0].label.c_str(), 4, &button_bindings[static_cast<std::size_t>(index)][0], 54);
    make_button(toolbar(root), actions[1].label.c_str(), 62, &button_bindings[static_cast<std::size_t>(index)][1], 54);
    make_button(toolbar(root), actions[2].label.c_str(), 120, &button_bindings[static_cast<std::size_t>(index)][2], 54);
    make_button(toolbar(root), actions[3].label.c_str(), 178, &button_bindings[static_cast<std::size_t>(index)][3], 54);
    lv_obj_t* battery_label = make_label(bottom(root), taskbar_battery_text(), 230, 8, 30, 10);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t* time_shadow = make_label(bottom(root), taskbar_time_text(), 267, 8, 49, 10);
    lv_obj_set_style_text_align(time_shadow, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t* time_label = make_label(bottom(root), taskbar_time_text(), 266, 8, 49, 10);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_move_foreground(bottom(root));
}

void add_row(lv_obj_t* parent, int y, const std::string& left, const std::string& detail, const std::string& right) {
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, 300, 32);
    lv_obj_set_pos(line, 10, y);
    lv_obj_set_style_bg_color(line, lv_color_hex(ce_window), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 1, 0);
    lv_obj_set_style_border_color(line, lv_color_hex(ce_shadow), 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    make_label(line, left, 4, 3, 154, 12);
    if (!detail.empty()) {
        lv_obj_t* sub = make_label(line, detail, 4, 18, 190, 10);
        lv_obj_set_style_text_color(sub, lv_color_hex(ce_dark_shadow), 0);
    }
    lv_obj_t* r = make_label(line, right, 188, 9, 108, 12);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_RIGHT, 0);
}

void add_selected_row(lv_obj_t* parent, int y, bool selected, const std::string& left,
                      const std::string& detail, const std::string& right) {
    add_row(parent, y, selected ? "> " + left : left, detail, right);
}

lv_obj_t* add_icon_part(lv_obj_t* parent,
                        DesktopIconBinding* binding,
                        int x,
                        int y,
                        int w,
                        int h,
                        uint32_t color,
                        uint32_t border = ce_dark_shadow,
                        int border_width = 0,
                        int radius = 0) {
    lv_obj_t* part = lv_obj_create(parent);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_size(part, w, h);
    lv_obj_set_style_bg_color(part, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(part, border_width, 0);
    lv_obj_set_style_border_color(part, lv_color_hex(border), 0);
    lv_obj_set_style_radius(part, radius, 0);
    lv_obj_set_style_pad_all(part, 0, 0);
    lv_obj_clear_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    bind_desktop_click(part, binding);
    return part;
}

void add_icon_text(lv_obj_t* parent,
                   DesktopIconBinding* binding,
                   const std::string& text,
                   int x,
                   int y,
                   int w,
                   int size,
                   uint32_t color) {
    lv_obj_t* label = make_label(parent, text, x, y, w, size);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    bind_desktop_click(label, binding);
}

void draw_desktop_icon(lv_obj_t* parent, DesktopIconKind kind, DesktopIconBinding* binding) {
    lv_obj_t* icon = lv_obj_create(parent);
    lv_obj_set_pos(icon, 13, 0);
    lv_obj_set_size(icon, 32, 32);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    set_plain(icon);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    bind_desktop_click(icon, binding);

    switch (kind) {
        case DesktopIconKind::Inbox:
            add_icon_part(icon, binding, 5, 9, 22, 16, 0xffffff, 0x303030, 1);
            add_icon_part(icon, binding, 6, 10, 20, 4, 0x9ec3ff);
            add_icon_part(icon, binding, 7, 16, 18, 2, 0x000080);
            add_icon_part(icon, binding, 9, 19, 14, 2, 0x000080);
            add_icon_part(icon, binding, 12, 22, 8, 2, 0x000080);
            break;
        case DesktopIconKind::Compose:
            add_icon_part(icon, binding, 8, 3, 17, 23, 0xffffff, 0x303030, 1);
            add_icon_part(icon, binding, 10, 6, 11, 2, 0x1084d0);
            add_icon_part(icon, binding, 10, 11, 11, 1, 0x808080);
            add_icon_part(icon, binding, 10, 15, 9, 1, 0x808080);
            add_icon_part(icon, binding, 5, 22, 17, 5, 0xf8d35b, 0x303030, 1);
            add_icon_part(icon, binding, 4, 25, 4, 3, 0x303030);
            break;
        case DesktopIconKind::Nodes:
            add_icon_part(icon, binding, 14, 6, 4, 17, 0x000080);
            add_icon_part(icon, binding, 7, 13, 18, 3, 0x000080);
            add_icon_part(icon, binding, 3, 5, 10, 8, 0x9ec3ff, 0x303030, 1);
            add_icon_part(icon, binding, 19, 5, 10, 8, 0x9ec3ff, 0x303030, 1);
            add_icon_part(icon, binding, 11, 20, 10, 8, 0x9ec3ff, 0x303030, 1);
            add_icon_part(icon, binding, 6, 14, 4, 2, 0x303030);
            add_icon_part(icon, binding, 22, 14, 4, 2, 0x303030);
            add_icon_part(icon, binding, 14, 29, 4, 1, 0x303030);
            break;
        case DesktopIconKind::Contacts:
            add_icon_part(icon, binding, 6, 5, 20, 22, 0xffffff, 0x303030, 1);
            add_icon_part(icon, binding, 9, 8, 8, 8, 0x9ec3ff, 0x303030, 1, 4);
            add_icon_part(icon, binding, 8, 18, 10, 4, 0x000080);
            add_icon_part(icon, binding, 20, 9, 6, 2, 0x008000);
            add_icon_part(icon, binding, 20, 14, 6, 2, 0x008000);
            add_icon_part(icon, binding, 20, 19, 6, 2, 0x008000);
            break;
        case DesktopIconKind::Channels:
            add_icon_part(icon, binding, 4, 10, 24, 15, 0xf8d35b, 0x805a00, 1);
            add_icon_part(icon, binding, 5, 6, 11, 6, 0xffe080, 0x805a00, 1);
            add_icon_part(icon, binding, 7, 14, 18, 2, 0xffffff);
            add_icon_part(icon, binding, 7, 18, 15, 2, 0x1084d0);
            add_icon_part(icon, binding, 7, 22, 12, 2, 0x1084d0);
            break;
        case DesktopIconKind::ChannelEditor:
            add_icon_part(icon, binding, 4, 6, 24, 20, 0xffffff, 0x303030, 1);
            add_icon_part(icon, binding, 7, 10, 18, 2, 0x000080);
            add_icon_part(icon, binding, 7, 15, 14, 2, 0x008000);
            add_icon_part(icon, binding, 7, 20, 16, 2, 0xc00000);
            add_icon_part(icon, binding, 23, 20, 5, 5, 0xf8d35b, 0x303030, 1);
            break;
        case DesktopIconKind::Map:
            add_icon_part(icon, binding, 4, 5, 22, 22, 0xcfeec7, 0x303030, 1);
            add_icon_part(icon, binding, 5, 12, 20, 2, 0x5da15d);
            add_icon_part(icon, binding, 13, 6, 2, 20, 0x5da15d);
            add_icon_part(icon, binding, 18, 16, 7, 7, 0xc00000, 0x303030, 1, 4);
            add_icon_part(icon, binding, 20, 23, 3, 5, 0xc00000);
            add_icon_part(icon, binding, 20, 18, 3, 3, 0xffffff, 0xffffff, 0, 2);
            break;
        case DesktopIconKind::Settings:
            add_icon_part(icon, binding, 5, 5, 22, 22, 0xd8d8d8, 0x303030, 1);
            add_icon_part(icon, binding, 9, 10, 14, 2, 0x000080);
            add_icon_part(icon, binding, 9, 16, 14, 2, 0x000080);
            add_icon_part(icon, binding, 9, 22, 14, 2, 0x000080);
            add_icon_part(icon, binding, 12, 8, 4, 6, 0xffe080, 0x303030, 1);
            add_icon_part(icon, binding, 18, 14, 4, 6, 0xffe080, 0x303030, 1);
            add_icon_part(icon, binding, 10, 20, 4, 6, 0xffe080, 0x303030, 1);
            break;
        case DesktopIconKind::Radio:
            add_icon_part(icon, binding, 15, 3, 2, 14, 0x303030);
            add_icon_part(icon, binding, 10, 6, 2, 5, 0x1084d0);
            add_icon_part(icon, binding, 20, 6, 2, 5, 0x1084d0);
            add_icon_part(icon, binding, 6, 2, 2, 10, 0x1084d0);
            add_icon_part(icon, binding, 24, 2, 2, 10, 0x1084d0);
            add_icon_part(icon, binding, 7, 17, 18, 10, 0xd8d8d8, 0x303030, 1);
            add_icon_part(icon, binding, 10, 20, 5, 4, 0x000080);
            add_icon_part(icon, binding, 18, 20, 4, 4, 0xc00000, 0x303030, 1, 3);
            break;
        case DesktopIconKind::RadioAdvanced:
            add_icon_part(icon, binding, 5, 16, 22, 11, 0xd8d8d8, 0x303030, 1);
            add_icon_part(icon, binding, 8, 19, 5, 3, 0x000080);
            add_icon_part(icon, binding, 15, 19, 5, 3, 0x008000);
            add_icon_part(icon, binding, 22, 19, 3, 3, 0xc00000);
            add_icon_part(icon, binding, 15, 3, 2, 13, 0x303030);
            add_icon_part(icon, binding, 9, 5, 2, 8, 0x1084d0);
            add_icon_part(icon, binding, 21, 5, 2, 8, 0x1084d0);
            break;
        case DesktopIconKind::Identity:
            add_icon_part(icon, binding, 7, 5, 18, 22, 0xffffff, 0x303030, 1);
            add_icon_part(icon, binding, 11, 8, 10, 8, 0x9ec3ff, 0x303030, 1, 4);
            add_icon_part(icon, binding, 10, 18, 12, 3, 0x000080);
            add_icon_part(icon, binding, 10, 23, 12, 2, 0x808080);
            break;
        case DesktopIconKind::Ble:
            add_icon_part(icon, binding, 15, 4, 3, 24, 0x1084d0);
            add_icon_part(icon, binding, 16, 5, 8, 8, 0x1084d0);
            add_icon_part(icon, binding, 16, 19, 8, 8, 0x1084d0);
            add_icon_part(icon, binding, 9, 10, 8, 8, 0xffffff);
            add_icon_part(icon, binding, 9, 16, 8, 8, 0xffffff);
            add_icon_part(icon, binding, 11, 12, 4, 4, 0x1084d0);
            add_icon_part(icon, binding, 11, 18, 4, 4, 0x1084d0);
            break;
        case DesktopIconKind::Servers:
            add_icon_part(icon, binding, 8, 4, 17, 24, 0xd8d8d8, 0x303030, 1);
            add_icon_part(icon, binding, 10, 8, 13, 1, 0xffffff);
            add_icon_part(icon, binding, 10, 14, 13, 1, 0xffffff);
            add_icon_part(icon, binding, 10, 20, 13, 1, 0xffffff);
            add_icon_part(icon, binding, 11, 10, 4, 2, 0x1084d0);
            add_icon_part(icon, binding, 11, 16, 4, 2, 0x1084d0);
            add_icon_part(icon, binding, 11, 22, 4, 2, 0x1084d0);
            add_icon_part(icon, binding, 19, 10, 2, 2, 0x00a000);
            add_icon_part(icon, binding, 19, 16, 2, 2, 0x00a000);
            add_icon_part(icon, binding, 19, 22, 2, 2, 0xc00000);
            break;
        case DesktopIconKind::Tools:
            add_icon_part(icon, binding, 5, 20, 22, 6, 0x808080, 0x303030, 1);
            add_icon_part(icon, binding, 8, 8, 8, 14, 0xd8d8d8, 0x303030, 1);
            add_icon_part(icon, binding, 17, 6, 8, 16, 0xf8d35b, 0x303030, 1);
            add_icon_part(icon, binding, 20, 3, 4, 5, 0x303030);
            break;
        case DesktopIconKind::Diagnostics:
            add_icon_part(icon, binding, 6, 5, 20, 22, 0xffe080, 0x303030, 1);
            add_icon_part(icon, binding, 9, 8, 14, 3, 0xc00000);
            add_icon_part(icon, binding, 15, 13, 3, 8, 0x303030);
            add_icon_part(icon, binding, 15, 23, 3, 3, 0x303030);
            add_icon_text(icon, binding, "!", 11, 9, 10, 16, 0x303030);
            break;
    }
}

lv_obj_t* make_desktop_icon(lv_obj_t* parent,
                            const char* label,
                            DesktopIconKind kind,
                            int x,
                            int y,
                            DesktopIconBinding* binding) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 58, 58);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    bind_desktop_click(btn, binding);

    draw_desktop_icon(btn, kind, binding);

    lv_obj_t* text = make_label(btn, label, 0, 35, 58, 12);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(ce_title_text), 0);
    bind_desktop_click(text, binding);
    return btn;
}

lv_obj_t* make_start_button(lv_obj_t* parent, DesktopIconBinding* binding) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_pos(btn, 4, 3);
    lv_obj_set_size(btn, 50, 22);
    lv_obj_set_style_bg_color(btn, lv_color_hex(ce_face), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    bind_desktop_click(btn, binding);
    add_bevel(btn, 0, 0, 50, 22, true);

    lv_obj_t* label = make_label(btn, "Chat", 0, 5, 50, 10);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ce_text), 0);
    bind_desktop_click(label, binding);
    return btn;
}

std::size_t bounded_index(int index, std::size_t size) {
    if (size == 0 || index < 0) {
        return 0;
    }
    const auto value = static_cast<std::size_t>(index);
    return value < size ? value : size - 1;
}

std::string chat_time(const MeshMessage& message, std::size_t index) {
    const unsigned minutes = message.timestamp > 0
        ? (message.timestamp / 60U) % (24U * 60U)
        : (9U * 60U + 42U + static_cast<unsigned>(index) * 2U) % (24U * 60U);
    char buffer[8] = {};
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u", minutes / 60U, minutes % 60U);
    return buffer;
}

std::string chat_channel_name() {
    if (!data.channels.empty()) {
        const auto& channel = data.channels[bounded_index(data.state.selected_channel, data.channels.size())];
        if (!channel.name.empty()) {
            return "#" + channel.name;
        }
    }
    return "#" + data.state.channel;
}

std::string chat_contact_name() {
    if (!data.nodes.empty()) {
        return data.nodes[bounded_index(data.state.selected_node, data.nodes.size())].name;
    }
    return data.state.compose_recipient.empty() ? "broadcast" : data.state.compose_recipient;
}

bool chat_is_channel_message(const MeshMessage& message) {
    return message.subject.rfind("Channel ", 0) == 0;
}

std::string chat_message_channel(const MeshMessage& message) {
    return chat_is_channel_message(message) ? message.subject.substr(8) : "";
}

bool text_matches_peer(const std::string& value, const NodeInfo& node) {
    return value == node.short_id || value == node.name ||
           (!node.short_id.empty() && value.find(node.short_id) != std::string::npos) ||
           (!node.name.empty() && value.find(node.name) != std::string::npos);
}

bool chat_message_matches(const MeshMessage& message, bool channel_mode) {
    if (channel_mode) {
        if (!chat_is_channel_message(message)) {
            return false;
        }
        const std::string channel = chat_message_channel(message);
        if (!data.channels.empty()) {
            const auto& selected = data.channels[bounded_index(data.state.selected_channel, data.channels.size())];
            return channel == selected.name;
        }
        return channel == data.state.channel;
    }

    if (chat_is_channel_message(message)) {
        return false;
    }
    if (data.nodes.empty()) {
        return true;
    }
    const auto& selected = data.nodes[bounded_index(data.state.selected_node, data.nodes.size())];
    if (message.outgoing) {
        return text_matches_peer(message.subject, selected);
    }
    return text_matches_peer(message.sender, selected);
}

void add_chat_roster_header(lv_obj_t* parent, const std::string& text, int y) {
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_pos(header, 0, y);
    lv_obj_set_size(header, 86, 13);
    lv_obj_set_style_bg_color(header, lv_color_hex(ce_title), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    set_plain(header);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* label = make_label(header, text, 3, 1, 80, 10);
    lv_obj_set_style_text_color(label, lv_color_hex(ce_selected_text), 0);
}

void add_chat_roster_row(lv_obj_t* parent,
                         int y,
                         bool selected,
                         const std::string& name,
                         const std::string& detail,
                         ChatTargetBinding* binding) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_pos(row, 0, y);
    lv_obj_set_size(row, 86, 16);
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? 0xd7e7ff : ce_window), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    if (binding != nullptr) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_chat_target, LV_EVENT_CLICKED, binding);
    }
    lv_obj_t* dot = lv_obj_create(row);
    lv_obj_set_pos(dot, 2, 5);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x00a000), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(0x007000), 0);
    lv_obj_set_style_radius(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    make_label(row, name, 12, 2, 50, 10);
    lv_obj_t* right = make_label(row, detail, 63, 2, 20, 10);
    lv_obj_set_style_text_align(right, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(right, lv_color_hex(ce_dark_shadow), 0);
}

void add_chat_line(lv_obj_t* parent, int y, const MeshMessage& message, std::size_t index, bool selected) {
    const std::string sender = message.outgoing ? "me" : message.sender;
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_pos(row, 2, y);
    lv_obj_set_size(row, 214, 13);
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? ce_selected : ce_window), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* time = make_label(row, chat_time(message, index), 1, 1, 30, 10);
    lv_obj_set_style_text_color(time, lv_color_hex(0x606060), 0);
    if (selected) {
        lv_obj_set_style_text_color(time, lv_color_hex(ce_selected_text), 0);
    }
    lv_obj_t* who = make_label(row, sender, 32, 1, 46, 10);
    lv_obj_set_style_text_color(who, lv_color_hex(selected ? ce_selected_text : (message.outgoing ? 0x008000 : ce_title)), 0);
    lv_obj_t* body = make_label(row, message.body, 80, 1, 130, 10);
    lv_obj_set_style_text_color(body, lv_color_hex(selected ? ce_selected_text : ce_text), 0);
}

void show_chat(ScreenId screen) {
    const bool channel_mode = screen == ScreenId::Channels || data.state.compose_recipient == "broadcast";
    lv_obj_t* root = make_root(screen);
    lv_obj_t* area = content(root);
    disable_scroll(area);

    lv_obj_t* mode_label = make_label(area, channel_mode ? "Channel selected" : "DM selected", 4, 4, 100, 10);
    lv_obj_set_style_text_color(mode_label, lv_color_hex(channel_mode ? ce_title : 0x004000), 0);
    lv_obj_t* mesh_label = make_label(area, data.state.connected ? "MeshCore online" : "MeshCore offline", 206, 4, 108, 10);
    lv_obj_set_style_text_align(mesh_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(mesh_label, lv_color_hex(data.state.connected ? 0x004000 : 0x800000), 0);

    lv_obj_t* roster = lv_obj_create(area);
    lv_obj_set_pos(roster, 4, 18);
    lv_obj_set_size(roster, 88, 114);
    set_panel(roster, ce_window);
    disable_scroll(roster);
    add_chat_roster_header(roster, "Contacts", 0);
    int y = 13;
    int binding_index = 0;
    const std::size_t selected_node = bounded_index(data.state.selected_node, data.nodes.size());
    const std::size_t visible_contacts = std::min<std::size_t>(3, data.nodes.size());
    const std::size_t first_contact = data.nodes.size() <= visible_contacts
        ? 0
        : std::min<std::size_t>(selected_node, data.nodes.size() - visible_contacts);
    for (std::size_t offset = 0; offset < visible_contacts && binding_index < static_cast<int>(chat_target_bindings.size()); ++offset) {
        const std::size_t i = first_contact + offset;
        chat_target_bindings[static_cast<std::size_t>(binding_index)] = {false, static_cast<int>(i)};
        add_chat_roster_row(roster, y, !channel_mode && i == bounded_index(data.state.selected_node, data.nodes.size()),
                            data.nodes[i].name, "DM",
                            &chat_target_bindings[static_cast<std::size_t>(binding_index)]);
        ++binding_index;
        y += 16;
    }
    add_chat_roster_header(roster, "Channels", y);
    y += 13;
    const std::size_t selected_channel = bounded_index(data.state.selected_channel, data.channels.size());
    const std::size_t visible_channels = std::min<std::size_t>(3, data.channels.size());
    const std::size_t first_channel = data.channels.size() <= visible_channels
        ? 0
        : std::min<std::size_t>(selected_channel, data.channels.size() - visible_channels);
    for (std::size_t offset = 0; offset < visible_channels && binding_index < static_cast<int>(chat_target_bindings.size()); ++offset) {
        const std::size_t i = first_channel + offset;
        chat_target_bindings[static_cast<std::size_t>(binding_index)] = {true, static_cast<int>(i)};
        add_chat_roster_row(roster, y, channel_mode && i == bounded_index(data.state.selected_channel, data.channels.size()),
                            "#" + data.channels[i].name, std::to_string(data.channels[i].users),
                            &chat_target_bindings[static_cast<std::size_t>(binding_index)]);
        ++binding_index;
        y += 16;
    }
    draw_scrollbar_indicator(roster,
                             80,
                             14,
                             46,
                             data.nodes.size(),
                             visible_contacts,
                             first_contact);
    draw_scrollbar_indicator(roster,
                             80,
                             75,
                             36,
                             data.channels.size(),
                             visible_channels,
                             first_channel);

    lv_obj_t* transcript = lv_obj_create(area);
    lv_obj_set_pos(transcript, 96, 18);
    lv_obj_set_size(transcript, 220, 114);
    set_panel(transcript, ce_window);
    disable_scroll(transcript);
    lv_obj_t* title = make_label(transcript, channel_mode ? chat_channel_name() : chat_contact_name(), 3, 2, 212, 10);
    lv_obj_set_style_text_color(title, lv_color_hex(ce_title), 0);
    lv_obj_t* rule = lv_obj_create(transcript);
    lv_obj_set_pos(rule, 2, 14);
    lv_obj_set_size(rule, 216, 1);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0xd0d0d0), 0);
    set_plain(rule);

    std::vector<std::size_t> visible_messages;
    bool selected_in_conversation = false;
    for (std::size_t i = 0; i < data.messages.size(); ++i) {
        if (!chat_message_matches(data.messages[i], channel_mode)) {
            continue;
        }
        if (static_cast<int>(i) == data.state.selected_message) {
            selected_in_conversation = true;
        }
        visible_messages.push_back(i);
    }
    const std::size_t transcript_rows = 7;
    std::size_t selected_visible = 0;
    if (selected_in_conversation) {
        for (std::size_t i = 0; i < visible_messages.size(); ++i) {
            if (static_cast<int>(visible_messages[i]) == data.state.selected_message) {
                selected_visible = i;
                break;
            }
        }
    }
    const std::size_t first_message = visible_messages.size() <= transcript_rows
        ? 0
        : std::min<std::size_t>(selected_visible > 3 ? selected_visible - 3 : 0,
                                visible_messages.size() - transcript_rows);
    int line_y = 17;
    std::size_t displayed = 0;
    for (std::size_t i = first_message; i < visible_messages.size() && displayed < transcript_rows; ++i) {
        const std::size_t message_index = visible_messages[i];
        const bool selected = selected_in_conversation
            ? static_cast<int>(message_index) == data.state.selected_message
            : displayed == 0;
        add_chat_line(transcript, line_y, data.messages[message_index], displayed, selected);
        line_y += 13;
        ++displayed;
    }
    draw_scrollbar_indicator(transcript,
                             212,
                             17,
                             92,
                             visible_messages.size(),
                             transcript_rows,
                             first_message);
    if (displayed == 0) {
        lv_obj_t* empty = make_label(transcript, "No messages in this conversation.", 3, 30, 212, 10);
        lv_obj_set_style_text_color(empty, lv_color_hex(ce_dark_shadow), 0);
    }

    const MeshMessage* selected_message = nullptr;
    if (!visible_messages.empty()) {
        const auto selected_index = selected_in_conversation
            ? bounded_index(data.state.selected_message, data.messages.size())
            : visible_messages.front();
        if (selected_index < data.messages.size()) {
            selected_message = &data.messages[selected_index];
        }
    }
    if (selected_message != nullptr) {
        const std::string status = (selected_message->outgoing ? "out " : "in ") +
            (selected_message->acked ? std::string("ack ") : selected_message->delivered ? std::string("sent ") : std::string("")) +
            (selected_message->persisted ? std::string("saved ") : std::string("")) +
            selected_message->status;
        lv_obj_t* status_label = make_label(area, status, 96, 121, 218, 10);
        lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(status_label, lv_color_hex(ce_dark_shadow), 0);
    }

    make_label(area, "To:", 4, 139, 22, 10);
    lv_obj_t* target = make_label(area, channel_mode ? chat_channel_name() : chat_contact_name(), 26, 139, 68, 10);
    lv_obj_set_style_text_color(target, lv_color_hex(ce_title), 0);
    lv_obj_t* compose = lv_obj_create(area);
    lv_obj_set_pos(compose, 96, 134);
    lv_obj_set_size(compose, 220, 22);
    lv_obj_set_style_bg_color(compose, lv_color_hex(ce_window), 0);
    lv_obj_set_style_bg_opa(compose, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(compose, 1, 0);
    lv_obj_set_style_border_color(compose, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_set_style_radius(compose, 0, 0);
    lv_obj_set_style_pad_all(compose, 0, 0);
    disable_scroll(compose);
    make_label(compose, data.state.compose_text, 3, 4, 212, 10);

    add_actions(root, screen);
    load_screen(root);
}

void show_boot() {
    lv_obj_t* root = make_root(ScreenId::Boot);
    lv_obj_t* area = content(root);
    lv_obj_t* panel = lv_obj_create(area);
    lv_obj_set_pos(panel, 34, 40);
    lv_obj_set_size(panel, 252, 72);
    set_panel(panel, ce_window);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    add_bevel(panel, 0, 0, 252, 72, false);
    lv_obj_t* brand = make_label(panel, "MeshCore", 0, 16, 252, 16);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* sub = make_label(panel, "T-Deck Plus", 0, 42, 252, 12);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    add_actions(root, ScreenId::Boot);
    load_screen(root);
}

void show_home() {
    lv_obj_t* root = lv_obj_create(NULL);
    lv_obj_set_size(root, screen_width, screen_height);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x008080), 0);
    lv_obj_set_style_text_color(root, lv_color_hex(ce_title_text), 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    set_plain(root);
    activate_screen_root(root);

    desktop_icon_bindings = {{
        {ScreenId::Inbox},
        {ScreenId::Contacts},
        {ScreenId::ChannelEditor},
        {ScreenId::Radio},
        {ScreenId::Map},
        {ScreenId::Nodes},
        {ScreenId::RadioAdvanced},
        {ScreenId::Identity},
        {ScreenId::Ble},
        {ScreenId::Settings},
        {ScreenId::Servers},
        {ScreenId::Tools},
        {ScreenId::Diagnostics},
    }};

    make_desktop_icon(root, "Chat", DesktopIconKind::Inbox, 8, 8, &desktop_icon_bindings[0]);
    make_desktop_icon(root, "Contacts", DesktopIconKind::Contacts, 70, 8, &desktop_icon_bindings[1]);
    make_desktop_icon(root, "Channels", DesktopIconKind::ChannelEditor, 132, 8, &desktop_icon_bindings[2]);
    make_desktop_icon(root, "Radio", DesktopIconKind::Radio, 194, 8, &desktop_icon_bindings[3]);
    make_desktop_icon(root, "Map", DesktopIconKind::Map, 256, 8, &desktop_icon_bindings[4]);
    make_desktop_icon(root, "Nodes", DesktopIconKind::Nodes, 8, 80, &desktop_icon_bindings[5]);
    make_desktop_icon(root, "Radio+", DesktopIconKind::RadioAdvanced, 70, 80, &desktop_icon_bindings[6]);
    make_desktop_icon(root, "Identity", DesktopIconKind::Identity, 132, 80, &desktop_icon_bindings[7]);
    make_desktop_icon(root, "BLE", DesktopIconKind::Ble, 194, 80, &desktop_icon_bindings[8]);
    make_desktop_icon(root, "Settings", DesktopIconKind::Settings, 256, 80, &desktop_icon_bindings[9]);
    make_desktop_icon(root, "Servers", DesktopIconKind::Servers, 8, 152, &desktop_icon_bindings[10]);
    make_desktop_icon(root, "Tools", DesktopIconKind::Tools, 70, 152, &desktop_icon_bindings[11]);
    make_desktop_icon(root, "Diag", DesktopIconKind::Diagnostics, 132, 152, &desktop_icon_bindings[12]);

    lv_obj_t* taskbar = lv_obj_create(root);
    lv_obj_set_size(taskbar, screen_width, ce_taskbar_height);
    lv_obj_set_pos(taskbar, 0, ce_taskbar_y);
    lv_obj_set_style_bg_color(taskbar, lv_color_hex(ce_face), 0);
    lv_obj_clear_flag(taskbar, LV_OBJ_FLAG_SCROLLABLE);
    set_plain(taskbar);
    add_bevel(taskbar, 0, 0, screen_width, ce_taskbar_height, true);
    taskbar_start_binding = {ScreenId::Inbox};
    make_start_button(taskbar, &taskbar_start_binding);

    lv_obj_t* battery_label = make_label(taskbar, taskbar_battery_text(), 230, 8, 30, 10);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t* time_shadow = make_label(taskbar, taskbar_time_text(), 267, 8, 49, 10);
    lv_obj_set_style_text_align(time_shadow, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t* time_label = make_label(taskbar, taskbar_time_text(), 266, 8, 49, 10);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
    load_screen(root);
}

void show_inbox() {
    show_chat(ScreenId::Inbox);
}

void show_message() {
    show_chat(ScreenId::MessageView);
}

void show_compose() {
    show_chat(ScreenId::Compose);
}

std::string bool_text(bool value) {
    return value ? "on" : "off";
}

std::string key_prefix(const std::array<unsigned char, NodeInfo::public_key_size>& key) {
    bool any = false;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < 4 && i < key.size(); ++i) {
        any = any || key[i] != 0;
        out << std::setw(2) << static_cast<unsigned>(key[i]);
    }
    return any ? out.str() : "none";
}

std::string path_text(const NodeInfo& node) {
    if (node.out_path_len == NodeInfo::out_path_unknown) {
        return "unknown";
    }
    if (node.out_path_len == 0) {
        return "direct";
    }
    return std::to_string(node.out_path_len) + " hop";
}

std::string secret_text(const ChannelInfo& channel) {
    bool any = false;
    for (const auto byte : channel.secret) {
        any = any || byte != 0;
    }
    return any ? "custom" : "public";
}

std::string advert_policy_text() {
    switch (data.state.advert_location_policy % 3U) {
        case 1:
            return "manual";
        case 2:
            return "gps";
        default:
            return "none";
    }
}

void show_nodes() {
    lv_obj_t* root = make_root(ScreenId::Nodes);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    constexpr std::size_t visible_rows = 5;
    const std::size_t selected = bounded_index(data.state.selected_node, data.nodes.size());
    const std::size_t first = data.nodes.size() <= visible_rows
        ? 0
        : std::min<std::size_t>(selected > 2 ? selected - 2 : 0, data.nodes.size() - visible_rows);
    int y = 0;
    for (std::size_t i = first; i < data.nodes.size() && i < first + visible_rows; ++i) {
        const auto& node = data.nodes[i];
        detail_buffer = node.short_id + " seen " + std::to_string(node.last_seen_seconds) + "m";
        if (node.has_position) {
            detail_buffer += " pos";
        }
        add_selected_row(area, y, static_cast<int>(i) == data.state.selected_node,
                         node.name, detail_buffer, std::to_string(node.rssi) + " " + fmt_float(node.snr));
        y += 32;
    }
    draw_scrollbar_indicator(area, 312, 2, 154, data.nodes.size(), visible_rows, first);
    add_actions(root, ScreenId::Nodes);
    load_screen(root);
}

void show_contacts() {
    lv_obj_t* root = make_root(ScreenId::Contacts);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    if (data.nodes.empty()) {
        add_row(area, 18, "No contacts", "use BLE companion or advert scan", "");
    } else {
        const auto& node = data.nodes[bounded_index(data.state.selected_node, data.nodes.size())];
        add_row(area, 0, node.name, node.short_id, std::to_string(node.rssi) + " dBm");
        add_row(area, 31, "Public key", "first 4 bytes", key_prefix(node.public_key));
        add_row(area, 62, "Path", "route to contact", path_text(node));
        add_row(area, 93, "Flags", "type " + std::to_string(node.contact_type), std::to_string(node.contact_flags));
        add_row(area, 124, "Last mod", "contact record", std::to_string(node.lastmod));
    }
    draw_scrollbar_indicator(area, 312, 2, 154, data.nodes.size(), 1, bounded_index(data.state.selected_node, data.nodes.size()));
    add_actions(root, ScreenId::Contacts);
    load_screen(root);
}

void show_channels() {
    show_chat(ScreenId::Channels);
}

void show_channel_editor() {
    lv_obj_t* root = make_root(ScreenId::ChannelEditor);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    if (data.channels.empty()) {
        add_row(area, 18, "No channels", "BLE companion can provision channels", "");
    } else {
        const auto& channel = data.channels[bounded_index(data.state.selected_channel, data.channels.size())];
        add_row(area, 0, "#" + channel.name, channel.active ? "active" : "inactive", std::to_string(channel.users) + " users");
        add_row(area, 31, "Secret", "channel encryption", secret_text(channel));
        add_row(area, 62, "Slot", "selected channel", std::to_string(data.state.selected_channel));
        add_row(area, 93, "Default", "current selected channel", data.state.channel);
        add_row(area, 124, "Compose", "target", data.state.compose_recipient == "broadcast" ? "broadcast" : "DM");
    }
    draw_scrollbar_indicator(area, 312, 2, 154, data.channels.size(), 1, bounded_index(data.state.selected_channel, data.channels.size()));
    add_actions(root, ScreenId::ChannelEditor);
    load_screen(root);
}

void show_map() {
    lv_obj_t* root = make_root(ScreenId::Map);
    lv_obj_t* area = content(root);
    const NodeInfo* selected = nullptr;
    if (!data.nodes.empty()) {
        selected = &data.nodes[bounded_index(data.state.selected_node, data.nodes.size())];
    }
    lv_obj_t* map = lv_obj_create(area);
    lv_obj_set_size(map, 300, 116);
    lv_obj_set_pos(map, 10, 8);
    set_panel(map, ce_window);
    lv_obj_set_style_border_color(map, lv_color_hex(ce_dark_shadow), 0);
    lv_obj_clear_flag(map, LV_OBJ_FLAG_SCROLLABLE);
    add_bevel(map, 0, 0, 300, 116, false);
    make_label(map, "Base", 20, 18, 80, 12);
    make_label(map, selected == nullptr ? "Node" : selected->name, 115, 58, 100, 12);
    make_label(map, "Relay", 220, 24, 70, 12);
    const double lat = (selected != nullptr && selected->has_position) ? selected->latitude : data.state.latitude;
    const double lon = (selected != nullptr && selected->has_position) ? selected->longitude : data.state.longitude;
    make_label(area, "z" + std::to_string(data.state.map_zoom) + "  " + fmt_float(lat, 4) + ", " + fmt_float(lon, 4), 10, 132, 300, 12);
    make_label(area, selected != nullptr && selected->has_position ? "selected node position" : "local GPS position", 10, 146, 300, 12);
    add_actions(root, ScreenId::Map);
    load_screen(root);
}

void show_radio_advanced() {
    lv_obj_t* root = make_root(ScreenId::RadioAdvanced);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 0, "Frequency", "numeric LoRa", std::to_string(data.state.radio_frequency_khz) + " kHz");
    add_row(area, 31, "Bandwidth", "modem setting", bandwidth_text(data.state.radio_bandwidth_hz));
    add_row(area, 62, "SF / CR", "spreading + coding", std::to_string(data.state.radio_spreading_factor) + " / " + std::to_string(data.state.radio_coding_rate));
    add_row(area, 93, "Repeat", "client repeat", bool_text(data.state.client_repeat));
    add_row(area, 124, "Tuning", "rx/airtime", std::to_string(data.state.rx_delay_base_ms) + "/" + std::to_string(data.state.airtime_factor_ms));
    add_actions(root, ScreenId::RadioAdvanced);
    load_screen(root);
}

void show_radio_tuning() {
    lv_obj_t* root = make_root(ScreenId::RadioTuning);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 0, "Path hash", "wire path bytes", std::to_string(data.state.path_hash_mode + 1));
    add_row(area, 31, "Client repeat", "repeat eligible packets", bool_text(data.state.client_repeat));
    add_row(area, 62, "RX delay", "base / airtime", std::to_string(data.state.rx_delay_base_ms) + "/" + std::to_string(data.state.airtime_factor_ms));
    add_row(area, 93, "CAD", data.state.radio_cad_status,
            std::to_string(data.state.radio_cad_detected_count) + "/" +
                std::to_string(data.state.radio_cad_error_count));
    add_row(area, 124, "Hardware", data.state.radio_dio2_as_rf_switch ? "DIO2 RF" : "RF fixed",
            std::to_string(data.state.radio_tcxo_mv) + "mV");
    add_actions(root, ScreenId::RadioTuning);
    load_screen(root);
}

void show_identity() {
    lv_obj_t* root = make_root(ScreenId::Identity);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 0, "Node", data.state.device_name, data.state.local_node_id);
    add_row(area, 31, "Public key", "first 4 bytes", key_prefix(data.state.public_key));
    add_row(area, 62, "Advert", "location policy", advert_policy_text());
    add_row(area, 93, "Location", fmt_float(data.state.latitude, 4), fmt_float(data.state.longitude, 4));
    add_row(area, 124, "Security", data.state.device_pin_set ? "PIN set" : "no PIN",
            data.state.private_key_export_enabled ? "export" : "locked");
    add_actions(root, ScreenId::Identity);
    load_screen(root);
}

void show_ble() {
    lv_obj_t* root = make_root(ScreenId::Ble);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 0, "Companion", data.state.ble_state, data.state.ble_connected ? "connected" : bool_text(data.state.ble_enabled));
    add_row(area, 31, "Frames", "rx / tx", std::to_string(data.state.ble_rx_frames) + " / " + std::to_string(data.state.ble_tx_frames));
    add_row(area, 62, "Last cmd", "BLE protocol", data.state.ble_last_command);
    add_row(area, 93, "Last error", "BLE protocol", data.state.ble_last_error);
    add_row(area, 124, "Messages", "waiting sync", std::to_string(data.state.unread_count));
    add_actions(root, ScreenId::Ble);
    load_screen(root);
}

void show_settings() {
    lv_obj_t* root = make_root(ScreenId::Settings);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 0, "Channel", "", data.state.channel);
    add_row(area, 31, "Node", data.state.device_name, data.state.local_node_id);
    add_row(area, 62, "GPS", data.state.gps_state, data.state.gps_enabled ? "on" : "off");
    add_row(area, 93, "Bluetooth", data.state.ble_state, data.state.ble_connected ? "link" : "adv");
    add_row(area, 124, "Storage", data.state.storage_state, data.state.storage_writable ? "write" : "read");
    add_actions(root, ScreenId::Settings);
    load_screen(root);
}

void show_radio() {
    lv_obj_t* root = make_root(ScreenId::Radio);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 0, "Region", data.state.region,
            std::to_string(data.state.radio_frequency_khz) + " kHz");
    add_row(area, 31, "State", data.state.radio_state, data.state.connected ? "online" : "offline");
    add_row(area, 62, "RX/TX",
            std::to_string(data.state.radio_rx_decoded_count) + "/" +
                std::to_string(data.state.radio_rx_raw_count) + " ok/raw",
            std::to_string(data.state.packet_tx_count) + " tx");
    add_row(area, 93, "Last RF",
            "type " + std::to_string(data.state.radio_last_packet_type) +
                " len " + std::to_string(data.state.radio_last_packet_len),
            data.state.radio_last_decode);
    add_row(area, 124, "Scan",
            data.state.radio_scan_status,
            std::to_string(data.state.radio_scan_raw_count) + "/" +
                std::to_string(data.state.radio_scan_decoded_count) + " raw/ok");
    add_actions(root, ScreenId::Radio);
    load_screen(root);
}

void show_servers() {
    lv_obj_t* root = make_root(ScreenId::Servers);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 10, "Room server", "stored posts/history", data.state.room_logged_in ? "login" : "off");
    add_row(area, 44, "Repeater", "remote admin over RF", data.state.repeater_admin ? "admin" : "locked");
    add_row(area, 78, "Clock sync", "server/device time", "ready");
    add_row(area, 112, "Register", "advanced T-Deck extras", data.state.registered ? "yes" : "no");
    add_actions(root, ScreenId::Servers);
    load_screen(root);
}

void show_tools() {
    lv_obj_t* root = make_root(ScreenId::Tools);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    add_row(area, 0, data.state.tool_title, data.state.tool_detail, data.state.tool_status);
    add_row(area, 31, "Path", "latest response", data.state.tool_path.empty() ? "none" : data.state.tool_path);
    add_row(area, 62, "Telemetry", "rx/tx queue", std::to_string(data.state.packet_rx_count) + "/" + std::to_string(data.state.packet_tx_count) + " q" + std::to_string(data.state.queue_len));
    add_row(area, 93, "Custom vars", "index/value", std::to_string(data.state.custom_var_index) + "/" + std::to_string(data.state.custom_var_value));
    add_row(area, 124, "Flood scope", "default/key", std::to_string(data.state.default_flood_scope) + "/" + std::to_string(data.state.flood_scope_key));
    add_actions(root, ScreenId::Tools);
    load_screen(root);
}

void show_diagnostics() {
    lv_obj_t* root = make_root(ScreenId::Diagnostics);
    lv_obj_t* area = content(root);
    disable_scroll(area);
    constexpr std::size_t visible_rows = 5;
    const std::size_t total_rows = 5 + data.logs.size();
    const std::size_t first = total_rows <= visible_rows
        ? 0
        : std::min<std::size_t>(bounded_index(data.state.diagnostics_scroll, total_rows), total_rows - visible_rows);
    for (std::size_t row = 0; row < visible_rows && first + row < total_rows; ++row) {
        const std::size_t item = first + row;
        const int y = static_cast<int>(row) * 32;
        if (item == 0) {
            add_row(area, y, "Radio", data.state.radio_state, data.state.connected ? "ok" : "off");
        } else if (item == 1) {
            add_row(area, y, "Battery", std::to_string(data.state.battery_mv) + " mV", std::to_string(data.state.battery_percent) + "%");
        } else if (item == 2) {
            add_row(area, y, "Memory", "heap " + fmt_bytes(data.state.heap_free_bytes),
                    data.state.psram_total_bytes > 0 ? fmt_bytes(data.state.psram_free_bytes) : "no psram");
        } else if (item == 3) {
            add_row(area, y, "Storage", std::to_string(data.state.persisted_message_count) + " msg "
                    + std::to_string(data.state.persisted_node_count) + " nodes",
                    data.state.storage_writable ? "ok" : "off");
        } else if (item == 4) {
            add_row(area, y, "Bluetooth", data.state.ble_state, data.state.ble_connected ? "connected" : "ready");
        } else {
            add_row(area, y, "Log", data.logs[item - 5], "");
        }
    }
    draw_scrollbar_indicator(area, 312, 2, 154, total_rows, visible_rows, first);
    add_actions(root, ScreenId::Diagnostics);
    load_screen(root);
}

bool process_pending_navigation() {
    if (!pending_navigation.active) {
        return false;
    }
    const PendingNavigation pending = pending_navigation;
    pending_navigation = {};

    if (pending.run_action) {
        const auto actions = screen_actions(pending.source);
        const auto& action = actions[static_cast<std::size_t>(pending.action_index)];
        app_handle_action(pending.source, action);
        ui_show(action.target);
        return true;
    }

    if (pending.start_app) {
        app_handle_action(ScreenId::Home, {"Start", pending.target, ActionCommand::StartApp});
    }
    if (pending.target != current_screen) {
        ui_show(pending.target);
    }
    return true;
}

}  // namespace

void ui_create(const AppSnapshot& snapshot) {
    app_set_snapshot(snapshot);
    remember_incoming_messages(snapshot);
    notification_visible = false;
    ui_show(ScreenId::Home);
}

ScreenId ui_current_screen() {
    return current_screen;
}

void ui_request_show(ScreenId screen) {
    if (screen == current_screen) {
        pending_navigation = {};
        return;
    }
    pending_navigation.active = true;
    pending_navigation.run_action = false;
    pending_navigation.start_app = false;
    pending_navigation.target = screen;
}

void ui_tick() {
    ui_tick(lv_tick_get());
}

void ui_tick(uint32_t now_ms) {
    ui_now_ms = now_ms;
    if (process_pending_navigation()) {
        return;
    }
    const bool was_visible = notification_visible;
    data = app_snapshot();
    update_notification_from_snapshot();
    if (!was_visible && notification_visible) {
        ui_show(current_screen);
        return;
    }
    if (!notification_visible) {
        hide_notification_overlay();
        return;
    }
    if (ui_now_ms - notification_started_ms >= 5000U) {
        if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
            std::fprintf(stderr, "notification hide\n");
        }
        hide_notification_overlay();
    }
}

void ui_show(ScreenId screen) {
    data = app_snapshot();
    update_notification_from_snapshot();
    current_screen = screen;
    app_set_active_screen(screen);
    switch (screen) {
        case ScreenId::Boot:
            show_boot();
            break;
        case ScreenId::Home:
            show_home();
            break;
        case ScreenId::Inbox:
            show_inbox();
            break;
        case ScreenId::MessageView:
            show_message();
            break;
        case ScreenId::Compose:
            show_compose();
            break;
        case ScreenId::Nodes:
            show_nodes();
            break;
        case ScreenId::Contacts:
            show_contacts();
            break;
        case ScreenId::Channels:
            show_channels();
            break;
        case ScreenId::ChannelEditor:
            show_channel_editor();
            break;
        case ScreenId::Map:
            show_map();
            break;
        case ScreenId::Settings:
            show_settings();
            break;
        case ScreenId::Radio:
            show_radio();
            break;
        case ScreenId::RadioAdvanced:
            show_radio_advanced();
            break;
        case ScreenId::RadioTuning:
            show_radio_tuning();
            break;
        case ScreenId::Identity:
            show_identity();
            break;
        case ScreenId::Ble:
            show_ble();
            break;
        case ScreenId::Servers:
            show_servers();
            break;
        case ScreenId::Tools:
            show_tools();
            break;
        case ScreenId::Diagnostics:
            show_diagnostics();
            break;
    }
}

}  // namespace meshcore
