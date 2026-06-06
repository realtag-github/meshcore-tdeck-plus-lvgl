#include "app_controller.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace meshcore {
namespace {

AppSnapshot state;
unsigned next_message_id = 100;
unsigned state_version = 0;
AppActionSink action_sink = nullptr;
ScreenId active_screen = ScreenId::Boot;

template <typename T>
int clamp_index(int index, const std::vector<T>& values) {
    if (values.empty()) {
        return 0;
    }
    if (index < 0) {
        return 0;
    }
    const int max_index = static_cast<int>(values.size()) - 1;
    return std::min(index, max_index);
}

void log_event(const std::string& value) {
    state.state.last_event = value;
    state.logs.insert(state.logs.begin(), value);
    if (state.logs.size() > 8) {
        state.logs.pop_back();
    }
}

void apply_region_defaults(AppState& target) {
    if (target.region == "433 MHz") {
        target.radio_frequency_khz = 433175;
    } else if (target.region == "868 MHz") {
        target.radio_frequency_khz = 868125;
    } else {
        target.region = "915 MHz";
        target.radio_frequency_khz = 910525;
        target.radio_bandwidth_hz = 62500;
        target.radio_spreading_factor = 7;
        target.radio_coding_rate = 5;
        return;
    }
    target.radio_bandwidth_hz = 250000;
    target.radio_spreading_factor = 10;
    target.radio_coding_rate = 5;
}

void mark_changed() {
    ++state_version;
}

bool same_nodes(const std::vector<NodeInfo>& left, const std::vector<NodeInfo>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].name != right[i].name ||
            left[i].short_id != right[i].short_id ||
            left[i].rssi != right[i].rssi ||
            left[i].snr != right[i].snr ||
            left[i].last_seen_seconds != right[i].last_seen_seconds ||
            left[i].has_position != right[i].has_position ||
            left[i].latitude != right[i].latitude ||
            left[i].longitude != right[i].longitude ||
            left[i].public_key != right[i].public_key ||
            left[i].out_path != right[i].out_path ||
            left[i].out_path_len != right[i].out_path_len ||
            left[i].contact_type != right[i].contact_type ||
            left[i].contact_flags != right[i].contact_flags ||
            left[i].lastmod != right[i].lastmod) {
            return false;
        }
    }
    return true;
}

bool same_messages(const std::vector<MeshMessage>& left, const std::vector<MeshMessage>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].id != right[i].id ||
            left[i].sender != right[i].sender ||
            left[i].subject != right[i].subject ||
            left[i].body != right[i].body ||
            left[i].timestamp != right[i].timestamp ||
            left[i].outgoing != right[i].outgoing ||
            left[i].delivered != right[i].delivered ||
            left[i].acked != right[i].acked ||
            left[i].persisted != right[i].persisted ||
            left[i].status != right[i].status) {
            return false;
        }
    }
    return true;
}

bool same_channels(const std::vector<ChannelInfo>& left, const std::vector<ChannelInfo>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].name != right[i].name ||
            left[i].active != right[i].active ||
            left[i].users != right[i].users ||
            left[i].secret != right[i].secret) {
            return false;
        }
    }
    return true;
}

bool app_state_needs_render(const AppState& left, const AppState& right) {
    if (left.device_name != right.device_name ||
        left.local_node_id != right.local_node_id ||
        left.channel != right.channel ||
        left.region != right.region ||
        left.radio_frequency_khz != right.radio_frequency_khz ||
        left.radio_bandwidth_hz != right.radio_bandwidth_hz ||
        left.radio_spreading_factor != right.radio_spreading_factor ||
        left.radio_coding_rate != right.radio_coding_rate ||
        left.client_repeat != right.client_repeat ||
        left.manual_add_contacts != right.manual_add_contacts ||
        left.advert_location_policy != right.advert_location_policy ||
        left.multi_acks != right.multi_acks ||
        left.rx_delay_base_ms != right.rx_delay_base_ms ||
        left.airtime_factor_ms != right.airtime_factor_ms ||
        left.autoadd_config != right.autoadd_config ||
        left.autoadd_max_hops != right.autoadd_max_hops ||
        left.default_flood_scope != right.default_flood_scope ||
        left.flood_scope_key != right.flood_scope_key ||
        left.default_flood_name != right.default_flood_name ||
        left.default_flood_secret != right.default_flood_secret ||
        left.custom_var_index != right.custom_var_index ||
        left.custom_var_value != right.custom_var_value ||
        left.device_pin_set != right.device_pin_set ||
        left.private_key_export_enabled != right.private_key_export_enabled ||
        left.identity_import_pending != right.identity_import_pending ||
        left.connected != right.connected ||
        left.battery_percent != right.battery_percent ||
        (left.current_epoch_seconds / 60U) != (right.current_epoch_seconds / 60U) ||
        left.node_count != right.node_count ||
        left.latitude != right.latitude ||
        left.longitude != right.longitude ||
        left.unread_count != right.unread_count ||
        left.selected_message != right.selected_message ||
        left.selected_node != right.selected_node ||
        left.selected_channel != right.selected_channel ||
        left.diagnostics_scroll != right.diagnostics_scroll ||
        left.map_zoom != right.map_zoom ||
        left.tx_power_dbm != right.tx_power_dbm ||
        left.path_hash_mode != right.path_hash_mode ||
        left.brightness_percent != right.brightness_percent ||
        left.gps_enabled != right.gps_enabled ||
        left.audio_enabled != right.audio_enabled ||
        left.sd_mounted != right.sd_mounted ||
        left.storage_writable != right.storage_writable ||
        left.room_logged_in != right.room_logged_in ||
        left.repeater_admin != right.repeater_admin ||
        left.registered != right.registered ||
        left.ble_enabled != right.ble_enabled ||
        left.ble_connected != right.ble_connected ||
        left.ble_rx_frames != right.ble_rx_frames ||
        left.ble_tx_frames != right.ble_tx_frames ||
        left.ble_last_command != right.ble_last_command ||
        left.ble_last_error != right.ble_last_error ||
        left.persisted_message_count != right.persisted_message_count ||
        left.persisted_node_count != right.persisted_node_count ||
        left.packet_rx_count != right.packet_rx_count ||
        left.packet_tx_count != right.packet_tx_count ||
        left.radio_rx_raw_count != right.radio_rx_raw_count ||
        left.radio_rx_decoded_count != right.radio_rx_decoded_count ||
        left.radio_rx_decode_fail_count != right.radio_rx_decode_fail_count ||
        left.radio_tx_fail_count != right.radio_tx_fail_count ||
        left.radio_last_packet_len != right.radio_last_packet_len ||
        left.radio_last_packet_type != right.radio_last_packet_type ||
        left.radio_dio2_as_rf_switch != right.radio_dio2_as_rf_switch ||
        left.radio_tcxo_mv != right.radio_tcxo_mv ||
        left.radio_scan_active != right.radio_scan_active ||
        left.radio_scan_index != right.radio_scan_index ||
        left.radio_scan_count != right.radio_scan_count ||
        left.radio_scan_raw_count != right.radio_scan_raw_count ||
        left.radio_scan_decoded_count != right.radio_scan_decoded_count ||
        left.radio_scan_status != right.radio_scan_status ||
        left.radio_cad_detected_count != right.radio_cad_detected_count ||
        left.radio_cad_error_count != right.radio_cad_error_count ||
        left.radio_cad_status != right.radio_cad_status ||
        left.error_flags != right.error_flags ||
        left.queue_len != right.queue_len ||
        left.noise_floor != right.noise_floor ||
        left.last_rssi != right.last_rssi ||
        left.last_snr_quarters != right.last_snr_quarters ||
        left.compose_text != right.compose_text ||
        left.compose_recipient != right.compose_recipient ||
        left.edit_field != right.edit_field ||
        left.last_applied_edit != right.last_applied_edit ||
        left.edit_title != right.edit_title ||
        left.edit_value != right.edit_value ||
        left.edit_error != right.edit_error ||
        left.tool_title != right.tool_title ||
        left.tool_status != right.tool_status ||
        left.tool_detail != right.tool_detail ||
        left.tool_path != right.tool_path ||
        left.radio_state != right.radio_state ||
        left.radio_last_decode != right.radio_last_decode ||
        left.gps_state != right.gps_state ||
        left.storage_state != right.storage_state ||
        left.ble_state != right.ble_state ||
        left.firmware_version != right.firmware_version) {
        return true;
    }

    if (active_screen == ScreenId::Diagnostics) {
        return left.battery_mv != right.battery_mv ||
               left.heap_free_bytes != right.heap_free_bytes ||
               left.psram_free_bytes != right.psram_free_bytes ||
               left.psram_total_bytes != right.psram_total_bytes ||
               left.uptime_seconds != right.uptime_seconds;
    }
    return false;
}

bool snapshot_needs_render(const AppSnapshot& left, const AppSnapshot& right) {
    return app_state_needs_render(left.state, right.state) ||
           !same_nodes(left.nodes, right.nodes) ||
           !same_messages(left.messages, right.messages) ||
           !same_channels(left.channels, right.channels) ||
           left.logs != right.logs;
}

NodeInfo* selected_node() {
    if (state.nodes.empty()) {
        return nullptr;
    }
    state.state.selected_node = clamp_index(state.state.selected_node, state.nodes);
    return &state.nodes[static_cast<std::size_t>(state.state.selected_node)];
}

MeshMessage* selected_message() {
    if (state.messages.empty()) {
        return nullptr;
    }
    state.state.selected_message = clamp_index(state.state.selected_message, state.messages);
    return &state.messages[static_cast<std::size_t>(state.state.selected_message)];
}

ChannelInfo* selected_channel() {
    if (state.channels.empty()) {
        return nullptr;
    }
    state.state.selected_channel = clamp_index(state.state.selected_channel, state.channels);
    return &state.channels[static_cast<std::size_t>(state.state.selected_channel)];
}

bool has_message_id(const std::vector<MeshMessage>& messages, const std::string& id) {
    return std::any_of(messages.begin(), messages.end(), [&](const MeshMessage& message) {
        return message.id == id;
    });
}

bool is_channel_message(const MeshMessage& message) {
    return message.subject.rfind("Channel ", 0) == 0;
}

std::string message_channel(const MeshMessage& message) {
    return is_channel_message(message) ? message.subject.substr(8) : "";
}

void sync_state_channel_to_selection() {
    if (state.channels.empty()) {
        return;
    }
    const auto index = static_cast<std::size_t>(clamp_index(state.state.selected_channel, state.channels));
    if (!state.channels[index].name.empty()) {
        state.state.channel = state.channels[index].name;
    }
}

bool controller_text_matches_peer(const std::string& value, const NodeInfo& node) {
    return value == node.short_id || value == node.name ||
           (!node.short_id.empty() && value.find(node.short_id) != std::string::npos) ||
           (!node.name.empty() && value.find(node.name) != std::string::npos);
}

bool controller_is_chat_screen(ScreenId screen) {
    return screen == ScreenId::Inbox || screen == ScreenId::MessageView ||
           screen == ScreenId::Compose || screen == ScreenId::Channels;
}

bool controller_is_node_screen(ScreenId screen) {
    return screen == ScreenId::Nodes || screen == ScreenId::Contacts;
}

bool message_matches_chat(const MeshMessage& message, bool channel_mode) {
    if (channel_mode) {
        if (!is_channel_message(message)) {
            return false;
        }
        const std::string channel = message_channel(message);
        if (!state.channels.empty()) {
            const auto index = static_cast<std::size_t>(clamp_index(state.state.selected_channel, state.channels));
            return channel == state.channels[index].name;
        }
        return channel == state.state.channel;
    }

    if (is_channel_message(message)) {
        return false;
    }
    if (state.nodes.empty()) {
        return true;
    }
    const auto index = static_cast<std::size_t>(clamp_index(state.state.selected_node, state.nodes));
    const auto& selected = state.nodes[index];
    return message.outgoing ? controller_text_matches_peer(message.subject, selected)
                            : controller_text_matches_peer(message.sender, selected);
}

void select_message_conversation(const MeshMessage& message) {
    if (is_channel_message(message)) {
        const std::string channel_name = message.subject.substr(8);
        for (std::size_t i = 0; i < state.channels.size(); ++i) {
            if (state.channels[i].name == channel_name) {
                state.state.selected_channel = static_cast<int>(i);
                state.state.compose_recipient = "broadcast";
                state.state.channel = state.channels[i].name;
                return;
            }
        }
        state.state.compose_recipient = "broadcast";
        return;
    }

    for (std::size_t i = 0; i < state.nodes.size(); ++i) {
        if (state.nodes[i].short_id == message.sender || state.nodes[i].name == message.sender) {
            state.state.selected_node = static_cast<int>(i);
            state.state.compose_recipient = state.nodes[i].short_id;
            return;
        }
    }
}

void refresh_counts() {
    state.state.node_count = static_cast<int>(state.nodes.size());
    state.state.unread_count = 0;
    for (const auto& message : state.messages) {
        if (!message.outgoing) {
            ++state.state.unread_count;
        }
    }
    sync_state_channel_to_selection();
}

bool parse_unsigned_value(const std::string& value, unsigned& out) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const auto parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<unsigned>(parsed);
    return true;
}

bool parse_double_value(const std::string& value, double& out) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const auto parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    out = parsed;
    return true;
}

std::string bandwidth_khz_text(unsigned bandwidth_hz) {
    if (bandwidth_hz % 1000U == 0) {
        return std::to_string(bandwidth_hz / 1000U);
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << static_cast<double>(bandwidth_hz) / 1000.0;
    return out.str();
}

std::string hex_secret(const ChannelInfo& channel) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : channel.secret) {
        out << std::setw(2) << static_cast<unsigned>(byte);
    }
    return out.str();
}

std::string path_summary_for_selected_node() {
    const auto* node = selected_node();
    if (node == nullptr) {
        return "no contact selected";
    }
    if (node->out_path_len == NodeInfo::out_path_unknown) {
        return node->name + ": path unknown";
    }
    if (node->out_path_len == 0) {
        return node->name + ": direct";
    }
    std::ostringstream out;
    out << node->name << ": ";
    for (uint8_t i = 0; i < node->out_path_len && i < node->out_path.size(); ++i) {
        if (i > 0) {
            out << " > ";
        }
        out << static_cast<unsigned>(node->out_path[i]);
    }
    return out.str();
}

void begin_edit(EditField field, const std::string& title, const std::string& value) {
    state.state.edit_field = field;
    state.state.last_applied_edit = EditField::NoEdit;
    state.state.edit_title = title;
    state.state.edit_value = value;
    state.state.edit_error.clear();
    log_event("edit: " + title);
}

void apply_edit_value() {
    state.state.edit_error.clear();
    const auto field = state.state.edit_field;
    const auto value = state.state.edit_value;
    unsigned unsigned_value = 0;
    double double_value = 0.0;
    switch (field) {
        case EditField::NoEdit:
            return;
        case EditField::ComposeText:
            state.state.compose_text = value;
            break;
        case EditField::DeviceName:
            if (value.empty() || value.size() > 31) {
                state.state.edit_error = "name must be 1..31 chars";
                return;
            }
            state.state.device_name = value;
            break;
        case EditField::DevicePin:
            if (value.size() < 4 || value.size() > 12) {
                state.state.edit_error = "PIN must be 4..12 chars";
                return;
            }
            state.state.device_pin_set = true;
            break;
        case EditField::ChannelName:
            if (auto* channel = selected_channel()) {
                if (value.empty() || value.size() > 16) {
                    state.state.edit_error = "channel must be 1..16 chars";
                    return;
                }
                channel->name = value;
                if (channel->active) {
                    state.state.channel = value;
                }
            }
            break;
        case EditField::ChannelSecret:
            if (auto* channel = selected_channel()) {
                channel->secret = {};
                for (std::size_t i = 0; i < value.size() && i / 2 < channel->secret.size(); i += 2) {
                    if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
                        state.state.edit_error = "secret must be hex";
                        return;
                    }
                    const auto hi = static_cast<unsigned>(std::strtoul(value.substr(i, 1).c_str(), nullptr, 16));
                    const auto lo = i + 1 < value.size() && std::isxdigit(static_cast<unsigned char>(value[i + 1]))
                        ? static_cast<unsigned>(std::strtoul(value.substr(i + 1, 1).c_str(), nullptr, 16))
                        : 0U;
                    channel->secret[i / 2] = static_cast<unsigned char>((hi << 4U) | lo);
                }
            }
            break;
        case EditField::RadioFrequency:
            if (!parse_unsigned_value(value, unsigned_value) || unsigned_value < 150000 || unsigned_value > 2500000) {
                state.state.edit_error = "freq kHz out of range";
                return;
            }
            state.state.radio_frequency_khz = unsigned_value;
            break;
        case EditField::RadioBandwidth:
            if (!parse_double_value(value, double_value) || double_value < 7.0 || double_value > 500.0) {
                state.state.edit_error = "bandwidth kHz 7..500";
                return;
            }
            state.state.radio_bandwidth_hz = static_cast<unsigned>((double_value * 1000.0) + 0.5);
            break;
        case EditField::RadioSpreadingFactor:
            if (!parse_unsigned_value(value, unsigned_value) || unsigned_value < 5 || unsigned_value > 12) {
                state.state.edit_error = "SF must be 5..12";
                return;
            }
            state.state.radio_spreading_factor = unsigned_value;
            break;
        case EditField::RadioCodingRate:
            if (!parse_unsigned_value(value, unsigned_value) || unsigned_value < 5 || unsigned_value > 8) {
                state.state.edit_error = "CR must be 5..8";
                return;
            }
            state.state.radio_coding_rate = unsigned_value;
            break;
        case EditField::Latitude:
            if (!parse_double_value(value, double_value) || double_value < -90.0 || double_value > 90.0) {
                state.state.edit_error = "lat must be -90..90";
                return;
            }
            state.state.latitude = double_value;
            state.state.advert_location_policy = 1;
            break;
        case EditField::Longitude:
            if (!parse_double_value(value, double_value) || double_value < -180.0 || double_value > 180.0) {
                state.state.edit_error = "lon must be -180..180";
                return;
            }
            state.state.longitude = double_value;
            state.state.advert_location_policy = 1;
            break;
        case EditField::CustomVarValue:
            if (!parse_unsigned_value(value, unsigned_value)) {
                state.state.edit_error = "value must be numeric";
                return;
            }
            state.state.custom_var_value = unsigned_value;
            break;
    }
    state.state.last_applied_edit = field;
    state.state.edit_field = EditField::NoEdit;
    state.state.edit_title.clear();
    state.state.edit_value.clear();
    log_event("edit: applied");
}

}  // namespace

void app_set_snapshot(const AppSnapshot& snapshot) {
    state = snapshot;
    refresh_counts();
    mark_changed();
}

const AppSnapshot& app_snapshot() {
    return state;
}

unsigned app_snapshot_version() {
    return state_version;
}

void app_set_active_screen(ScreenId screen) {
    active_screen = screen;
}

ScreenId app_active_screen() {
    return active_screen;
}

void app_set_action_sink(AppActionSink sink) {
    action_sink = sink;
}

void app_set_compose_text(const std::string& text) {
    state.state.compose_text = text;
}

bool app_edit_active() {
    return state.state.edit_field != EditField::NoEdit;
}

bool app_handle_key(char key) {
    if (state.state.edit_field == EditField::NoEdit) {
        return false;
    }
    const auto before = state;
    if (key == 27) {
        state.state.edit_field = EditField::NoEdit;
        state.state.edit_title.clear();
        state.state.edit_value.clear();
        state.state.edit_error.clear();
        log_event("edit: canceled");
    } else if (key == '\r' || key == '\n') {
        apply_edit_value();
    } else if (key == 8 || key == 127) {
        if (!state.state.edit_value.empty()) {
            state.state.edit_value.pop_back();
        }
    } else if (key >= 32 && key <= 126) {
        if (state.state.edit_value.size() < 160) {
            state.state.edit_value.push_back(key);
        }
    } else {
        return true;
    }
    if (snapshot_needs_render(before, state)) {
        mark_changed();
    }
    return true;
}

void app_select_chat_contact(int index) {
    if (state.nodes.empty()) {
        return;
    }
    state.state.selected_node = clamp_index(index, state.nodes);
    const auto& node = state.nodes[static_cast<std::size_t>(state.state.selected_node)];
    state.state.compose_recipient = node.short_id;
    log_event("chat: DM " + node.name);
    mark_changed();
}

void app_select_chat_channel(int index) {
    if (state.channels.empty()) {
        return;
    }
    state.state.selected_channel = clamp_index(index, state.channels);
    state.state.compose_recipient = "broadcast";
    const auto& channel = state.channels[static_cast<std::size_t>(state.state.selected_channel)];
    state.state.channel = channel.name;
    log_event("chat: channel " + channel.name);
    mark_changed();
}

void app_scroll_selection(ScreenId screen, int delta) {
    if (delta == 0) {
        return;
    }
    const auto before = state;
    if (controller_is_chat_screen(screen)) {
        if (!state.messages.empty()) {
            const bool channel_mode = screen == ScreenId::Channels || state.state.compose_recipient == "broadcast";
            const int size = static_cast<int>(state.messages.size());
            int index = clamp_index(state.state.selected_message, state.messages);
            for (int step = 0; step < size; ++step) {
                index = (index + delta + size) % size;
                if (message_matches_chat(state.messages[static_cast<std::size_t>(index)], channel_mode)) {
                    state.state.selected_message = index;
                    state.state.last_event = "chat: message select";
                    break;
                }
            }
        }
    } else if (controller_is_node_screen(screen)) {
        if (!state.nodes.empty()) {
            const int next = clamp_index(state.state.selected_node + delta, state.nodes);
            state.state.selected_node = next;
            state.state.compose_recipient = state.nodes[static_cast<std::size_t>(next)].short_id;
            state.state.last_event = "nodes: selected";
        }
    } else if (screen == ScreenId::ChannelEditor) {
        if (!state.channels.empty()) {
            state.state.selected_channel = clamp_index(state.state.selected_channel + delta, state.channels);
            state.state.compose_recipient = "broadcast";
            state.state.last_event = "channel: selected";
        }
    } else if (screen == ScreenId::Diagnostics) {
        const int total = static_cast<int>(5 + state.logs.size());
        constexpr int visible = 5;
        const int max_scroll = std::max(0, total - visible);
        state.state.diagnostics_scroll = std::max(0, std::min(max_scroll, state.state.diagnostics_scroll + delta));
        state.state.last_event = "diag: scrolled";
    }
    if (snapshot_needs_render(before, state)) {
        mark_changed();
    }
}

void app_ingest_service_snapshot(const AppSnapshot& snapshot) {
    const auto before = state;
    const auto selected_message_index = state.state.selected_message;
    state = snapshot;
    state.state.selected_message = clamp_index(selected_message_index, state.messages);
    refresh_counts();
    for (std::size_t i = 0; i < state.messages.size(); ++i) {
        const auto& message = state.messages[i];
        if (!message.outgoing && !message.id.empty() && !has_message_id(before.messages, message.id)) {
            state.state.selected_message = static_cast<int>(i);
            select_message_conversation(message);
            break;
        }
    }
    if (snapshot_needs_render(before, state)) {
        mark_changed();
    }
}

void app_handle_action(ScreenId source, const Action& action) {
    if (action.command == ActionCommand::SendMessage && source == ScreenId::Channels) {
        state.state.compose_recipient = "broadcast";
    }
    const auto before = state;
    switch (action.command) {
        case ActionCommand::Navigate:
        case ActionCommand::StartApp:
            break;
        case ActionCommand::OpenSelectedMessage:
            state.state.selected_message = clamp_index(state.state.selected_message, state.messages);
            log_event("message: opened selected");
            break;
        case ActionCommand::NextMessage:
            if (!state.messages.empty()) {
                state.state.selected_message = (state.state.selected_message + 1) % static_cast<int>(state.messages.size());
                log_event("message: selected next");
            }
            break;
        case ActionCommand::DeleteMessage:
            if (!state.messages.empty()) {
                state.state.selected_message = clamp_index(state.state.selected_message, state.messages);
                state.messages.erase(state.messages.begin() + state.state.selected_message);
                state.state.selected_message = clamp_index(state.state.selected_message, state.messages);
                log_event("message: deleted");
            }
            break;
        case ActionCommand::ReplyMessage:
            if (const auto* message = selected_message()) {
                if (is_channel_message(*message)) {
                    select_message_conversation(*message);
                } else {
                    state.state.compose_recipient = message->sender;
                }
                state.state.compose_text = "Re: " + message->subject + " ";
                log_event("compose: reply started");
            }
            break;
        case ActionCommand::CycleRecipient:
            if (!state.nodes.empty()) {
                state.state.selected_node = (state.state.selected_node + 1) % static_cast<int>(state.nodes.size());
                state.state.compose_recipient = state.nodes[static_cast<std::size_t>(state.state.selected_node)].short_id;
                log_event("chat: DM " + state.nodes[static_cast<std::size_t>(state.state.selected_node)].name);
            }
            break;
        case ActionCommand::SendMessage: {
            const std::string body = state.state.compose_text.empty() ? "Ping" : state.state.compose_text;
            std::ostringstream id;
            id << next_message_id++;
            const std::string to = state.state.compose_recipient.empty() ? "broadcast" : state.state.compose_recipient;
            const bool channel_target = to == "broadcast";
            std::string subject = "Direct to " + to;
            if (channel_target) {
                const auto* channel = selected_channel();
                subject = "Channel " + (channel != nullptr ? channel->name : state.state.channel);
            }
            state.messages.insert(state.messages.begin(), {id.str(), "me", subject, body, 0, true, false, false, false, "queued"});
            state.state.selected_message = 0;
            state.state.radio_state = "tx ok";
            state.state.compose_text.clear();
            log_event(channel_target ? "mesh: sent channel message" : "mesh: sent direct message");
            break;
        }
        case ActionCommand::ClearCompose:
            state.state.compose_text.clear();
            log_event("compose: cleared");
            break;
        case ActionCommand::EditCompose:
            begin_edit(EditField::ComposeText, "Message text", state.state.compose_text);
            break;
        case ActionCommand::ScanNodes:
            for (auto& node : state.nodes) {
                node.last_seen_seconds = node.last_seen_seconds > 0 ? node.last_seen_seconds - 1 : 0;
                node.rssi += node.rssi < -55 ? 1 : 0;
            }
            log_event("mesh: node scan complete");
            break;
        case ActionCommand::ScanRadio:
            state.state.radio_scan_active = !state.state.radio_scan_active;
            state.state.radio_scan_status = state.state.radio_scan_active ? "starting" : "stopping";
            state.state.radio_state = state.state.radio_scan_active ? "scan rx" : "rx";
            log_event(state.state.radio_scan_active ? "radio: receive scan starting" : "radio: receive scan stopping");
            break;
        case ActionCommand::RunCadScan:
            state.state.radio_cad_status = "queued";
            state.state.radio_scan_status = "cad queued";
            log_event("radio: cad requested");
            break;
        case ActionCommand::PingNode:
            if (const auto* node = selected_node()) {
                state.state.radio_state = "ping " + node->short_id;
                log_event("mesh: ping " + node->name);
            }
            break;
        case ActionCommand::NextNode:
            if (!state.nodes.empty()) {
                state.state.selected_node = (state.state.selected_node + 1) % static_cast<int>(state.nodes.size());
                log_event("nodes: selected next");
            }
            break;
        case ActionCommand::ResetContactPath:
            if (auto* node = selected_node()) {
                node->out_path_len = NodeInfo::out_path_unknown;
                node->out_path = {};
                log_event("contact: path reset");
            }
            break;
        case ActionCommand::ShareContact:
            if (const auto* node = selected_node()) {
                log_event("contact: share " + node->name);
            }
            break;
        case ActionCommand::RemoveContact:
            if (!state.nodes.empty()) {
                state.state.selected_node = clamp_index(state.state.selected_node, state.nodes);
                const std::string name = state.nodes[static_cast<std::size_t>(state.state.selected_node)].name;
                state.nodes.erase(state.nodes.begin() + state.state.selected_node);
                state.state.selected_node = clamp_index(state.state.selected_node, state.nodes);
                log_event("contact: removed " + name);
            }
            break;
        case ActionCommand::JoinChannel:
            if (auto* channel = selected_channel()) {
                for (auto& item : state.channels) {
                    item.active = false;
                }
                channel->active = true;
                state.state.channel = channel->name;
                state.state.compose_recipient = "broadcast";
                log_event("channel: joined " + channel->name);
            }
            break;
        case ActionCommand::LeaveChannel:
            if (auto* channel = selected_channel()) {
                channel->active = false;
                log_event("channel: left " + channel->name);
            }
            break;
        case ActionCommand::NextChannel:
            if (!state.channels.empty()) {
                state.state.selected_channel = (state.state.selected_channel + 1) % static_cast<int>(state.channels.size());
                state.state.compose_recipient = "broadcast";
                const auto& channel = state.channels[static_cast<std::size_t>(state.state.selected_channel)];
                state.state.channel = channel.name;
                log_event("chat: channel " + channel.name);
            }
            break;
        case ActionCommand::EditChannelName:
            if (const auto* channel = selected_channel()) {
                begin_edit(EditField::ChannelName, "Channel name", channel->name);
            }
            break;
        case ActionCommand::EditChannelSecret:
            if (const auto* channel = selected_channel()) {
                begin_edit(EditField::ChannelSecret, "Channel secret", hex_secret(*channel));
            }
            break;
        case ActionCommand::CycleChannelSecret:
            if (auto* channel = selected_channel()) {
                channel->secret[0] = static_cast<unsigned char>(channel->secret[0] + 1U);
                log_event("channel: secret slot changed");
            }
            break;
        case ActionCommand::ToggleRegion:
            if (state.state.region == "915 MHz") {
                state.state.region = "868 MHz";
            } else if (state.state.region == "868 MHz") {
                state.state.region = "433 MHz";
            } else {
                state.state.region = "915 MHz";
            }
            apply_region_defaults(state.state);
            log_event("radio: region " + state.state.region);
            break;
        case ActionCommand::CycleTxPower:
            state.state.tx_power_dbm += 2;
            if (state.state.tx_power_dbm > 22) {
                state.state.tx_power_dbm = 10;
            }
            log_event("radio: tx power changed");
            break;
        case ActionCommand::CyclePathHash:
            state.state.path_hash_mode = (state.state.path_hash_mode + 1) % 3;
            log_event("radio: path hash mode changed");
            break;
        case ActionCommand::CycleFrequency:
            if (state.state.radio_frequency_khz == 910525) {
                state.state.radio_frequency_khz = 927875;
            } else if (state.state.radio_frequency_khz == 927875) {
                state.state.radio_frequency_khz = 918000;
            } else if (state.state.radio_frequency_khz == 918000) {
                state.state.radio_frequency_khz = 915000;
            } else {
                state.state.radio_frequency_khz = 910525;
            }
            state.state.region = "915 MHz";
            log_event("radio: frequency changed");
            break;
        case ActionCommand::EditFrequency:
            begin_edit(EditField::RadioFrequency, "Frequency kHz", std::to_string(state.state.radio_frequency_khz));
            break;
        case ActionCommand::CycleBandwidth:
            if (state.state.radio_bandwidth_hz == 250000) {
                state.state.radio_bandwidth_hz = 125000;
            } else if (state.state.radio_bandwidth_hz == 125000) {
                state.state.radio_bandwidth_hz = 62500;
            } else if (state.state.radio_bandwidth_hz == 62500) {
                state.state.radio_bandwidth_hz = 500000;
            } else {
                state.state.radio_bandwidth_hz = 250000;
            }
            log_event("radio: bandwidth changed");
            break;
        case ActionCommand::EditBandwidth:
            begin_edit(EditField::RadioBandwidth, "Bandwidth kHz", bandwidth_khz_text(state.state.radio_bandwidth_hz));
            break;
        case ActionCommand::CycleSpreadingFactor:
            ++state.state.radio_spreading_factor;
            if (state.state.radio_spreading_factor > 12) {
                state.state.radio_spreading_factor = 7;
            }
            log_event("radio: spreading factor changed");
            break;
        case ActionCommand::EditSpreadingFactor:
            begin_edit(EditField::RadioSpreadingFactor, "Spreading factor", std::to_string(state.state.radio_spreading_factor));
            break;
        case ActionCommand::CycleCodingRate:
            ++state.state.radio_coding_rate;
            if (state.state.radio_coding_rate > 8) {
                state.state.radio_coding_rate = 5;
            }
            log_event("radio: coding rate changed");
            break;
        case ActionCommand::EditCodingRate:
            begin_edit(EditField::RadioCodingRate, "Coding rate", std::to_string(state.state.radio_coding_rate));
            break;
        case ActionCommand::ToggleClientRepeat:
            state.state.client_repeat = !state.state.client_repeat;
            log_event(state.state.client_repeat ? "mesh: repeat enabled" : "mesh: repeat disabled");
            break;
        case ActionCommand::CycleTuning:
            state.state.rx_delay_base_ms = (state.state.rx_delay_base_ms + 250U) % 2000U;
            state.state.airtime_factor_ms = (state.state.airtime_factor_ms + 16U) % 128U;
            log_event("mesh: tuning changed");
            break;
        case ActionCommand::ToggleManualContacts:
            state.state.manual_add_contacts = !state.state.manual_add_contacts;
            log_event(state.state.manual_add_contacts ? "contacts: manual add" : "contacts: auto add");
            break;
        case ActionCommand::CycleAutoAdd:
            state.state.autoadd_config = (state.state.autoadd_config + 1U) % 4U;
            state.state.autoadd_max_hops = state.state.autoadd_max_hops >= 64U ? 8U : state.state.autoadd_max_hops + 8U;
            log_event("contacts: auto-add changed");
            break;
        case ActionCommand::ToggleGps:
            state.state.gps_enabled = !state.state.gps_enabled;
            state.state.gps_state = state.state.gps_enabled ? "mock fix" : "off";
            log_event(state.state.gps_enabled ? "gps: enabled" : "gps: disabled");
            break;
        case ActionCommand::ToggleAudio:
            state.state.audio_enabled = !state.state.audio_enabled;
            log_event(state.state.audio_enabled ? "audio: enabled" : "audio: muted");
            break;
        case ActionCommand::CycleAdvertPolicy:
            state.state.advert_location_policy = (state.state.advert_location_policy + 1U) % 3U;
            log_event("advert: location policy changed");
            break;
        case ActionCommand::EditDeviceName:
            begin_edit(EditField::DeviceName, "Device name", state.state.device_name);
            break;
        case ActionCommand::EditDevicePin:
            begin_edit(EditField::DevicePin, "Device PIN", state.state.device_pin_set ? "1234" : "");
            break;
        case ActionCommand::EditLatitude:
            begin_edit(EditField::Latitude, "Latitude", std::to_string(state.state.latitude));
            break;
        case ActionCommand::EditLongitude:
            begin_edit(EditField::Longitude, "Longitude", std::to_string(state.state.longitude));
            break;
        case ActionCommand::SendSelfAdvert:
            state.state.packet_tx_count++;
            state.state.radio_state = "advert queued";
            log_event("advert: self advert queued");
            break;
        case ActionCommand::MapCenter:
            state.state.latitude = 37.7749;
            state.state.longitude = -122.4194;
            log_event("map: centered");
            break;
        case ActionCommand::MapZoomIn:
            state.state.map_zoom = std::min(state.state.map_zoom + 1, 12);
            log_event("map: zoom in");
            break;
        case ActionCommand::MapZoomOut:
            state.state.map_zoom = std::max(state.state.map_zoom - 1, 1);
            log_event("map: zoom out");
            break;
        case ActionCommand::ToggleBle:
            state.state.ble_enabled = !state.state.ble_enabled;
            state.state.ble_state = state.state.ble_enabled ? "advertising" : "off";
            log_event(state.state.ble_enabled ? "ble: enabled" : "ble: disabled");
            break;
        case ActionCommand::RoomLogin:
            state.state.room_logged_in = !state.state.room_logged_in;
            log_event(state.state.room_logged_in ? "room: logged in" : "room: logged out");
            break;
        case ActionCommand::RemoteAdmin:
            state.state.repeater_admin = !state.state.repeater_admin;
            log_event(state.state.repeater_admin ? "repeater: admin session open" : "repeater: admin closed");
            break;
        case ActionCommand::SyncClock:
            log_event("server: clock sync queued");
            break;
        case ActionCommand::RequestStatus:
            log_event("remote: status request queued");
            break;
        case ActionCommand::TracePath:
            state.state.packet_tx_count++;
            state.state.tool_title = "Trace path";
            state.state.tool_status = "queued";
            state.state.tool_detail = "Tracing selected contact route";
            state.state.tool_path = path_summary_for_selected_node();
            log_event("tools: trace path queued");
            break;
        case ActionCommand::DiscoverPath:
            state.state.packet_tx_count++;
            state.state.tool_title = "Path discovery";
            state.state.tool_status = "queued";
            state.state.tool_detail = "Discovering route to selected contact";
            state.state.tool_path = path_summary_for_selected_node();
            log_event("tools: path discovery queued");
            break;
        case ActionCommand::SendTelemetry:
            state.state.packet_tx_count++;
            state.state.tool_title = "Telemetry";
            state.state.tool_status = "queued";
            state.state.tool_detail = "Telemetry request sent";
            state.state.tool_path = "rx " + std::to_string(state.state.packet_rx_count) +
                " tx " + std::to_string(state.state.packet_tx_count);
            log_event("tools: telemetry request queued");
            break;
        case ActionCommand::ExportPrivateKey:
            state.state.private_key_export_enabled = !state.state.private_key_export_enabled;
            log_event(state.state.private_key_export_enabled ? "identity: export armed" : "identity: export locked");
            break;
        case ActionCommand::ImportPrivateKey:
            state.state.identity_import_pending = !state.state.identity_import_pending;
            log_event(state.state.identity_import_pending ? "identity: import pending" : "identity: import cleared");
            break;
        case ActionCommand::SetDevicePin:
            state.state.device_pin_set = !state.state.device_pin_set;
            log_event(state.state.device_pin_set ? "security: pin set" : "security: pin cleared");
            break;
        case ActionCommand::RebootDevice:
            log_event("admin: reboot requested");
            break;
        case ActionCommand::FactoryReset:
            log_event("admin: factory reset armed");
            break;
        case ActionCommand::CycleFloodScope:
            state.state.default_flood_scope = (state.state.default_flood_scope + 1U) % 4U;
            state.state.flood_scope_key++;
            log_event("mesh: flood scope changed");
            break;
        case ActionCommand::CycleCustomVar:
            state.state.custom_var_index = (state.state.custom_var_index + 1U) % 8U;
            state.state.custom_var_value++;
            log_event("custom: variable changed");
            break;
        case ActionCommand::EditCustomVar:
            begin_edit(EditField::CustomVarValue, "Custom var value", std::to_string(state.state.custom_var_value));
            break;
        case ActionCommand::AddLog:
            log_event("diag: log refresh");
            break;
    }
    refresh_counts();
    if (action_sink != nullptr) {
        action_sink(action.command, before, state);
    }
    mark_changed();
}

}  // namespace meshcore
