#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace meshcore {

constexpr int screen_width = 320;
constexpr int screen_height = 240;
constexpr int top_bar_height = 24;
constexpr int bottom_bar_height = 28;
constexpr int content_height = screen_height - top_bar_height - bottom_bar_height;

enum class ScreenId {
    Boot,
    Home,
    Inbox,
    MessageView,
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

enum class ActionCommand {
    Navigate,
    StartApp,
    OpenSelectedMessage,
    NextMessage,
    DeleteMessage,
    ReplyMessage,
    CycleRecipient,
    SendMessage,
    ClearCompose,
    EditCompose,
    ScanNodes,
    ScanRadio,
    RunCadScan,
    PingNode,
    NextNode,
    ResetContactPath,
    ShareContact,
    RemoveContact,
    JoinChannel,
    LeaveChannel,
    NextChannel,
    EditChannelName,
    EditChannelSecret,
    CycleChannelSecret,
    ToggleRegion,
    CycleTxPower,
    CyclePathHash,
    CycleFrequency,
    EditFrequency,
    CycleBandwidth,
    EditBandwidth,
    CycleSpreadingFactor,
    EditSpreadingFactor,
    CycleCodingRate,
    EditCodingRate,
    ToggleClientRepeat,
    CycleTuning,
    ToggleManualContacts,
    CycleAutoAdd,
    ToggleGps,
    ToggleAudio,
    CycleAdvertPolicy,
    EditDeviceName,
    EditDevicePin,
    EditLatitude,
    EditLongitude,
    SendSelfAdvert,
    MapCenter,
    MapZoomIn,
    MapZoomOut,
    ToggleBle,
    RoomLogin,
    RemoteAdmin,
    SyncClock,
    RequestStatus,
    TracePath,
    DiscoverPath,
    SendTelemetry,
    ExportPrivateKey,
    ImportPrivateKey,
    SetDevicePin,
    RebootDevice,
    FactoryReset,
    CycleFloodScope,
    CycleCustomVar,
    EditCustomVar,
    AddLog,
};

enum class EditField {
    NoEdit,
    ComposeText,
    DeviceName,
    DevicePin,
    ChannelName,
    ChannelSecret,
    RadioFrequency,
    RadioBandwidth,
    RadioSpreadingFactor,
    RadioCodingRate,
    Latitude,
    Longitude,
    CustomVarValue,
};

struct AppState {
    std::string device_name = "T-Deck Plus";
    std::string local_node_id = "0xTDECK";
    std::array<unsigned char, 32> public_key{};
    std::string channel = "test";
    std::string region = "915 MHz";
    unsigned radio_frequency_khz = 910525;
    unsigned radio_bandwidth_hz = 62500;
    unsigned radio_spreading_factor = 7;
    unsigned radio_coding_rate = 5;
    bool client_repeat = false;
    bool manual_add_contacts = true;
    unsigned advert_location_policy = 0;
    unsigned multi_acks = 0;
    unsigned rx_delay_base_ms = 0;
    unsigned airtime_factor_ms = 0;
    unsigned autoadd_config = 0;
    unsigned autoadd_max_hops = 64;
    unsigned default_flood_scope = 0;
    unsigned flood_scope_key = 0;
    std::string default_flood_name;
    std::array<unsigned char, 16> default_flood_secret{};
    unsigned custom_var_index = 0;
    unsigned custom_var_value = 0;
    bool device_pin_set = false;
    bool private_key_export_enabled = false;
    bool identity_import_pending = false;
    bool connected = true;
    int battery_percent = 87;
    int node_count = 5;
    double latitude = 37.7749;
    double longitude = -122.4194;
    int unread_count = 1;
    int selected_message = 0;
    int selected_node = 0;
    int selected_channel = 1;
    int diagnostics_scroll = 0;
    int map_zoom = 4;
    int tx_power_dbm = 20;
    int path_hash_mode = 0;
    int brightness_percent = 80;
    bool gps_enabled = false;
    bool audio_enabled = true;
    bool sd_mounted = true;
    bool storage_writable = true;
    bool room_logged_in = false;
    bool repeater_admin = false;
    bool registered = false;
    bool ble_enabled = false;
    bool ble_connected = false;
    unsigned ble_rx_frames = 0;
    unsigned ble_tx_frames = 0;
    std::string ble_last_command = "none";
    std::string ble_last_error = "none";
    int battery_mv = 3950;
    unsigned uptime_seconds = 0;
    unsigned current_epoch_seconds = 0;
    unsigned heap_free_bytes = 0;
    unsigned psram_free_bytes = 0;
    unsigned psram_total_bytes = 0;
    unsigned persisted_message_count = 0;
    unsigned persisted_node_count = 0;
    unsigned packet_rx_count = 0;
    unsigned packet_tx_count = 0;
    unsigned radio_rx_raw_count = 0;
    unsigned radio_rx_decoded_count = 0;
    unsigned radio_rx_decode_fail_count = 0;
    unsigned radio_tx_fail_count = 0;
    unsigned radio_last_packet_len = 0;
    unsigned radio_last_packet_type = 255;
    bool radio_rx_active = false;
    int radio_dio1_level = -1;
    int radio_busy_level = -1;
    int radio_begin_result = 0;
    int radio_rx_start_result = 0;
    int radio_read_result = 0;
    unsigned radio_irq_flags = 0;
    bool radio_dio2_as_rf_switch = false;
    int radio_tcxo_mv = 1800;
    bool radio_scan_active = false;
    unsigned radio_scan_index = 0;
    unsigned radio_scan_count = 0;
    unsigned radio_scan_raw_count = 0;
    unsigned radio_scan_decoded_count = 0;
    std::string radio_scan_status = "idle";
    unsigned radio_cad_detected_count = 0;
    unsigned radio_cad_error_count = 0;
    std::string radio_cad_status = "idle";
    unsigned error_flags = 0;
    unsigned queue_len = 0;
    int noise_floor = -127;
    int last_rssi = -127;
    int last_snr_quarters = 0;
    std::string compose_text = "ETA 5 minutes.";
    std::string compose_recipient = "A7";
    EditField edit_field = EditField::NoEdit;
    EditField last_applied_edit = EditField::NoEdit;
    std::string edit_title;
    std::string edit_value;
    std::string edit_error;
    std::string tool_title = "Path tools";
    std::string tool_status = "ready";
    std::string tool_detail = "No request sent";
    std::string tool_path = "";
    std::string radio_state = "idle";
    std::string radio_last_decode = "none";
    std::string gps_state = "off";
    std::string storage_state = "mounted";
    std::string ble_state = "off";
    std::string last_event = "Boot";
    std::string firmware_version = "dev";
};

struct NodeInfo {
    static constexpr std::size_t public_key_size = 32;
    static constexpr std::size_t max_path_size = 64;
    static constexpr uint8_t out_path_unknown = 0xff;

    NodeInfo() = default;
    NodeInfo(const std::string& name_value,
             const std::string& short_id_value,
             int rssi_value,
             float snr_value,
             unsigned last_seen_seconds_value,
             bool has_position_value = false,
             double latitude_value = 0.0,
             double longitude_value = 0.0)
        : name(name_value),
          short_id(short_id_value),
          rssi(rssi_value),
          snr(snr_value),
          last_seen_seconds(last_seen_seconds_value),
          has_position(has_position_value),
          latitude(latitude_value),
          longitude(longitude_value) {}

    std::string name;
    std::string short_id;
    int rssi;
    float snr;
    unsigned last_seen_seconds;
    bool has_position = false;
    double latitude = 0.0;
    double longitude = 0.0;
    std::array<unsigned char, public_key_size> public_key{};
    std::array<unsigned char, max_path_size> out_path{};
    uint8_t out_path_len = out_path_unknown;
    uint8_t contact_type = 1;
    uint8_t contact_flags = 0;
    unsigned lastmod = 0;
};

struct MeshMessage {
    MeshMessage() = default;
    MeshMessage(const std::string& id_value,
                const std::string& sender_value,
                const std::string& subject_value,
                const std::string& body_value,
                unsigned timestamp_value,
                bool outgoing_value,
                bool delivered_value = false,
                bool acked_value = false,
                bool persisted_value = false,
                const std::string& status_value = "new")
        : id(id_value),
          sender(sender_value),
          subject(subject_value),
          body(body_value),
          timestamp(timestamp_value),
          outgoing(outgoing_value),
          delivered(delivered_value),
          acked(acked_value),
          persisted(persisted_value),
          status(status_value) {}

    std::string id;
    std::string sender;
    std::string subject;
    std::string body;
    unsigned timestamp;
    bool outgoing;
    bool delivered = false;
    bool acked = false;
    bool persisted = false;
    std::string status = "new";
};

struct ChannelInfo {
    ChannelInfo() = default;
    ChannelInfo(const std::string& name_value,
                bool active_value,
                int users_value,
                const std::array<unsigned char, 16>& secret_value = {})
        : name(name_value),
          active(active_value),
          users(users_value),
          secret(secret_value) {}

    std::string name;
    bool active;
    int users;
    std::array<unsigned char, 16> secret{};
};

struct Action {
    Action() = default;
    Action(const std::string& label_value, ScreenId target_value,
           ActionCommand command_value = ActionCommand::Navigate)
        : label(label_value), target(target_value), command(command_value) {}

    std::string label;
    ScreenId target = ScreenId::Home;
    ActionCommand command = ActionCommand::Navigate;
};

struct AppSnapshot {
    AppState state;
    std::vector<NodeInfo> nodes;
    std::vector<MeshMessage> messages;
    std::vector<ChannelInfo> channels;
    std::vector<std::string> logs;
};

inline const char* screen_title(ScreenId screen) {
    switch (screen) {
        case ScreenId::Boot:
            return "Boot";
        case ScreenId::Home:
            return "Home";
        case ScreenId::Inbox:
            return "Inbox";
        case ScreenId::MessageView:
            return "Message";
        case ScreenId::Compose:
            return "Compose";
        case ScreenId::Nodes:
            return "Nodes";
        case ScreenId::Contacts:
            return "Contacts";
        case ScreenId::Channels:
            return "Channels";
        case ScreenId::ChannelEditor:
            return "Channel Edit";
        case ScreenId::Map:
            return "Map";
        case ScreenId::Settings:
            return "Settings";
        case ScreenId::Radio:
            return "Radio";
        case ScreenId::RadioAdvanced:
            return "Radio Adv";
        case ScreenId::RadioTuning:
            return "Radio Tune";
        case ScreenId::Identity:
            return "Identity";
        case ScreenId::Ble:
            return "Bluetooth";
        case ScreenId::Servers:
            return "Servers";
        case ScreenId::Tools:
            return "Tools";
        case ScreenId::Diagnostics:
            return "Diagnostics";
    }
    return "Unknown";
}

}  // namespace meshcore
