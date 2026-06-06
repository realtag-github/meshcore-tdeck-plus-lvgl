#include <SDL2/SDL.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lvgl.h>

#include "app/app_controller.h"
#include "app/mock_data.h"
#include "app/navigation.h"
#include "sim_service.h"
#include "ui/app_ui.h"

#if __has_include(<drivers/sdl/lv_sdl_window.h>)
#include <drivers/sdl/lv_sdl_keyboard.h>
#include <drivers/sdl/lv_sdl_mouse.h>
#include <drivers/sdl/lv_sdl_mousewheel.h>
#include <drivers/sdl/lv_sdl_window.h>
#elif __has_include(<lvgl/src/drivers/sdl/lv_sdl_window.h>)
#include <lvgl/src/drivers/sdl/lv_sdl_keyboard.h>
#include <lvgl/src/drivers/sdl/lv_sdl_mouse.h>
#include <lvgl/src/drivers/sdl/lv_sdl_mousewheel.h>
#include <lvgl/src/drivers/sdl/lv_sdl_window.h>
#else
#error "LVGL SDL driver headers were not found"
#endif

namespace {

struct PointerState {
    int x = 0;
    int y = 0;
    bool pressed = false;
};

PointerState pointer_state;
PointerState pending_touch;
bool has_pending_touch = false;
PointerState pending_scroll;
int pending_scroll_delta = 0;
bool has_pending_scroll = false;

::Display* x11_display() {
    static ::Display* display = XOpenDisplay(nullptr);
    return display;
}

void read_vnc_pointer(lv_indev_t*, lv_indev_data_t* input) {
    ::Display* display = x11_display();
    if (display == nullptr) {
        input->state = LV_INDEV_STATE_RELEASED;
        input->point.x = 0;
        input->point.y = 0;
        return;
    }

    Window root_return = 0;
    Window child_return = 0;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int mask = 0;
    XQueryPointer(display, DefaultRootWindow(display), &root_return, &child_return,
                  &root_x, &root_y, &win_x, &win_y, &mask);

    SDL_PumpEvents();
    int sdl_x = root_x;
    int sdl_y = root_y;
    const uint32_t sdl_buttons = SDL_GetMouseState(&sdl_x, &sdl_y);
    const bool x11_pressed = (mask & Button1Mask) != 0;
    const bool sdl_pressed = (sdl_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    const int pointer_x = sdl_pressed ? sdl_x : root_x;
    const int pointer_y = sdl_pressed ? sdl_y : root_y;

    input->point.x = std::clamp(pointer_x, 0, meshcore::screen_width - 1);
    input->point.y = std::clamp(pointer_y, 0, meshcore::screen_height - 1);
    input->state = (x11_pressed || sdl_pressed) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    pointer_state = {input->point.x, input->point.y, input->state == LV_INDEV_STATE_PRESSED};
    if ((mask & Button4Mask) != 0 || (mask & Button5Mask) != 0) {
        static uint32_t last_wheel_ms = 0;
        const uint32_t now = SDL_GetTicks();
        if (now - last_wheel_ms >= 120) {
            last_wheel_ms = now;
            pending_scroll = {input->point.x, input->point.y, false};
            pending_scroll_delta = (mask & Button5Mask) != 0 ? 1 : -1;
            has_pending_scroll = true;
        }
    }
    if (pointer_state.pressed) {
        SDL_Delay(1);
        pending_touch = pointer_state;
        has_pending_touch = true;
    }
    if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr && input->state == LV_INDEV_STATE_PRESSED) {
        std::fprintf(stderr, "pointer down %d,%d mask=%u\n", input->point.x, input->point.y, mask);
    }
}

bool in_rect(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

void route_action(meshcore::ScreenId screen, int action_index) {
    const auto actions = meshcore::screen_actions(screen);
    const auto& action = actions[static_cast<std::size_t>(action_index)];
    meshcore::app_handle_action(screen, action);
    meshcore::ui_show(action.target);
    lv_refr_now(nullptr);
}

void route_home_touch(int x, int y) {
    struct IconTarget {
        int x;
        int y;
        meshcore::ScreenId screen;
        meshcore::ActionCommand command = meshcore::ActionCommand::Navigate;
    };
    constexpr IconTarget icons[] = {
        {8, 8, meshcore::ScreenId::Inbox},
        {70, 8, meshcore::ScreenId::Contacts},
        {132, 8, meshcore::ScreenId::ChannelEditor},
        {194, 8, meshcore::ScreenId::Radio},
        {256, 8, meshcore::ScreenId::Map},
        {8, 80, meshcore::ScreenId::Nodes},
        {70, 80, meshcore::ScreenId::RadioAdvanced},
        {132, 80, meshcore::ScreenId::Identity},
        {194, 80, meshcore::ScreenId::Ble},
        {256, 80, meshcore::ScreenId::Settings},
        {8, 152, meshcore::ScreenId::Servers},
        {70, 152, meshcore::ScreenId::Tools},
        {132, 152, meshcore::ScreenId::Diagnostics},
    };

    for (const auto& icon : icons) {
        if (in_rect(x, y, icon.x, icon.y, 58, 58)) {
            if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
                std::fprintf(stderr, "route home icon -> %d\n", static_cast<int>(icon.screen));
            }
            if (icon.command == meshcore::ActionCommand::StartApp) {
                meshcore::app_handle_action(meshcore::ScreenId::Home, {"Start", icon.screen, icon.command});
            }
            meshcore::ui_show(icon.screen);
            lv_refr_now(nullptr);
            if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
                std::fprintf(stderr, "current screen after icon=%d\n",
                             static_cast<int>(meshcore::ui_current_screen()));
            }
            return;
        }
    }

    if (in_rect(x, y, 4, 215, 50, 22)) {
        meshcore::ui_show(meshcore::ScreenId::Inbox);
        lv_refr_now(nullptr);
    }
}

void route_window_touch(meshcore::ScreenId screen, int x, int y) {
    if (in_rect(x, y, 298, 3, 18, 18)) {
        meshcore::ui_show(meshcore::ScreenId::Home);
        lv_refr_now(nullptr);
        return;
    }
    if (in_rect(x, y, 4, 215, 50, 22)) {
        meshcore::ui_show(meshcore::ScreenId::Inbox);
        lv_refr_now(nullptr);
        return;
    }

    if (in_rect(x, y, 4, 27, 54, 22)) {
        route_action(screen, 0);
    } else if (in_rect(x, y, 62, 27, 54, 22)) {
        route_action(screen, 1);
    } else if (in_rect(x, y, 120, 27, 54, 22)) {
        route_action(screen, 2);
    } else if (in_rect(x, y, 178, 27, 54, 22)) {
        route_action(screen, 3);
    } else if ((screen == meshcore::ScreenId::Inbox || screen == meshcore::ScreenId::MessageView ||
                screen == meshcore::ScreenId::Compose || screen == meshcore::ScreenId::Channels) &&
               in_rect(x, y, 4, 70, 88, 114)) {
        const int roster_y = y - 70;
        if (roster_y >= 13 && roster_y < 61) {
            meshcore::app_select_chat_contact((roster_y - 13) / 16);
            meshcore::ui_show(meshcore::ScreenId::Inbox);
            lv_refr_now(nullptr);
        } else if (roster_y >= 74 && roster_y < 122) {
            meshcore::app_select_chat_channel((roster_y - 74) / 16);
            meshcore::ui_show(meshcore::ScreenId::Channels);
            lv_refr_now(nullptr);
        }
    }
}

int clamped_index(int value, int total) {
    if (total <= 0) {
        return 0;
    }
    return std::max(0, std::min(total - 1, value));
}

void route_scroll(meshcore::ScreenId screen, int x, int y, int delta) {
    if (delta == 0 || screen == meshcore::ScreenId::Home || screen == meshcore::ScreenId::Boot) {
        return;
    }

    const auto& snapshot = meshcore::app_snapshot();
    if ((screen == meshcore::ScreenId::Inbox || screen == meshcore::ScreenId::MessageView ||
         screen == meshcore::ScreenId::Compose || screen == meshcore::ScreenId::Channels) &&
        in_rect(x, y, 4, 70, 88, 114)) {
        const int roster_y = y - 70;
        if (roster_y >= 13 && roster_y < 61) {
            meshcore::app_select_chat_contact(clamped_index(snapshot.state.selected_node + delta,
                                                           static_cast<int>(snapshot.nodes.size())));
            meshcore::ui_show(meshcore::ScreenId::Inbox);
            lv_refr_now(nullptr);
            return;
        }
        if (roster_y >= 74 && roster_y < 122) {
            meshcore::app_select_chat_channel(clamped_index(snapshot.state.selected_channel + delta,
                                                           static_cast<int>(snapshot.channels.size())));
            meshcore::ui_show(meshcore::ScreenId::Channels);
            lv_refr_now(nullptr);
            return;
        }
    }

    if (screen == meshcore::ScreenId::Nodes || screen == meshcore::ScreenId::Contacts ||
        screen == meshcore::ScreenId::ChannelEditor || screen == meshcore::ScreenId::Diagnostics ||
        screen == meshcore::ScreenId::Inbox || screen == meshcore::ScreenId::MessageView ||
        screen == meshcore::ScreenId::Compose || screen == meshcore::ScreenId::Channels) {
        meshcore::app_scroll_selection(screen, delta);
        meshcore::ui_show(screen);
        lv_refr_now(nullptr);
    }
}

void route_sdl_input() {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT) {
            std::exit(0);
        }
        if (event.type == SDL_MOUSEWHEEL) {
            int x = pointer_state.x;
            int y = pointer_state.y;
            SDL_GetMouseState(&x, &y);
            const int delta = event.wheel.y < 0 ? 1 : -1;
            route_scroll(meshcore::ui_current_screen(), x, y, delta);
        } else if (event.type == SDL_KEYDOWN) {
            const auto key = event.key.keysym.sym;
            if (meshcore::app_edit_active()) {
                char ch = 0;
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    ch = '\n';
                } else if (key == SDLK_BACKSPACE) {
                    ch = 8;
                } else if (key == SDLK_ESCAPE) {
                    ch = 27;
                } else if (key >= 32 && key <= 126) {
                    ch = static_cast<char>(key);
                }
                if (ch != 0 && meshcore::app_handle_key(ch)) {
                    meshcore::ui_show(meshcore::ui_current_screen());
                    lv_refr_now(nullptr);
                }
            } else if (key == SDLK_DOWN || key == SDLK_RIGHT) {
                route_scroll(meshcore::ui_current_screen(), pointer_state.x, pointer_state.y, 1);
            } else if (key == SDLK_UP || key == SDLK_LEFT) {
                route_scroll(meshcore::ui_current_screen(), pointer_state.x, pointer_state.y, -1);
            }
        }
    }
}

void route_vnc_scroll() {
    if (!has_pending_scroll) {
        return;
    }
    const PointerState scroll = pending_scroll;
    const int delta = pending_scroll_delta;
    has_pending_scroll = false;
    pending_scroll_delta = 0;
    route_scroll(meshcore::ui_current_screen(), scroll.x, scroll.y, delta);
}

void route_vnc_touch() {
    static uint32_t last_route_ms = 0;
    if (!has_pending_touch) {
        return;
    }
    const uint32_t now = SDL_GetTicks();
    if (now - last_route_ms < 250) {
        return;
    }
    const PointerState touch = pending_touch;
    has_pending_touch = false;
    last_route_ms = now;

    const meshcore::ScreenId screen = meshcore::ui_current_screen();
    if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
        std::fprintf(stderr, "route touch %d,%d screen=%d\n", touch.x, touch.y, static_cast<int>(screen));
    }
    if (screen == meshcore::ScreenId::Home) {
        route_home_touch(touch.x, touch.y);
    } else {
        route_window_touch(screen, touch.x, touch.y);
    }
}

meshcore::ScreenId screen_from_name(const char* name) {
    if (name == nullptr) {
        return meshcore::ScreenId::Home;
    }
    for (const auto screen : meshcore::all_screens()) {
        if (std::strcmp(name, meshcore::screen_title(screen)) == 0) {
            return screen;
        }
    }
    return meshcore::ScreenId::Home;
}

}  // namespace

int main() {
    SimService sim;
    lv_init();
    lv_display_t* display = lv_sdl_window_create(meshcore::screen_width, meshcore::screen_height);
    lv_indev_t* mouse = lv_indev_create();
    lv_indev_set_type(mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse, read_vnc_pointer);
    lv_indev_t* mousewheel = lv_sdl_mousewheel_create();
    lv_indev_t* keyboard = lv_sdl_keyboard_create();
    lv_indev_set_display(mouse, display);
    lv_indev_set_display(mousewheel, display);
    lv_indev_set_display(keyboard, display);

    sim.begin();
    meshcore::ui_create(meshcore::app_snapshot());
    if (const char* initial_screen = std::getenv("MESHCORE_SIM_INITIAL_SCREEN")) {
        meshcore::ui_show(screen_from_name(initial_screen));
        lv_refr_now(nullptr);
    }
    unsigned last_rendered_version = meshcore::app_snapshot_version();
    while (true) {
        const uint32_t now_ms = SDL_GetTicks();
        route_sdl_input();
        sim.loop(now_ms);
        const unsigned version = meshcore::app_snapshot_version();
        if (version != last_rendered_version) {
            last_rendered_version = version;
            meshcore::ui_show(meshcore::ui_current_screen());
        }
        lv_indev_data_t direct_input{};
        read_vnc_pointer(mouse, &direct_input);
        route_vnc_touch();
        route_vnc_scroll();
        lv_indev_read(mouse);
        meshcore::ui_tick(SDL_GetTicks());
        const uint32_t wait_ms = lv_timer_handler();
        SDL_Delay(wait_ms > 0 ? std::min<uint32_t>(wait_ms, 5) : 5);
    }
    return 0;
}
