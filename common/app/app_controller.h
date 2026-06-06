#pragma once

#include <string>

#include "app_types.h"

namespace meshcore {

using AppActionSink = bool (*)(ActionCommand command, const AppSnapshot& before, AppSnapshot& after);

void app_set_snapshot(const AppSnapshot& snapshot);
const AppSnapshot& app_snapshot();
unsigned app_snapshot_version();
void app_set_active_screen(ScreenId screen);
ScreenId app_active_screen();
void app_set_action_sink(AppActionSink sink);
void app_handle_action(ScreenId source, const Action& action);
void app_set_compose_text(const std::string& text);
bool app_handle_key(char key);
bool app_edit_active();
void app_select_chat_contact(int index);
void app_select_chat_channel(int index);
void app_scroll_selection(ScreenId screen, int delta);
void app_ingest_service_snapshot(const AppSnapshot& snapshot);

}  // namespace meshcore
