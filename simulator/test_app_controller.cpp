#include <iostream>

#include "app/app_controller.h"
#include "app/mock_data.h"
#include "app/navigation.h"

namespace {

int sink_calls = 0;
meshcore::ActionCommand last_command = meshcore::ActionCommand::Navigate;

bool test_sink(meshcore::ActionCommand command,
               const meshcore::AppSnapshot& before,
               meshcore::AppSnapshot& after) {
    ++sink_calls;
    last_command = command;
    if (command == meshcore::ActionCommand::SendMessage && before.state.compose_text.empty()) {
        after.state.radio_state = "tx failed";
    }
    return true;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

meshcore::Action find_action(meshcore::ScreenId screen, meshcore::ActionCommand command) {
    for (const auto& action : meshcore::screen_actions(screen)) {
        if (action.command == command) {
            return action;
        }
    }
    return {};
}

}  // namespace

int main() {
    bool ok = true;
    meshcore::app_set_snapshot(meshcore::make_mock_snapshot());
    meshcore::app_set_action_sink(test_sink);
    meshcore::app_set_active_screen(meshcore::ScreenId::Nodes);
    ok &= expect(meshcore::app_active_screen() == meshcore::ScreenId::Nodes, "active screen was not retained");

    const auto initial_count = meshcore::app_snapshot().messages.size();
    meshcore::app_handle_action(meshcore::ScreenId::Compose,
                                find_action(meshcore::ScreenId::Compose, meshcore::ActionCommand::SendMessage));
    ok &= expect(meshcore::app_snapshot().messages.size() == initial_count + 1, "send did not add outgoing message");
    ok &= expect(meshcore::app_snapshot().messages.front().outgoing, "sent message is not marked outgoing");
    ok &= expect(last_command == meshcore::ActionCommand::SendMessage, "send did not notify action sink");

    meshcore::app_handle_action(meshcore::ScreenId::Inbox,
                                find_action(meshcore::ScreenId::Inbox, meshcore::ActionCommand::CycleRecipient));
    ok &= expect(meshcore::app_snapshot().state.compose_recipient == "0xF002", "DM selection did not move to next contact");
    meshcore::app_select_chat_contact(0);
    ok &= expect(meshcore::app_snapshot().state.compose_recipient == "0xA71B", "direct chat selection did not target contact");
    meshcore::app_select_chat_channel(1);
    ok &= expect(meshcore::app_snapshot().state.compose_recipient == "broadcast", "channel chat selection did not target broadcast");
    meshcore::app_scroll_selection(meshcore::ScreenId::Channels, 1);
    ok &= expect(meshcore::app_snapshot().state.selected_message == 9, "channel scroll did not select next #test message");
    meshcore::app_scroll_selection(meshcore::ScreenId::Nodes, 1);
    ok &= expect(meshcore::app_snapshot().state.selected_node == 1, "node scroll did not select next node");
    meshcore::app_scroll_selection(meshcore::ScreenId::Diagnostics, 1);
    ok &= expect(meshcore::app_snapshot().state.diagnostics_scroll == 1, "diagnostics scroll did not move");

    auto reply_snapshot = meshcore::app_snapshot();
    reply_snapshot.messages.insert(reply_snapshot.messages.begin(),
                                   {"new-dm", "Field Ops", "Direct", "Reply from field.", 123, false});
    meshcore::app_ingest_service_snapshot(reply_snapshot);
    ok &= expect(meshcore::app_snapshot().state.compose_recipient == "0xF002", "incoming direct reply did not select sender conversation");

    auto channel_snapshot = meshcore::app_snapshot();
    channel_snapshot.messages.insert(channel_snapshot.messages.begin(),
                                     {"new-channel", "Bravo Team", "Channel Ops", "Ops reply.", 124, false});
    meshcore::app_ingest_service_snapshot(channel_snapshot);
    ok &= expect(meshcore::app_snapshot().state.compose_recipient == "broadcast", "incoming channel reply did not select channel mode");
    ok &= expect(meshcore::app_snapshot().state.selected_channel == 2, "incoming channel reply did not select the channel");

    const auto before_region = meshcore::app_snapshot().state.region;
    meshcore::app_handle_action(meshcore::ScreenId::Radio,
                                find_action(meshcore::ScreenId::Radio, meshcore::ActionCommand::ToggleRegion));
    ok &= expect(meshcore::app_snapshot().state.region != before_region, "region toggle did not change region");

    meshcore::app_handle_action(meshcore::ScreenId::Radio,
                                find_action(meshcore::ScreenId::Radio, meshcore::ActionCommand::ScanRadio));
    ok &= expect(meshcore::app_snapshot().state.radio_scan_active, "radio scan action did not mark scan active");
    ok &= expect(meshcore::app_snapshot().state.radio_scan_status == "starting", "radio scan action did not set starting status");

    meshcore::app_handle_action(meshcore::ScreenId::RadioAdvanced,
                                find_action(meshcore::ScreenId::RadioAdvanced, meshcore::ActionCommand::EditFrequency));
    ok &= expect(meshcore::app_edit_active(), "frequency edit did not open editor");
    meshcore::app_handle_key(8);
    meshcore::app_handle_key('5');
    meshcore::app_handle_key('\n');
    ok &= expect(!meshcore::app_edit_active(), "frequency edit did not close after enter");
    ok &= expect(meshcore::app_snapshot().state.last_applied_edit == meshcore::EditField::RadioFrequency,
                 "frequency edit was not applied");

    meshcore::app_handle_action(meshcore::ScreenId::Compose,
                                find_action(meshcore::ScreenId::Compose, meshcore::ActionCommand::EditCompose));
    ok &= expect(meshcore::app_edit_active(), "compose edit did not open editor");
    meshcore::app_handle_key('!');
    meshcore::app_handle_key('\n');
    ok &= expect(!meshcore::app_snapshot().state.compose_text.empty(), "compose edit cleared text unexpectedly");

    meshcore::app_select_chat_channel(0);
    meshcore::app_handle_action(meshcore::ScreenId::Channels,
                                find_action(meshcore::ScreenId::Channels, meshcore::ActionCommand::NextChannel));
    ok &= expect(meshcore::app_snapshot().state.compose_recipient == "broadcast", "channel selection did not target broadcast");
    meshcore::app_handle_action(meshcore::ScreenId::Channels,
                                {"Join", meshcore::ScreenId::Channels, meshcore::ActionCommand::JoinChannel});
    ok &= expect(meshcore::app_snapshot().state.channel == "test", "channel join did not activate selected channel");
    ok &= expect(meshcore::app_snapshot().state.heap_free_bytes > 0, "runtime heap diagnostic was not populated");
    ok &= expect(meshcore::app_snapshot().state.persisted_message_count > 0, "message persistence diagnostic was not populated");
    ok &= expect(meshcore::app_snapshot().state.persisted_node_count > 0, "node persistence diagnostic was not populated");

    ok &= expect(sink_calls >= 4, "action sink was not called for controller commands");
    if (!ok) {
        return 1;
    }

    std::cout << "App controller command test passed\n";
    return 0;
}
