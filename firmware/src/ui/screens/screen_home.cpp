#include <lvgl.h>
#include "app_config.h"
#include "screen_home.h"

static lv_obj_t* make_button(lv_obj_t* parent, const char* text) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 72, 46);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

void screen_home_create() {
    lv_obj_t* root = lv_obj_create(NULL);
    lv_obj_set_size(root, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* top = lv_obj_create(root);
    lv_obj_set_size(top, SCREEN_WIDTH, TOP_BAR_HEIGHT);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(top);
    lv_label_set_text(title, "MeshCore");
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 6, 0);

    lv_obj_t* status = lv_label_create(top);
    lv_label_set_text(status, "87%  10:24");
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -6, 0);

    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_size(content, SCREEN_WIDTH, CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, TOP_BAR_HEIGHT);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* main = lv_label_create(content);
    lv_label_set_text(main, "Connected\nChannel: test\nNodes: 5\nBattery: 87%\n\nRecent:\nAlpha-7: ETA 5 minutes.");
    lv_obj_align(main, LV_ALIGN_TOP_LEFT, 12, 10);

    lv_obj_t* bottom = lv_obj_create(root);
    lv_obj_set_size(bottom, SCREEN_WIDTH, BOTTOM_BAR_HEIGHT);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* msg = make_button(bottom, "Msg");
    lv_obj_align(msg, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* nodes = make_button(bottom, "Nodes");
    lv_obj_align(nodes, LV_ALIGN_LEFT_MID, 84, 0);

    lv_obj_t* map = make_button(bottom, "Map");
    lv_obj_align(map, LV_ALIGN_LEFT_MID, 164, 0);

    lv_obj_t* menu = make_button(bottom, "Menu");
    lv_obj_align(menu, LV_ALIGN_LEFT_MID, 244, 0);

    lv_screen_load(root);
}
