#pragma once

#include <array>

#include "app_types.h"

namespace meshcore {

using ActionSet = std::array<Action, 4>;
using ScreenSet = std::array<ScreenId, 19>;

inline ScreenSet all_screens() {
    return {
        ScreenId::Boot,
        ScreenId::Home,
        ScreenId::Inbox,
        ScreenId::MessageView,
        ScreenId::Compose,
        ScreenId::Nodes,
        ScreenId::Contacts,
        ScreenId::Channels,
        ScreenId::ChannelEditor,
        ScreenId::Map,
        ScreenId::Settings,
        ScreenId::Radio,
        ScreenId::RadioAdvanced,
        ScreenId::RadioTuning,
        ScreenId::Identity,
        ScreenId::Ble,
        ScreenId::Servers,
        ScreenId::Tools,
        ScreenId::Diagnostics,
    };
}

inline ActionSet screen_actions(ScreenId screen) {
    switch (screen) {
        case ScreenId::Boot:
            return {{{"Start", ScreenId::Home, ActionCommand::StartApp}, {"Nodes", ScreenId::Nodes}, {"Map", ScreenId::Map}, {"Diag", ScreenId::Diagnostics}}};
        case ScreenId::Home:
            return {{{"Msgs", ScreenId::Inbox}, {"Nodes", ScreenId::Nodes}, {"Map", ScreenId::Map}, {"Menu", ScreenId::Settings}}};
        case ScreenId::Inbox:
            return {{{"DM", ScreenId::Inbox, ActionCommand::CycleRecipient}, {"Chan", ScreenId::Channels, ActionCommand::NextChannel}, {"Send", ScreenId::Inbox, ActionCommand::SendMessage}, {"Edit", ScreenId::Inbox, ActionCommand::EditCompose}}};
        case ScreenId::MessageView:
            return {{{"Reply", ScreenId::MessageView, ActionCommand::ReplyMessage}, {"Del", ScreenId::MessageView, ActionCommand::DeleteMessage}, {"Send", ScreenId::MessageView, ActionCommand::SendMessage}, {"Edit", ScreenId::MessageView, ActionCommand::EditCompose}}};
        case ScreenId::Compose:
            return {{{"DM", ScreenId::Inbox, ActionCommand::CycleRecipient}, {"Chan", ScreenId::Channels, ActionCommand::NextChannel}, {"Send", ScreenId::Compose, ActionCommand::SendMessage}, {"Edit", ScreenId::Compose, ActionCommand::EditCompose}}};
        case ScreenId::Nodes:
            return {{{"Scan", ScreenId::Nodes, ActionCommand::ScanNodes}, {"Ping", ScreenId::Nodes, ActionCommand::PingNode}, {"Next", ScreenId::Nodes, ActionCommand::NextNode}, {"Info", ScreenId::Contacts}}};
        case ScreenId::Contacts:
            return {{{"Next", ScreenId::Contacts, ActionCommand::NextNode}, {"Path", ScreenId::Contacts, ActionCommand::ResetContactPath}, {"Share", ScreenId::Contacts, ActionCommand::ShareContact}, {"Del", ScreenId::Contacts, ActionCommand::RemoveContact}}};
        case ScreenId::Channels:
            return {{{"DM", ScreenId::Inbox, ActionCommand::CycleRecipient}, {"Chan", ScreenId::Channels, ActionCommand::NextChannel}, {"Send", ScreenId::Channels, ActionCommand::SendMessage}, {"Edit", ScreenId::Channels, ActionCommand::EditCompose}}};
        case ScreenId::ChannelEditor:
            return {{{"Next", ScreenId::ChannelEditor, ActionCommand::NextChannel}, {"Name", ScreenId::ChannelEditor, ActionCommand::EditChannelName}, {"Secret", ScreenId::ChannelEditor, ActionCommand::EditChannelSecret}, {"Join", ScreenId::ChannelEditor, ActionCommand::JoinChannel}}};
        case ScreenId::Map:
            return {{{"Center", ScreenId::Map, ActionCommand::MapCenter}, {"Lat", ScreenId::Map, ActionCommand::EditLatitude}, {"Lon", ScreenId::Map, ActionCommand::EditLongitude}, {"Home", ScreenId::Home}}};
        case ScreenId::Settings:
            return {{{"Home", ScreenId::Home}, {"Ident", ScreenId::Identity}, {"BLE", ScreenId::Ble}, {"GPS", ScreenId::Settings, ActionCommand::ToggleGps}}};
        case ScreenId::Radio:
            return {{{"Scan", ScreenId::Radio, ActionCommand::ScanRadio}, {"Adv", ScreenId::RadioAdvanced}, {"Region", ScreenId::Radio, ActionCommand::ToggleRegion}, {"Power", ScreenId::Radio, ActionCommand::CycleTxPower}}};
        case ScreenId::RadioAdvanced:
            return {{{"Freq", ScreenId::RadioAdvanced, ActionCommand::EditFrequency}, {"BW", ScreenId::RadioAdvanced, ActionCommand::EditBandwidth}, {"SF", ScreenId::RadioAdvanced, ActionCommand::EditSpreadingFactor}, {"Tune", ScreenId::RadioTuning}}};
        case ScreenId::RadioTuning:
            return {{{"Path", ScreenId::RadioTuning, ActionCommand::CyclePathHash}, {"Rpt", ScreenId::RadioTuning, ActionCommand::ToggleClientRepeat}, {"Tune", ScreenId::RadioTuning, ActionCommand::CycleTuning}, {"CAD", ScreenId::RadioTuning, ActionCommand::RunCadScan}}};
        case ScreenId::Identity:
            return {{{"Name", ScreenId::Identity, ActionCommand::EditDeviceName}, {"Adv", ScreenId::Identity, ActionCommand::SendSelfAdvert}, {"Key", ScreenId::Identity, ActionCommand::ExportPrivateKey}, {"PIN", ScreenId::Identity, ActionCommand::EditDevicePin}}};
        case ScreenId::Ble:
            return {{{"BLE", ScreenId::Ble, ActionCommand::ToggleBle}, {"Stats", ScreenId::Diagnostics}, {"Msgs", ScreenId::Inbox}, {"Home", ScreenId::Home}}};
        case ScreenId::Servers:
            return {{{"Back", ScreenId::Settings}, {"Room", ScreenId::Servers, ActionCommand::RoomLogin}, {"Admin", ScreenId::Servers, ActionCommand::RemoteAdmin}, {"Clock", ScreenId::Servers, ActionCommand::SyncClock}}};
        case ScreenId::Tools:
            return {{{"Trace", ScreenId::Tools, ActionCommand::TracePath}, {"Path", ScreenId::Tools, ActionCommand::DiscoverPath}, {"Telem", ScreenId::Tools, ActionCommand::SendTelemetry}, {"Var", ScreenId::Tools, ActionCommand::EditCustomVar}}};
        case ScreenId::Diagnostics:
            return {{{"Back", ScreenId::Settings}, {"Logs", ScreenId::Diagnostics, ActionCommand::AddLog}, {"Radio", ScreenId::Radio}, {"Tools", ScreenId::Tools}}};
    }
    return {{{"Home", ScreenId::Home}, {"Home", ScreenId::Home}, {"Home", ScreenId::Home}, {"Home", ScreenId::Home}}};
}

inline bool is_known_screen(ScreenId screen) {
    for (const auto known : all_screens()) {
        if (known == screen) {
            return true;
        }
    }
    return false;
}

}  // namespace meshcore
