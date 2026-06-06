#pragma once

#include "app_types.h"

namespace meshcore {

inline AppSnapshot make_mock_snapshot() {
    AppSnapshot snapshot;
    snapshot.state.device_name = "T-Deck Plus";
    snapshot.state.local_node_id = "0xA71B";
    snapshot.state.channel = "test";
    snapshot.state.selected_channel = 1;
    snapshot.state.region = "915 MHz";
    snapshot.state.connected = true;
    snapshot.state.battery_percent = 87;
    snapshot.state.battery_mv = 3950;
    snapshot.state.node_count = 7;
    snapshot.state.latitude = 37.7749;
    snapshot.state.longitude = -122.4194;
    snapshot.state.compose_recipient = "0xA71B";
    snapshot.state.last_event = "NodeSeen Alpha-7";
    snapshot.state.firmware_version = "dev-sim";
    snapshot.state.uptime_seconds = 42;
    snapshot.state.current_epoch_seconds = 13U * 60U * 60U + 37U * 60U;
    snapshot.state.heap_free_bytes = 196608;
    snapshot.state.psram_free_bytes = 7340032;
    snapshot.state.psram_total_bytes = 8388608;
    snapshot.state.persisted_message_count = 18;
    snapshot.state.persisted_node_count = 7;
    snapshot.state.ble_enabled = true;
    snapshot.state.ble_connected = false;
    snapshot.state.ble_state = "advertising";
    snapshot.state.ble_rx_frames = 42;
    snapshot.state.ble_tx_frames = 39;
    snapshot.state.ble_last_command = "GET_CONTACTS";
    snapshot.state.ble_last_error = "none";
    snapshot.state.packet_rx_count = 128;
    snapshot.state.packet_tx_count = 27;
    snapshot.state.radio_rx_raw_count = 134;
    snapshot.state.radio_rx_decoded_count = 128;
    snapshot.state.radio_rx_decode_fail_count = 6;
    snapshot.state.radio_tx_fail_count = 1;
    snapshot.state.radio_last_packet_len = 72;
    snapshot.state.radio_last_packet_type = 5;
    snapshot.state.radio_last_decode = "channel test";
    snapshot.state.radio_dio2_as_rf_switch = false;
    snapshot.state.radio_tcxo_mv = 1800;
    snapshot.state.radio_scan_status = "idle";
    snapshot.state.radio_scan_count = 14;
    snapshot.state.radio_cad_detected_count = 0;
    snapshot.state.radio_cad_error_count = 0;
    snapshot.state.radio_cad_status = "idle";
    snapshot.state.queue_len = 2;
    snapshot.state.noise_floor = -112;
    snapshot.state.last_rssi = -67;
    snapshot.state.last_snr_quarters = 28;
    snapshot.state.default_flood_scope = 1;
    snapshot.state.flood_scope_key = 7;
    snapshot.state.custom_var_index = 2;
    snapshot.state.custom_var_value = 15;
    snapshot.state.tool_title = "Path discovery";
    snapshot.state.tool_status = "last ok";
    snapshot.state.tool_detail = "Relay route from last request";
    snapshot.state.tool_path = "Alpha-7: direct";
    snapshot.state.public_key[0] = 0xa7;
    snapshot.state.public_key[1] = 0x1b;
    snapshot.state.public_key[2] = 0x42;
    snapshot.state.public_key[3] = 0x10;
    snapshot.nodes = {
        {"Alpha-7", "0xA71B", -67, 7.2f, 1, true, 37.7755, -122.4190},
        {"Field Ops", "0xF002", -71, 6.1f, 2, true, 37.7741, -122.4184},
        {"Bravo Team", "0xBEEF", -65, 8.3f, 1, false, 0.0, 0.0},
        {"Relay West", "0x5512", -82, 4.4f, 9, true, 37.7762, -122.4201},
        {"Base", "0xB453", -59, 9.1f, 0, true, 37.7749, -122.4194},
        {"Gate North", "0x6A7E", -73, 5.8f, 4, true, 37.7758, -122.4189},
        {"Supply Van", "0x5A11", -88, 3.2f, 14, false, 0.0, 0.0},
    };
    for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
        snapshot.nodes[i].public_key[0] = static_cast<unsigned char>(0xa0 + i);
        snapshot.nodes[i].public_key[1] = static_cast<unsigned char>(0x10 + i);
        snapshot.nodes[i].public_key[2] = static_cast<unsigned char>(0x20 + i);
        snapshot.nodes[i].public_key[3] = static_cast<unsigned char>(0x30 + i);
        snapshot.nodes[i].out_path_len = i % 2 == 0 ? 0 : 2;
        snapshot.nodes[i].contact_flags = static_cast<uint8_t>(i);
        snapshot.nodes[i].lastmod = 1000 + static_cast<unsigned>(i);
    }
    snapshot.messages = {
        {"1", "Alpha-7", "ETA Update", "ETA 5 minutes.", 0, false},
        {"2", "me", "Direct to 0xA71B", "Copy. Holding position near the north gate.", 0, true},
        {"3", "Alpha-7", "ETA Update", "Traffic is slow by the loading dock.", 0, false},
        {"4", "me", "Direct to 0xA71B", "Use the east service road if it is clear.", 0, true},
        {"5", "Alpha-7", "ETA Update", "East road is open. Switching route now.", 0, false},
        {"6", "Alpha-7", "Status", "Signal is stable after the relay hop.", 0, false},
        {"7", "me", "Direct to 0xA71B", "Send location when you reach the staging point.", 0, true},
        {"8", "Alpha-7", "Position", "At staging point. Awaiting next tasking.", 0, false},
        {"9", "Field Ops", "Channel test", "Test channel check-in received.", 0, false},
        {"10", "Base", "Channel test", "Base station has the updated route table.", 0, false},
        {"11", "me", "Channel test", "T-Deck Plus online on #test.", 0, true},
        {"12", "Relay West", "Channel test", "Repeater path looks healthy.", 0, false},
        {"13", "Gate North", "Channel test", "North gate is clear.", 0, false},
        {"14", "Supply Van", "Channel test", "Supply van is delayed by ten minutes.", 0, false},
        {"15", "Bravo Team", "Channel test", "Bravo is moving to the rally point.", 0, false},
        {"16", "Field Ops", "Channel test", "Field Ops acknowledges all updates.", 0, false},
        {"17", "Field Ops", "Re: Position", "Got it. Moving to waypoint.", 0, false},
        {"18", "Bravo Team", "Rally Point", "See you at the rally point.", 0, false},
    };
    for (std::size_t i = 0; i < snapshot.messages.size(); ++i) {
        snapshot.messages[i].persisted = true;
        snapshot.messages[i].status = snapshot.messages[i].outgoing ? "queued" : "received";
        if (snapshot.messages[i].outgoing && i < 7) {
            snapshot.messages[i].delivered = true;
            snapshot.messages[i].acked = i % 2 == 0;
            snapshot.messages[i].status = snapshot.messages[i].acked ? "acked" : "sent";
        }
    }
    snapshot.channels = {
        {"public", false, 5},
        {"test", true, 3},
        {"Ops", false, 3},
        {"Emergency", false, 1},
        {"Local", false, 8},
        {"Relay", false, 2},
    };
    snapshot.channels[1].secret = {0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
                                   0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f};
    snapshot.channels[2].secret[0] = 0x22;
    snapshot.channels[3].secret[0] = 0x33;
    snapshot.channels[4].secret[0] = 0x44;
    snapshot.logs = {
        "boot: ui initialized",
        "radio: SX1262 mock ready",
        "mesh: advert from Alpha-7",
        "gps: disabled by default",
        "ble: companion service advertising",
        "storage: mock persistence writable",
    };
    return snapshot;
}

}  // namespace meshcore
