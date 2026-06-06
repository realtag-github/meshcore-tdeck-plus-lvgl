#include "mesh_service.h"
#include "app_config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <SHA256.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <Esp.h>
#include <Preferences.h>
#endif

#include "drivers/wifi_ota_service.h"
#include "app/navigation.h"
#include "ui/app_ui.h"

#ifndef APP_ENABLE_SD_STORAGE
#define APP_ENABLE_SD_STORAGE 0
#endif

namespace {

MeshService* active_service = nullptr;

constexpr uint32_t meshcore_915_default_frequency_khz = 910525;
constexpr uint32_t meshcore_915_default_bandwidth_hz = 62500;
constexpr uint8_t meshcore_915_default_spreading_factor = 7;
constexpr uint8_t meshcore_default_coding_rate = 5;

bool is_valid_client_repeat_frequency(uint32_t frequency_khz) {
    return frequency_khz == 433000 || frequency_khz == 869000 || frequency_khz == 918000;
}

struct RadioScanPreset {
    const char* name;
    uint32_t frequency_khz;
    uint32_t bandwidth_hz;
    uint8_t spreading_factor;
    uint8_t coding_rate;
};

constexpr RadioScanPreset radio_scan_presets[] = {
    {"meshcore-us", meshcore_915_default_frequency_khz, meshcore_915_default_bandwidth_hz, meshcore_915_default_spreading_factor, meshcore_default_coding_rate},
    {"legacy-915-250-sf10", 915000, 250000, 10, 5},
    {"legacy-915-250-sf11", 915000, 250000, 11, 5},
    {"legacy-915-250-sf9", 915000, 250000, 9, 5},
    {"915-125-sf9", 915000, 125000, 9, 5},
    {"915-125-sf10", 915000, 125000, 10, 5},
    {"915-62-sf7", 915000, 62500, 7, 5},
    {"918-62-sf8", 918000, 62500, 8, 5},
    {"us-910.525-125", 910525, 125000, 9, 5},
    {"us-927.875-62", 927875, 62500, 7, 5},
    {"us-927.875-125", 927875, 125000, 9, 5},
    {"912.875-250", 912875, 250000, 10, 5},
    {"917.875-250", 917875, 250000, 10, 5},
    {"922.875-250", 922875, 250000, 10, 5},
    {"927.875-250", 927875, 250000, 10, 5},
};

constexpr uint32_t radio_scan_dwell_ms = 15000;

bool mesh_service_action_sink(meshcore::ActionCommand command,
                              const meshcore::AppSnapshot& before,
                              meshcore::AppSnapshot& after) {
    if (active_service == nullptr) {
        return false;
    }
    return active_service->handleUiCommand(command, before, after);
}

std::string to_string(const String& value) {
    return std::string(value.c_str());
}

String from_string(const std::string& value) {
    return String(value.c_str());
}

String public_key_hex(const std::array<unsigned char, 32>& public_key, std::size_t bytes = 8) {
    const std::size_t n = std::min(bytes, public_key.size());
    String value;
    value.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02X", public_key[i]);
        value += hex;
    }
    return value;
}

template <std::size_t N>
String bytes_hex(const std::array<unsigned char, N>& bytes, std::size_t byte_count = N) {
    const std::size_t n = std::min(byte_count, bytes.size());
    String value;
    value.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02X", bytes[i]);
        value += hex;
    }
    return value;
}

std::string hex_word(unsigned value) {
    char hex[5];
    std::snprintf(hex, sizeof(hex), "%04X", value & 0xffffU);
    return hex;
}

std::string ble_command_label(const BleCompanionCommand& command) {
    const char* name = "unknown";
    switch (command.type) {
        case BleCompanionCommand::Type::AppStart:
            name = "app-start";
            break;
        case BleCompanionCommand::Type::DeviceQuery:
            name = "device-query";
            break;
        case BleCompanionCommand::Type::GetDeviceTime:
            name = "get-time";
            break;
        case BleCompanionCommand::Type::SetDeviceTime:
            name = "set-time";
            break;
        case BleCompanionCommand::Type::SetAdvertName:
            name = "set-name";
            break;
        case BleCompanionCommand::Type::SetAdvertLatLon:
            name = "set-position";
            break;
        case BleCompanionCommand::Type::SendSelfAdvert:
            name = "self-advert";
            break;
        case BleCompanionCommand::Type::SetRadioParams:
            name = "set-radio";
            break;
        case BleCompanionCommand::Type::SetRadioTxPower:
            name = "set-power";
            break;
        case BleCompanionCommand::Type::SetTuningParams:
            name = "set-tuning";
            break;
        case BleCompanionCommand::Type::GetTuningParams:
            name = "get-tuning";
            break;
        case BleCompanionCommand::Type::SetOtherParams:
            name = "set-options";
            break;
        case BleCompanionCommand::Type::ExportPrivateKey:
            name = "export-key";
            break;
        case BleCompanionCommand::Type::ImportPrivateKey:
            name = "import-key";
            break;
        case BleCompanionCommand::Type::GetStats:
            name = "get-stats";
            break;
        case BleCompanionCommand::Type::SetAutoAddConfig:
            name = "set-autoadd";
            break;
        case BleCompanionCommand::Type::GetAutoAddConfig:
            name = "get-autoadd";
            break;
        case BleCompanionCommand::Type::GetAllowedRepeatFreq:
            name = "repeat-freq";
            break;
        case BleCompanionCommand::Type::SetPathHashMode:
            name = "path-hash";
            break;
        case BleCompanionCommand::Type::HasConnection:
            name = "has-connection";
            break;
        case BleCompanionCommand::Type::Logout:
            name = "logout";
            break;
        case BleCompanionCommand::Type::SetDevicePin:
            name = "set-pin";
            break;
        case BleCompanionCommand::Type::GetAdvertPath:
            name = "advert-path";
            break;
        case BleCompanionCommand::Type::SetDefaultFloodScope:
            name = "set-flood";
            break;
        case BleCompanionCommand::Type::GetDefaultFloodScope:
            name = "get-flood";
            break;
        case BleCompanionCommand::Type::GetChannel:
            name = "get-channel";
            break;
        case BleCompanionCommand::Type::SetChannel:
            name = "set-channel";
            break;
        case BleCompanionCommand::Type::SendContactMessage:
            name = "send-dm";
            break;
        case BleCompanionCommand::Type::SendChannelMessage:
            name = "send-channel";
            break;
        case BleCompanionCommand::Type::GetContacts:
            name = "get-contacts";
            break;
        case BleCompanionCommand::Type::AddUpdateContact:
            name = "set-contact";
            break;
        case BleCompanionCommand::Type::RemoveContact:
            name = "remove-contact";
            break;
        case BleCompanionCommand::Type::ResetPath:
            name = "reset-path";
            break;
        case BleCompanionCommand::Type::GetContactByKey:
            name = "get-contact";
            break;
        case BleCompanionCommand::Type::GetMessage:
            name = "sync-message";
            break;
        case BleCompanionCommand::Type::GetBattery:
            name = "battery";
            break;
        case BleCompanionCommand::Type::Unknown:
        default:
            break;
    }
    char label[32];
    std::snprintf(label, sizeof(label), "0x%02X %s", static_cast<unsigned>(command.raw_type), name);
    return label;
}

int hex_nibble(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return -1;
}

template <std::size_t N>
bool parse_hex_bytes(const String& hex, std::array<unsigned char, N>& dest, std::size_t byte_count = N) {
    if (byte_count > N || hex.length() < static_cast<int>(byte_count * 2)) {
        return false;
    }
    for (std::size_t i = 0; i < byte_count; ++i) {
        const int hi = hex_nibble(hex[static_cast<int>(i * 2)]);
        const int lo = hex_nibble(hex[static_cast<int>(i * 2 + 1)]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        dest[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

bool has_public_key(const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& public_key) {
    for (const auto byte : public_key) {
        if (byte != 0 && byte != 0xff) {
            return true;
        }
    }
    return false;
}

String short_id_from_public_key(const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& public_key) {
    if (!has_public_key(public_key)) {
        return "";
    }
    return "0x" + bytes_hex(public_key, 4);
}

std::size_t path_byte_len(uint8_t encoded_path_len) {
    if (encoded_path_len == meshcore::NodeInfo::out_path_unknown) {
        return 0;
    }
    const uint8_t hash_size = (encoded_path_len >> 6) + 1;
    const uint8_t hash_count = encoded_path_len & 63;
    return std::min<std::size_t>(meshcore::NodeInfo::max_path_size, hash_size * hash_count);
}

std::array<unsigned char, 16> public_channel_secret() {
    return {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
            0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
}

std::array<unsigned char, 16> test_channel_secret() {
    return {0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
            0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f};
}

std::array<unsigned char, 16> hashtag_channel_secret(String name) {
    name.trim();
    if (!name.startsWith("#")) {
        name = "#" + name;
    }
    name.toLowerCase();

    uint8_t digest[32] = {};
    SHA256 sha;
    sha.reset();
    sha.update(reinterpret_cast<const uint8_t*>(name.c_str()), name.length());
    sha.finalize(digest, sizeof(digest));

    std::array<unsigned char, 16> secret{};
    std::memcpy(secret.data(), digest, secret.size());
    return secret;
}

constexpr std::size_t max_meshcore_channels = 40;
constexpr uint32_t settings_schema_version = 6;

bool secret_is_empty(const std::array<unsigned char, 16>& secret) {
    return std::all_of(secret.begin(), secret.end(), [](unsigned char value) {
        return value == 0;
    });
}

void trim_empty_channel_tail(std::vector<meshcore::ChannelInfo>& channels) {
    constexpr std::size_t min_visible_channels = 5;
    while (channels.size() > min_visible_channels) {
        const auto& channel = channels.back();
        if (!channel.name.empty() || !secret_is_empty(channel.secret)) {
            break;
        }
        channels.pop_back();
    }
}

void ensure_channel_defaults(std::vector<meshcore::ChannelInfo>& channels) {
    if (channels.empty()) {
        channels.push_back({"public", false, 0});
    }
    if (channels.size() > max_meshcore_channels) {
        channels.resize(max_meshcore_channels);
    }
    if (channels[0].name.empty()) {
        channels[0].name = "public";
    }
    if (secret_is_empty(channels[0].secret)) {
        channels[0].secret = public_channel_secret();
    }

    bool has_test_channel = false;
    for (auto& channel : channels) {
        if (channel.name == "#test") {
            channel.name = "test";
        }
        if (channel.name == "test") {
            has_test_channel = true;
            if (secret_is_empty(channel.secret)) {
                channel.secret = test_channel_secret();
            }
        }
    }
    if (!has_test_channel && channels.size() < max_meshcore_channels) {
        channels.push_back({"test", true, 0, test_channel_secret()});
    }

    bool has_active_channel = false;
    for (const auto& channel : channels) {
        if (channel.active && !channel.name.empty()) {
            has_active_channel = true;
            break;
        }
    }
    if (!has_active_channel) {
        for (auto& channel : channels) {
            if (channel.name == "test") {
                channel.active = true;
                return;
            }
        }
        channels[0].active = true;
    }
}

int find_channel_index(const std::vector<meshcore::ChannelInfo>& channels, const std::string& name) {
    for (std::size_t i = 0; i < channels.size(); ++i) {
        if (channels[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void select_channel(meshcore::AppSnapshot& snapshot, int index) {
    if (snapshot.channels.empty()) {
        snapshot.state.selected_channel = 0;
        return;
    }
    index = std::max<int>(0, std::min<int>(index, static_cast<int>(snapshot.channels.size()) - 1));
    snapshot.state.selected_channel = index;
    for (auto& item : snapshot.channels) {
        item.active = false;
    }
    const auto& channel = snapshot.channels[static_cast<std::size_t>(index)];
    if (!channel.name.empty()) {
        snapshot.channels[static_cast<std::size_t>(index)].active = true;
        snapshot.state.channel = channel.name;
        snapshot.state.compose_recipient = "broadcast";
    }
}

void select_test_channel(meshcore::AppSnapshot& snapshot) {
    const int test_index = find_channel_index(snapshot.channels, "test");
    if (test_index >= 0) {
        select_channel(snapshot, test_index);
    }
}

String clean_record_field(String value) {
    value.replace("|", "/");
    value.replace("\r", " ");
    value.replace("\n", " ");
    return value;
}

String message_record(const meshcore::MeshMessage& message) {
    String line;
    line.reserve(message.id.size() + message.sender.size() + message.subject.size() + message.body.size() + 24);
    line += "MSG|";
    line += clean_record_field(from_string(message.id));
    line += "|";
    line += clean_record_field(from_string(message.sender));
    line += "|";
    line += clean_record_field(from_string(message.subject));
    line += "|";
    line += clean_record_field(from_string(message.body));
    line += "|";
    line += String(message.timestamp);
    line += "|";
    line += message.outgoing ? "1" : "0";
    return line;
}

String delete_record(const String& id) {
    return "DEL|" + clean_record_field(id);
}

String node_record(const meshcore::NodeInfo& node) {
    String line;
    line.reserve(node.short_id.size() + node.name.size() + 260);
    line += "NODE|";
    line += clean_record_field(from_string(node.short_id));
    line += "|";
    line += clean_record_field(from_string(node.name));
    line += "|";
    line += String(node.rssi);
    line += "|";
    line += String(node.snr, 1);
    line += "|";
    line += String(node.last_seen_seconds);
    line += "|";
    line += node.has_position ? "1" : "0";
    line += "|";
    line += String(node.latitude, 6);
    line += "|";
    line += String(node.longitude, 6);
    line += "|";
    line += bytes_hex(node.public_key);
    line += "|";
    line += String(node.out_path_len);
    line += "|";
    line += node.out_path_len == meshcore::NodeInfo::out_path_unknown
                ? String("")
                : bytes_hex(node.out_path, path_byte_len(node.out_path_len));
    line += "|";
    line += String(node.contact_type);
    line += "|";
    line += String(node.contact_flags);
    line += "|";
    line += String(node.lastmod);
    return line;
}

String record_field(const String& record, int field_index) {
    int start = 0;
    int current = 0;
    for (int i = 0; i <= record.length(); ++i) {
        if (i == record.length() || record[i] == '|') {
            if (current == field_index) {
                return record.substring(start, i);
            }
            start = i + 1;
            ++current;
        }
    }
    return "";
}

bool parse_message_record(const String& record, meshcore::MeshMessage& message) {
    const bool prefixed = record.startsWith("MSG|");
    const int offset = prefixed ? 1 : 0;
    const String id = record_field(record, offset + 0);
    const String sender = record_field(record, offset + 1);
    const String subject = record_field(record, offset + 2);
    const String body = record_field(record, offset + 3);
    if (id.length() == 0 || sender.length() == 0 || body.length() == 0) {
        return false;
    }
    message.id = to_string(id);
    message.sender = to_string(sender);
    message.subject = to_string(subject.length() > 0 ? subject : "Message");
    message.body = to_string(body);
    message.timestamp = static_cast<unsigned>(record_field(record, offset + 4).toInt());
    message.outgoing = record_field(record, offset + 5) == "1";
    return true;
}

bool parse_node_record(const String& record, meshcore::NodeInfo& node) {
    const bool prefixed = record.startsWith("NODE|");
    const int offset = prefixed ? 1 : 0;
    const String short_id = record_field(record, offset + 0);
    const String name = record_field(record, offset + 1);
    if (short_id.length() == 0) {
        return false;
    }
    node.short_id = to_string(short_id);
    node.name = to_string(name.length() > 0 ? name : short_id);
    node.rssi = record_field(record, offset + 2).toInt();
    if (node.rssi == 0) {
        node.rssi = -90;
    }
    node.snr = record_field(record, offset + 3).toFloat();
    node.last_seen_seconds = static_cast<unsigned>(record_field(record, offset + 4).toInt());
    node.has_position = record_field(record, offset + 5) == "1";
    node.latitude = record_field(record, offset + 6).toDouble();
    node.longitude = record_field(record, offset + 7).toDouble();
    parse_hex_bytes(record_field(record, offset + 8), node.public_key);
    const String path_len = record_field(record, offset + 9);
    if (path_len.length() > 0) {
        node.out_path_len = static_cast<uint8_t>(std::max<int>(0, std::min<int>(255, path_len.toInt())));
    }
    if (node.out_path_len != meshcore::NodeInfo::out_path_unknown) {
        const std::size_t path_bytes = path_byte_len(node.out_path_len);
        parse_hex_bytes(record_field(record, offset + 10), node.out_path, path_bytes);
    }
    const String type = record_field(record, offset + 11);
    const String flags = record_field(record, offset + 12);
    const String lastmod = record_field(record, offset + 13);
    if (type.length() > 0) {
        node.contact_type = static_cast<uint8_t>(std::max<int>(0, std::min<int>(255, type.toInt())));
    }
    if (flags.length() > 0) {
        node.contact_flags = static_cast<uint8_t>(std::max<int>(0, std::min<int>(255, flags.toInt())));
    }
    if (lastmod.length() > 0) {
        node.lastmod = static_cast<unsigned>(lastmod.toInt());
    }
    return true;
}

bool id_in_list(const std::vector<String>& values, const String& id) {
    for (const auto& value : values) {
        if (value == id) {
            return true;
        }
    }
    return false;
}

bool id_in_list(const std::vector<std::string>& values, const std::string& id) {
    for (const auto& value : values) {
        if (value == id) {
            return true;
        }
    }
    return false;
}

void erase_message_by_id(std::vector<meshcore::MeshMessage>& messages, const String& id) {
    messages.erase(std::remove_if(messages.begin(), messages.end(),
                                  [&](const meshcore::MeshMessage& message) {
                                      return message.id == to_string(id);
                                  }),
                   messages.end());
}

String node_name_for_sender(const String& sender) {
    if (sender.length() == 0) {
        return "Unknown";
    }
    if (sender.startsWith("0x") || sender.startsWith("0X")) {
        return "Node " + sender;
    }
    return sender;
}

bool is_broadcast_target(const String& target) {
    return target.length() == 0 || target == "*" || target == "all" || target == "broadcast";
}

bool is_chat_screen(meshcore::ScreenId screen) {
    return screen == meshcore::ScreenId::Inbox || screen == meshcore::ScreenId::MessageView ||
           screen == meshcore::ScreenId::Compose || screen == meshcore::ScreenId::Channels;
}

bool service_channel_message(const meshcore::MeshMessage& message) {
    return message.subject.rfind("Channel ", 0) == 0;
}

bool service_text_matches_peer(const std::string& value, const meshcore::NodeInfo& node) {
    return value == node.short_id || value == node.name ||
           (!node.short_id.empty() && value.find(node.short_id) != std::string::npos) ||
           (!node.name.empty() && value.find(node.name) != std::string::npos);
}

bool service_message_matches_chat(const meshcore::AppSnapshot& snapshot,
                                  const meshcore::MeshMessage& message,
                                  bool channel_mode) {
    if (channel_mode) {
        if (!service_channel_message(message)) {
            return false;
        }
        const std::string channel = message.subject.substr(8);
        if (!snapshot.channels.empty()) {
            const std::size_t index = static_cast<std::size_t>(std::max<int>(
                0,
                std::min<int>(snapshot.state.selected_channel,
                              static_cast<int>(snapshot.channels.size()) - 1)));
            return channel == snapshot.channels[index].name;
        }
        return channel == snapshot.state.channel;
    }

    if (service_channel_message(message)) {
        return false;
    }
    if (snapshot.nodes.empty()) {
        return true;
    }
    const std::size_t index = static_cast<std::size_t>(std::max<int>(
        0,
        std::min<int>(snapshot.state.selected_node, static_cast<int>(snapshot.nodes.size()) - 1)));
    const auto& selected = snapshot.nodes[index];
    if (message.outgoing) {
        return service_text_matches_peer(message.subject, selected);
    }
    return service_text_matches_peer(message.sender, selected);
}

bool select_next_chat_message(meshcore::AppSnapshot& snapshot, meshcore::ScreenId screen, int delta) {
    if (snapshot.messages.empty()) {
        return false;
    }
    const bool channel_mode = screen == meshcore::ScreenId::Channels ||
                              snapshot.state.compose_recipient == "broadcast";
    const int size = static_cast<int>(snapshot.messages.size());
    int index = snapshot.state.selected_message;
    for (int step = 0; step < size; ++step) {
        index = (index + delta + size) % size;
        if (service_message_matches_chat(snapshot,
                                         snapshot.messages[static_cast<std::size_t>(index)],
                                         channel_mode)) {
            snapshot.state.selected_message = index;
            return true;
        }
    }
    return false;
}

meshcore::NodeInfo* find_node(std::vector<meshcore::NodeInfo>& nodes, const String& id) {
    auto found = std::find_if(nodes.begin(), nodes.end(),
                              [&](meshcore::NodeInfo& node) {
                                  return node.short_id == to_string(id) || node.name == to_string(id);
                              });
    return found == nodes.end() ? nullptr : &(*found);
}

const meshcore::NodeInfo* find_node(const std::vector<meshcore::NodeInfo>& nodes, const String& id) {
    auto found = std::find_if(nodes.begin(), nodes.end(),
                              [&](const meshcore::NodeInfo& node) {
                                  return node.short_id == to_string(id) || node.name == to_string(id);
                              });
    return found == nodes.end() ? nullptr : &(*found);
}

void select_conversation_for_message(meshcore::AppSnapshot& snapshot, const meshcore::MeshMessage& message) {
    if (service_channel_message(message)) {
        const int channel_index = find_channel_index(snapshot.channels, message.subject.substr(8));
        if (channel_index >= 0) {
            select_channel(snapshot, channel_index);
        } else {
            snapshot.state.compose_recipient = "broadcast";
        }
        return;
    }

    for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
        if (service_text_matches_peer(message.sender, snapshot.nodes[i])) {
            snapshot.state.selected_node = static_cast<int>(i);
            snapshot.state.compose_recipient = snapshot.nodes[i].short_id;
            return;
        }
    }
}

bool same_public_key(const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& left,
                     const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& right) {
    return has_public_key(left) && left == right;
}

meshcore::NodeInfo* find_node_by_key(std::vector<meshcore::NodeInfo>& nodes,
                                     const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& public_key) {
    auto found = std::find_if(nodes.begin(), nodes.end(),
                              [&](meshcore::NodeInfo& node) {
                                  return same_public_key(node.public_key, public_key);
                              });
    return found == nodes.end() ? nullptr : &(*found);
}

const meshcore::NodeInfo* find_node_by_key(const std::vector<meshcore::NodeInfo>& nodes,
                                           const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& public_key) {
    auto found = std::find_if(nodes.begin(), nodes.end(),
                              [&](const meshcore::NodeInfo& node) {
                                  return same_public_key(node.public_key, public_key);
                              });
    return found == nodes.end() ? nullptr : &(*found);
}

const meshcore::NodeInfo* find_node_by_key_prefix(const std::vector<meshcore::NodeInfo>& nodes,
                                                  const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& public_key,
                                                  std::size_t prefix_len) {
    if (prefix_len == 0 || prefix_len > public_key.size()) {
        return nullptr;
    }
    auto found = std::find_if(nodes.begin(), nodes.end(),
                              [&](const meshcore::NodeInfo& node) {
                                  return has_public_key(node.public_key) &&
                                         std::equal(public_key.begin(),
                                                    public_key.begin() + prefix_len,
                                                    node.public_key.begin());
                              });
    return found == nodes.end() ? nullptr : &(*found);
}

bool parse_radio_payload(const String& payload,
                         const String& local_node_id,
                         unsigned timestamp,
                         unsigned sequence,
                         meshcore::MeshMessage& message) {
    char id[16];
    std::snprintf(id, sizeof(id), "%u", sequence);

    if (payload.startsWith("MC1|")) {
        const String sender = record_field(payload, 1);
        const String target = record_field(payload, 2);
        const String body = record_field(payload, 3);
        if (sender.length() == 0 || body.length() == 0) {
            return false;
        }
        if (!is_broadcast_target(target) && target != local_node_id) {
            return false;
        }
        const String subject = is_broadcast_target(target) ? String("Broadcast") : String("Direct");
        message = {id,
                   to_string(sender),
                   to_string(subject),
                   to_string(body),
                   timestamp,
                   false};
        return true;
    }

    if (payload.startsWith("POS1|")) {
        const String sender = record_field(payload, 1);
        const String target = record_field(payload, 2);
        const String lat = record_field(payload, 3);
        const String lon = record_field(payload, 4);
        if (sender.length() == 0 || lat.length() == 0 || lon.length() == 0) {
            return false;
        }
        if (!is_broadcast_target(target) && target != local_node_id) {
            return false;
        }
        message = {id,
                   to_string(sender),
                   "Position",
                   to_string(lat + "," + lon),
                   timestamp,
                   false};
        return true;
    }

    if (payload.startsWith("MC2|")) {
        const String sender = record_field(payload, 1);
        const String packet_timestamp = record_field(payload, 2);
        const String body = record_field(payload, 3);
        if (sender.length() == 0 || body.length() == 0) {
            return false;
        }
        message = {id,
                   to_string(sender),
                   "Direct",
                   to_string(body),
                   static_cast<unsigned>(packet_timestamp.toInt()),
                   false};
        return true;
    }

    if (payload.startsWith("CH1|")) {
        const String channel = record_field(payload, 1);
        const String packet_timestamp = record_field(payload, 2);
        const String text = record_field(payload, 3);
        if (channel.length() == 0 || text.length() == 0) {
            return false;
        }

        String sender = "Channel";
        String body = text;
        const int split = text.indexOf(": ");
        if (split > 0) {
            sender = text.substring(0, split);
            body = text.substring(split + 2);
        }

        message = {id,
                   to_string(sender),
                   to_string("Channel " + channel),
                   to_string(body),
                   static_cast<unsigned>(packet_timestamp.toInt()),
                   false};
        return true;
    }

    const int split = payload.indexOf(':');
    if (split > 0 && split < payload.length() - 1) {
        message = {id,
                   to_string(payload.substring(0, split)),
                   "Direct",
                   to_string(payload.substring(split + 1)),
                   timestamp,
                   false};
        return true;
    }

    if (payload.length() > 0) {
        message = {id, "LoRa", "Packet", to_string(payload), timestamp, false};
        return true;
    }
    return false;
}

std::string default_region() {
#if defined(REGION_433)
    return "433 MHz";
#elif defined(REGION_868)
    return "868 MHz";
#else
    return "915 MHz";
#endif
}

uint32_t default_frequency_khz_for_region(const std::string& region) {
    if (region.find("433") != std::string::npos) {
        return 433175;
    }
    if (region.find("868") != std::string::npos) {
        return 868125;
    }
    return meshcore_915_default_frequency_khz;
}

String bandwidth_khz_string(unsigned bandwidth_hz) {
    if (bandwidth_hz % 1000U == 0) {
        return String(bandwidth_hz / 1000U);
    }
    return String(static_cast<float>(bandwidth_hz) / 1000.0f, 1);
}

std::string region_for_frequency_khz(uint32_t frequency_khz) {
    if (frequency_khz >= 430000 && frequency_khz <= 440000) {
        return "433 MHz";
    }
    if (frequency_khz >= 860000 && frequency_khz <= 880000) {
        return "868 MHz";
    }
    if (frequency_khz >= 900000 && frequency_khz <= 930000) {
        return "915 MHz";
    }
    char label[24];
    std::snprintf(label, sizeof(label), "%lu.%03lu MHz",
                  static_cast<unsigned long>(frequency_khz / 1000),
                  static_cast<unsigned long>(frequency_khz % 1000));
    return label;
}

String normalize_command_token(String value) {
    value.trim();
    value.toLowerCase();
    value.replace(" ", "");
    value.replace("_", "");
    value.replace("-", "");
    return value;
}

bool screen_from_token(const String& value, meshcore::ScreenId& screen) {
    const String token = normalize_command_token(value);
    if (token == "desktop") {
        screen = meshcore::ScreenId::Home;
        return true;
    }
    if (token == "chat") {
        screen = meshcore::ScreenId::Inbox;
        return true;
    }
    if (token == "message") {
        screen = meshcore::ScreenId::MessageView;
        return true;
    }
    if (token == "channeledit" || token == "channeleditor") {
        screen = meshcore::ScreenId::ChannelEditor;
        return true;
    }
    if (token == "radioadv" || token == "radioadvanced") {
        screen = meshcore::ScreenId::RadioAdvanced;
        return true;
    }
    if (token == "radiotune" || token == "radiotuning") {
        screen = meshcore::ScreenId::RadioTuning;
        return true;
    }
    if (token == "ble" || token == "bluetooth") {
        screen = meshcore::ScreenId::Ble;
        return true;
    }
    if (token == "diag") {
        screen = meshcore::ScreenId::Diagnostics;
        return true;
    }
    for (const auto candidate : meshcore::all_screens()) {
        if (normalize_command_token(meshcore::screen_title(candidate)) == token) {
            screen = candidate;
            return true;
        }
    }
    return false;
}

void print_hil_commands() {
    Serial.println("hil commands: hil ping, hil health, hil dump-state, ui screen, ui home, ui show <screen>, ui action <0-3>, ui scroll <delta>, ui key <text>, mesh inject-direct <sender> <text>, mesh inject-channel <channel> <sender> <text>, mesh inject-node <id> <name>, get radio, set radio <freq_mhz>,<bw_khz>,<sf>,<cr>, radio set <freq_khz> <bw_khz> <sf> <cr>, radio rx-reset, send-channel <channel> <text>, sys reboot");
}

void print_hil_state(const meshcore::AppSnapshot& snapshot, bool startup_complete) {
    const auto screen = meshcore::app_active_screen();
    Serial.printf("hil: state screen=%s startup=%d messages=%u nodes=%u channels=%u selected_message=%d selected_node=%d selected_channel=%d cad=%s cad_detected=%u cad_errors=%u last_event=%s\n",
                  meshcore::screen_title(screen),
                  startup_complete ? 1 : 0,
                  static_cast<unsigned>(snapshot.messages.size()),
                  static_cast<unsigned>(snapshot.nodes.size()),
                  static_cast<unsigned>(snapshot.channels.size()),
                  snapshot.state.selected_message,
                  snapshot.state.selected_node,
                  snapshot.state.selected_channel,
                  snapshot.state.radio_cad_status.c_str(),
                  snapshot.state.radio_cad_detected_count,
                  snapshot.state.radio_cad_error_count,
                  snapshot.state.last_event.c_str());
}

void apply_region_defaults(meshcore::AppState& state) {
    state.radio_frequency_khz = default_frequency_khz_for_region(state.region);
    if (state.region.find("915") != std::string::npos) {
        state.radio_bandwidth_hz = meshcore_915_default_bandwidth_hz;
        state.radio_spreading_factor = meshcore_915_default_spreading_factor;
    } else {
        state.radio_bandwidth_hz = 250000;
        state.radio_spreading_factor = 10;
    }
    state.radio_coding_rate = meshcore_default_coding_rate;
}

String default_node_id() {
#if defined(ARDUINO_ARCH_ESP32)
    const uint64_t mac = ESP.getEfuseMac();
    char id[12];
    std::snprintf(id, sizeof(id), "0x%04X", static_cast<unsigned>(mac & 0xffffU));
    return String(id);
#else
    return "0xTDECK";
#endif
}

meshcore::AppSnapshot make_hardware_snapshot() {
    meshcore::AppSnapshot snapshot;
    snapshot.state.device_name = "T-Deck Plus";
    snapshot.state.channel = "test";
    snapshot.state.region = default_region();
    apply_region_defaults(snapshot.state);
    snapshot.state.connected = false;
    snapshot.state.battery_percent = 0;
    snapshot.state.battery_mv = 0;
    snapshot.state.node_count = 0;
    snapshot.state.latitude = 0.0;
    snapshot.state.longitude = 0.0;
    snapshot.state.unread_count = 0;
    snapshot.state.selected_message = 0;
    snapshot.state.selected_node = 0;
    snapshot.state.selected_channel = 1;
    snapshot.state.map_zoom = 4;
    snapshot.state.gps_enabled = false;
    snapshot.state.gps_state = "off";
    snapshot.state.storage_writable = false;
    snapshot.state.storage_state = "not mounted";
    snapshot.state.compose_text.clear();
    snapshot.state.compose_recipient = "broadcast";
    snapshot.state.radio_state = "starting";
    snapshot.state.ble_enabled = true;
    snapshot.state.ble_connected = false;
    snapshot.state.ble_state = "pending";
    snapshot.state.last_event = "boot: hardware init";
    snapshot.state.firmware_version = "dev";
    snapshot.channels = {
        {"public", false, 0},
        {"test", true, 0},
        {"Ops", false, 0},
        {"Emergency", false, 0},
        {"Local", false, 0},
    };
    snapshot.channels[0].secret = public_channel_secret();
    snapshot.channels[1].secret = test_channel_secret();
    ensure_channel_defaults(snapshot.channels);
    snapshot.logs = {
        "boot: hardware init",
    };
    return snapshot;
}

}  // namespace

bool MeshService::begin() {
    Serial.println("boot: mesh begin");
    snapshot_ = make_hardware_snapshot();
    snapshot_.state.region = default_region();
    snapshot_.state.local_node_id = to_string(default_node_id());
    Serial.println("boot: board power");
    board_power_.begin();
    Serial.println("boot: battery");
    battery_.begin();
    Serial.println("boot: gps");
    gps_.begin();
    Serial.println("boot: audio");
    audio_.begin();
    Serial.println("boot: input");
    input_.begin();
    Serial.println("boot: settings");
    loadSettings();
    active_service = this;
    meshcore::app_set_action_sink(mesh_service_action_sink);
    snapshot_.state.radio_state = "starting";
    snapshot_.state.storage_state = "pending";
    snapshot_.state.last_event = "boot: UI ready";
    meshcore::app_ingest_service_snapshot(snapshot_);
    startup_stage_ = 0;
    Serial.println("boot: mesh lightweight begin complete");
    return true;
}

void MeshService::loop() {
    const uint32_t now = millis();
    if (last_input_poll_ms_ == 0 || now - last_input_poll_ms_ >= 30) {
        last_input_poll_ms_ = now;
        input_.poll();
    }
    char key = 0;
    while (input_.readKey(key)) {
        handleInputKey(key);
    }
    pollSerialConsole();
    if (!startupComplete()) {
        continueStartup();
        return;
    }
    processBleCommands();
    pollRadio(now);
    pollHardware(now);
    if (now - last_status_ms_ > 5000) {
        last_status_ms_ = now;
        snapshot_.state.last_event = "status: heartbeat";
        meshcore::app_ingest_service_snapshot(snapshot_);
    }
}

const meshcore::AppSnapshot& MeshService::snapshot() const {
    return snapshot_;
}

bool MeshService::startupComplete() const {
    return startup_stage_ >= 5;
}

void MeshService::continueStartup() {
    switch (startup_stage_) {
        case 0:
#if APP_ENABLE_SD_STORAGE
            Serial.println("boot: storage begin");
            storage_.begin();
            storage_started_ = true;
            snapshot_.state.storage_state = storage_.poll().state.c_str();
#else
            Serial.println("boot: storage disabled");
            storage_started_ = false;
            snapshot_.state.storage_state = "disabled";
#endif
            snapshot_.state.last_event = "boot: storage ready";
            ++startup_stage_;
            Serial.println("boot: storage stage done");
            return;
        case 1:
#if APP_ENABLE_SD_STORAGE
            Serial.println("boot: restore history");
            loadMessageHistory();
            loadNodeHistory();
#else
            Serial.println("boot: restore history skipped");
#endif
            snapshot_.state.last_event = "boot: history loaded";
            ++startup_stage_;
            Serial.println("boot: history stage done");
            return;
        case 2:
            Serial.println("boot: radio begin");
            radio_.setLocalNodeId(from_string(snapshot_.state.local_node_id));
            radio_.setChannels(snapshot_.channels);
            radio_.setNodes(snapshot_.nodes);
            radio_.begin(snapshot_.state);
            radio_started_ = true;
            snapshot_.state.radio_state = radio_.poll().state.c_str();
            snapshot_.state.last_event = "boot: radio ready";
            ++startup_stage_;
            Serial.println("boot: radio stage done");
            return;
        case 3:
            Serial.println("boot: meshcore core begin");
            core_.begin(snapshot_);
            core_started_ = true;
            syncCoreState();
            snapshot_.state.last_event = "boot: core ready";
            ++startup_stage_;
            Serial.println("boot: core stage done");
            return;
        case 4:
            if (snapshot_.state.ble_enabled) {
                Serial.println("boot: BLE companion begin");
                ble_.begin(snapshot_);
            } else {
                Serial.println("boot: BLE companion disabled");
            }
            Serial.println("boot: hardware poll begin");
            pollHardware(millis());
            Serial.println("boot: hardware poll done");
            snapshot_.state.last_event = "boot: hardware backend ready";
            ++startup_stage_;
            meshcore::app_ingest_service_snapshot(snapshot_);
            Serial.println("boot: MeshService started with hardware backend");
            return;
        default:
            startup_stage_ = 5;
            return;
    }
}

bool MeshService::handleUiCommand(meshcore::ActionCommand command,
                                  const meshcore::AppSnapshot& before,
                                  meshcore::AppSnapshot& after) {
    bool persist_settings = false;
    switch (command) {
        case meshcore::ActionCommand::SendMessage: {
            snapshot_.state.compose_recipient = before.state.compose_recipient;
            snapshot_.state.selected_channel = before.state.selected_channel;
            snapshot_.state.selected_node = before.state.selected_node;
            const String node_id = from_string(before.state.compose_recipient);
            const String body = from_string(before.state.compose_text.empty() ? "Ping" : before.state.compose_text);
            const bool sent = sendDirectMessage(node_id, body);
            if (sent && !after.messages.empty()) {
                after.messages.front().sender = after.state.local_node_id;
                after.messages.front().delivered = true;
                after.messages.front().status = "queued";
                if (persistMessage(after.messages.front())) {
                    after.messages.front().persisted = true;
                    after.state.persisted_message_count = snapshot_.state.persisted_message_count + 1;
                }
            }
            if (sent) {
                audio_.beep(1568, 35, after.state.audio_enabled);
            }
            after.state.radio_state = sent ? "tx queued" : "tx failed";
            after.state.last_event = sent ? "mesh: tx queued" : "mesh: tx failed";
            break;
        }
        case meshcore::ActionCommand::DeleteMessage: {
            if (!before.messages.empty()) {
                int index = before.state.selected_message;
                if (index < 0) {
                    index = 0;
                }
                if (index >= static_cast<int>(before.messages.size())) {
                    index = static_cast<int>(before.messages.size()) - 1;
                }
                if (persistDelete(from_string(before.messages[static_cast<std::size_t>(index)].id)) &&
                    after.state.persisted_message_count > 0) {
                    --after.state.persisted_message_count;
                }
            }
            break;
        }
        case meshcore::ActionCommand::ToggleRegion:
        case meshcore::ActionCommand::CycleTxPower:
        case meshcore::ActionCommand::CyclePathHash:
        case meshcore::ActionCommand::CycleFrequency:
        case meshcore::ActionCommand::EditFrequency:
        case meshcore::ActionCommand::CycleBandwidth:
        case meshcore::ActionCommand::EditBandwidth:
        case meshcore::ActionCommand::CycleSpreadingFactor:
        case meshcore::ActionCommand::EditSpreadingFactor:
        case meshcore::ActionCommand::CycleCodingRate:
        case meshcore::ActionCommand::EditCodingRate:
        case meshcore::ActionCommand::ToggleClientRepeat:
            radio_.configure(after.state);
            persist_settings = true;
            break;
        case meshcore::ActionCommand::CycleChannelSecret:
        case meshcore::ActionCommand::EditChannelName:
        case meshcore::ActionCommand::EditChannelSecret:
            radio_.setChannels(after.channels);
            persist_settings = true;
            break;
        case meshcore::ActionCommand::ToggleGps:
        case meshcore::ActionCommand::ToggleAudio:
        case meshcore::ActionCommand::JoinChannel:
        case meshcore::ActionCommand::LeaveChannel:
        case meshcore::ActionCommand::RoomLogin:
        case meshcore::ActionCommand::RemoteAdmin:
        case meshcore::ActionCommand::CycleTuning:
        case meshcore::ActionCommand::ToggleManualContacts:
        case meshcore::ActionCommand::CycleAutoAdd:
        case meshcore::ActionCommand::CycleAdvertPolicy:
        case meshcore::ActionCommand::EditDeviceName:
        case meshcore::ActionCommand::EditDevicePin:
        case meshcore::ActionCommand::EditLatitude:
        case meshcore::ActionCommand::EditLongitude:
        case meshcore::ActionCommand::ToggleBle: {
            if (after.state.ble_enabled) {
                after.state.ble_state = "starting";
                ble_.begin(after);
            } else {
                ble_.end();
            }
            const auto ble = ble_.status();
            after.state.ble_enabled = ble.enabled;
            after.state.ble_connected = ble.connected;
            after.state.ble_state = ble.state.c_str();
            after.state.ble_rx_frames = ble.rx_frames;
            after.state.ble_tx_frames = ble.tx_frames;
            persist_settings = true;
            break;
        }
        case meshcore::ActionCommand::SetDevicePin:
        case meshcore::ActionCommand::ExportPrivateKey:
        case meshcore::ActionCommand::ImportPrivateKey:
        case meshcore::ActionCommand::CycleFloodScope:
        case meshcore::ActionCommand::CycleCustomVar:
        case meshcore::ActionCommand::EditCustomVar:
            persist_settings = true;
            break;
        case meshcore::ActionCommand::ResetContactPath:
        case meshcore::ActionCommand::RemoveContact:
            radio_.setNodes(after.nodes);
            storage_.clearNodeRecords();
            for (const auto& node : after.nodes) {
                persistNode(node);
            }
            break;
        case meshcore::ActionCommand::ShareContact:
            after.state.last_event = "contact: share queued";
            break;
        case meshcore::ActionCommand::SendSelfAdvert: {
            const auto result = radio_.sendSelfAdvert(from_string(after.state.device_name),
                                                      after.state.advert_location_policy != 0 &&
                                                          after.state.latitude != 0.0 &&
                                                          after.state.longitude != 0.0,
                                                      after.state.latitude,
                                                      after.state.longitude,
                                                      currentEpochSeconds());
            after.state.radio_state = result.status.c_str();
            after.state.last_event = result.accepted ? "advert: self advert queued" : "advert: self advert failed";
            break;
        }
        case meshcore::ActionCommand::ScanNodes:
            after.state.radio_state = "scan complete";
            break;
        case meshcore::ActionCommand::ScanRadio:
            snapshot_ = after;
            if (radio_scan_active_) {
                stopRadioScan(true, "stopped");
                snapshot_.state.last_event = "radio: receive scan stopped";
            } else {
                startRadioScan();
            }
            after = snapshot_;
            break;
        case meshcore::ActionCommand::RunCadScan: {
            unsigned detected = 0;
            unsigned errors = 0;
            RadioCadResult last;
            for (int i = 0; i < 8; ++i) {
                last = radio_.scanChannelActivity();
                if (last.detected) {
                    ++detected;
                }
                if (last.error) {
                    ++errors;
                }
                delay(5);
            }
            after.state.radio_cad_detected_count = detected;
            after.state.radio_cad_error_count = errors;
            after.state.radio_cad_status = "last " + std::to_string(last.result) +
                " irq 0x" + hex_word(last.irq_flags);
            after.state.radio_scan_status = "cad " + std::to_string(detected) +
                "/" + std::to_string(errors);
            after.state.last_event = "radio: cad checked";
            break;
        }
        case meshcore::ActionCommand::PingNode:
            after.state.radio_state = "ping queued";
            break;
        case meshcore::ActionCommand::SyncClock:
            after.state.last_event = "server: clock sync sent";
            break;
        case meshcore::ActionCommand::RequestStatus:
        case meshcore::ActionCommand::TracePath:
        case meshcore::ActionCommand::DiscoverPath:
        case meshcore::ActionCommand::SendTelemetry:
            after.state.last_event = "tools: request queued";
            break;
        case meshcore::ActionCommand::RebootDevice:
            after.state.last_event = "admin: reboot not armed";
            break;
        case meshcore::ActionCommand::FactoryReset:
            after.state.last_event = "admin: reset not armed";
            break;
        default:
            break;
    }

    snapshot_ = after;
    if (core_started_) {
        core_.ingestSnapshot(snapshot_);
        syncCoreState();
    }
    appendLog(from_string(after.state.last_event));
    if (persist_settings) {
        radio_.setChannels(snapshot_.channels);
        saveSettings();
    }
    return true;
}

bool MeshService::sendDirectMessage(const String& node_id, const String& text) {
    RadioTxResult result;
    if (is_broadcast_target(node_id)) {
        const uint8_t channel_index = static_cast<uint8_t>(std::min<int>(
            std::max<int>(snapshot_.state.selected_channel, 0),
            static_cast<int>(snapshot_.channels.empty() ? 0 : snapshot_.channels.size() - 1)));
        result = radio_.sendChannelMessage(channel_index,
                                           from_string(snapshot_.state.device_name),
                                           text,
                                           millis() / 1000);
    } else {
        const auto* contact = find_node(snapshot_.nodes, node_id);
        if (contact != nullptr && has_public_key(contact->public_key)) {
            result = radio_.sendContactMessage(*contact, text, millis() / 1000);
        } else {
            result = radio_.sendDirect(node_id, text);
        }
    }
    Serial.printf("MeshService TX direct node=%s text=%s result=%s\n",
                  node_id.c_str(), text.c_str(), result.status.c_str());
    return result.accepted;
}

std::vector<NodeInfo> MeshService::getKnownNodes() const {
    return snapshot_.nodes;
}

std::vector<MeshMessage> MeshService::getRecentMessages() const {
    return snapshot_.messages;
}

void MeshService::loadSettings() {
#if defined(ARDUINO_ARCH_ESP32)
    bool settings_migrated = false;
    Preferences prefs;
    if (prefs.begin("meshcore-ui", false)) {
        const uint32_t settings_schema = prefs.getUInt("schemav", 0);
        snapshot_.state.region = to_string(prefs.getString("region", snapshot_.state.region.c_str()));
        snapshot_.state.device_name = to_string(prefs.getString("name", snapshot_.state.device_name.c_str()));
        snapshot_.state.local_node_id = to_string(prefs.getString("nodeid", snapshot_.state.local_node_id.c_str()));
        snapshot_.state.channel = to_string(prefs.getString("channel", snapshot_.state.channel.c_str()));
        snapshot_.state.selected_channel = static_cast<int>(prefs.getUInt("selch", snapshot_.state.selected_channel));
        snapshot_.state.radio_frequency_khz = prefs.getUInt("freqkhz", default_frequency_khz_for_region(snapshot_.state.region));
        snapshot_.state.radio_bandwidth_hz = prefs.getUInt("bwhz", snapshot_.state.radio_bandwidth_hz);
        snapshot_.state.radio_spreading_factor = prefs.getUInt("sf", snapshot_.state.radio_spreading_factor);
        if (settings_schema < settings_schema_version &&
            snapshot_.state.radio_frequency_khz >= 902000 &&
            snapshot_.state.radio_frequency_khz <= 928000 &&
            snapshot_.state.radio_spreading_factor == 11) {
            snapshot_.state.radio_spreading_factor = 10;
            settings_migrated = true;
        }
        snapshot_.state.radio_coding_rate = prefs.getUInt("cr", snapshot_.state.radio_coding_rate);
        if (settings_schema < settings_schema_version &&
            snapshot_.state.region.find("915") != std::string::npos &&
            snapshot_.state.radio_frequency_khz == 915000 &&
            snapshot_.state.radio_bandwidth_hz == 250000 &&
            snapshot_.state.radio_spreading_factor == 10 &&
            snapshot_.state.radio_coding_rate == 5) {
            apply_region_defaults(snapshot_.state);
            settings_migrated = true;
        }
        if (settings_schema < 6 &&
            snapshot_.state.region.find("915") != std::string::npos &&
            snapshot_.state.radio_frequency_khz == 918000 &&
            snapshot_.state.radio_bandwidth_hz == 62500 &&
            snapshot_.state.radio_spreading_factor == 8 &&
            snapshot_.state.radio_coding_rate == 5) {
            apply_region_defaults(snapshot_.state);
            settings_migrated = true;
        }
        snapshot_.state.client_repeat = prefs.getBool("repeat", snapshot_.state.client_repeat);
        snapshot_.state.manual_add_contacts = prefs.getBool("manual", snapshot_.state.manual_add_contacts);
        snapshot_.state.advert_location_policy = prefs.getUInt("advloc", snapshot_.state.advert_location_policy);
        snapshot_.state.multi_acks = prefs.getUInt("multiack", snapshot_.state.multi_acks);
        snapshot_.state.rx_delay_base_ms = prefs.getUInt("rxdelay", snapshot_.state.rx_delay_base_ms);
        snapshot_.state.airtime_factor_ms = prefs.getUInt("airtime", snapshot_.state.airtime_factor_ms);
        snapshot_.state.autoadd_config = prefs.getUInt("autoadd", snapshot_.state.autoadd_config);
        snapshot_.state.autoadd_max_hops = prefs.getUInt("addhops", snapshot_.state.autoadd_max_hops);
        snapshot_.state.default_flood_scope = prefs.getUInt("flood", snapshot_.state.default_flood_scope);
        snapshot_.state.flood_scope_key = prefs.getUInt("floodkey", snapshot_.state.flood_scope_key);
        snapshot_.state.default_flood_name = to_string(prefs.getString("floodn", snapshot_.state.default_flood_name.c_str()));
        if (prefs.getBytesLength("floods") == snapshot_.state.default_flood_secret.size()) {
            prefs.getBytes("floods",
                           snapshot_.state.default_flood_secret.data(),
                           snapshot_.state.default_flood_secret.size());
        }
        snapshot_.state.custom_var_index = prefs.getUInt("customi", snapshot_.state.custom_var_index);
        snapshot_.state.custom_var_value = prefs.getUInt("customv", snapshot_.state.custom_var_value);
        snapshot_.state.device_pin_set = prefs.getBool("pinset", snapshot_.state.device_pin_set);
        snapshot_.state.tx_power_dbm = prefs.getInt("txpwr", snapshot_.state.tx_power_dbm);
        snapshot_.state.path_hash_mode = prefs.getInt("pathhash", snapshot_.state.path_hash_mode);
        if (settings_schema < 4 && snapshot_.state.path_hash_mode == 1) {
            snapshot_.state.path_hash_mode = 0;
            settings_migrated = true;
        }
        snapshot_.state.gps_enabled = prefs.getBool("gps", snapshot_.state.gps_enabled);
        snapshot_.state.audio_enabled = prefs.getBool("audio", snapshot_.state.audio_enabled);
        const bool saved_ble_enabled = prefs.getBool("ble", true);
        snapshot_.state.ble_enabled = true;
        if (!saved_ble_enabled) {
            Serial.println("settings: saved BLE off ignored; companion BLE defaults on");
        }
        snapshot_.state.room_logged_in = prefs.getBool("room", snapshot_.state.room_logged_in);
        snapshot_.state.repeater_admin = prefs.getBool("admin", snapshot_.state.repeater_admin);

        const uint32_t saved_channel_count = prefs.getUInt("chcount", snapshot_.channels.size());
        const std::size_t channel_count = std::min<std::size_t>(
            max_meshcore_channels,
            std::max<std::size_t>(1, static_cast<std::size_t>(saved_channel_count)));
        snapshot_.channels.resize(channel_count);
        for (std::size_t i = 0; i < snapshot_.channels.size(); ++i) {
            char key[12];
            std::snprintf(key, sizeof(key), "ch%un", static_cast<unsigned>(i));
            snapshot_.channels[i].name = to_string(prefs.getString(key, snapshot_.channels[i].name.c_str()));
            std::snprintf(key, sizeof(key), "ch%ua", static_cast<unsigned>(i));
            snapshot_.channels[i].active = prefs.getBool(key, snapshot_.channels[i].active);
            std::snprintf(key, sizeof(key), "ch%us", static_cast<unsigned>(i));
            if (prefs.getBytesLength(key) == snapshot_.channels[i].secret.size()) {
                prefs.getBytes(key, snapshot_.channels[i].secret.data(), snapshot_.channels[i].secret.size());
            }
        }
        ensure_channel_defaults(snapshot_.channels);
        if (snapshot_.state.selected_channel < 0 ||
            snapshot_.state.selected_channel >= static_cast<int>(snapshot_.channels.size())) {
            snapshot_.state.selected_channel = 0;
        }
        if (!snapshot_.state.channel.empty()) {
            for (std::size_t i = 0; i < snapshot_.channels.size(); ++i) {
                if (snapshot_.channels[i].name == snapshot_.state.channel) {
                    snapshot_.state.selected_channel = static_cast<int>(i);
                    break;
                }
            }
        }
        if (snapshot_.state.selected_channel >= 0 &&
            snapshot_.state.selected_channel < static_cast<int>(snapshot_.channels.size())) {
            select_channel(snapshot_, snapshot_.state.selected_channel);
        }
        if (settings_schema < settings_schema_version &&
            (snapshot_.state.channel == "public" || snapshot_.state.channel == "Public")) {
            select_test_channel(snapshot_);
            settings_migrated = true;
        }
        prefs.end();
    }
    if (settings_migrated) {
        saveSettings();
    }
#endif
}

void MeshService::saveSettings() const {
#if defined(ARDUINO_ARCH_ESP32)
    Preferences prefs;
    if (prefs.begin("meshcore-ui", false)) {
        prefs.putUInt("schemav", settings_schema_version);
        prefs.putString("region", snapshot_.state.region.c_str());
        prefs.putString("name", snapshot_.state.device_name.c_str());
        prefs.putString("nodeid", snapshot_.state.local_node_id.c_str());
        prefs.putString("channel", snapshot_.state.channel.c_str());
        prefs.putUInt("selch", static_cast<unsigned>(std::max(snapshot_.state.selected_channel, 0)));
        prefs.putUInt("freqkhz", snapshot_.state.radio_frequency_khz);
        prefs.putUInt("bwhz", snapshot_.state.radio_bandwidth_hz);
        prefs.putUInt("sf", snapshot_.state.radio_spreading_factor);
        prefs.putUInt("cr", snapshot_.state.radio_coding_rate);
        prefs.putBool("repeat", snapshot_.state.client_repeat);
        prefs.putBool("manual", snapshot_.state.manual_add_contacts);
        prefs.putUInt("advloc", snapshot_.state.advert_location_policy);
        prefs.putUInt("multiack", snapshot_.state.multi_acks);
        prefs.putUInt("rxdelay", snapshot_.state.rx_delay_base_ms);
        prefs.putUInt("airtime", snapshot_.state.airtime_factor_ms);
        prefs.putUInt("autoadd", snapshot_.state.autoadd_config);
        prefs.putUInt("addhops", snapshot_.state.autoadd_max_hops);
        prefs.putUInt("flood", snapshot_.state.default_flood_scope);
        prefs.putUInt("floodkey", snapshot_.state.flood_scope_key);
        prefs.putString("floodn", snapshot_.state.default_flood_name.c_str());
        prefs.putBytes("floods",
                       snapshot_.state.default_flood_secret.data(),
                       snapshot_.state.default_flood_secret.size());
        prefs.putUInt("customi", snapshot_.state.custom_var_index);
        prefs.putUInt("customv", snapshot_.state.custom_var_value);
        prefs.putBool("pinset", snapshot_.state.device_pin_set);
        prefs.putInt("txpwr", snapshot_.state.tx_power_dbm);
        prefs.putInt("pathhash", snapshot_.state.path_hash_mode);
        prefs.putBool("gps", snapshot_.state.gps_enabled);
        prefs.putBool("audio", snapshot_.state.audio_enabled);
        prefs.putBool("ble", snapshot_.state.ble_enabled);
        prefs.putBool("room", snapshot_.state.room_logged_in);
        prefs.putBool("admin", snapshot_.state.repeater_admin);
        prefs.putUInt("chcount", static_cast<unsigned>(std::min<std::size_t>(snapshot_.channels.size(), max_meshcore_channels)));
        for (std::size_t i = 0; i < snapshot_.channels.size() && i < max_meshcore_channels; ++i) {
            char key[12];
            std::snprintf(key, sizeof(key), "ch%un", static_cast<unsigned>(i));
            prefs.putString(key, snapshot_.channels[i].name.c_str());
            std::snprintf(key, sizeof(key), "ch%ua", static_cast<unsigned>(i));
            prefs.putBool(key, snapshot_.channels[i].active);
            std::snprintf(key, sizeof(key), "ch%us", static_cast<unsigned>(i));
            prefs.putBytes(key, snapshot_.channels[i].secret.data(), snapshot_.channels[i].secret.size());
        }
        prefs.end();
    }
#endif
}

void MeshService::appendLog(const String& value) {
    if (value.length() == 0) {
        return;
    }
    snapshot_.logs.insert(snapshot_.logs.begin(), to_string(value));
    if (snapshot_.logs.size() > 8) {
        snapshot_.logs.pop_back();
    }
    storage_.appendLog(value);
}

void MeshService::loadMessageHistory() {
    const auto records = storage_.readMessageRecords(8);
    if (records.empty()) {
        snapshot_.state.persisted_message_count = 0;
        return;
    }

    std::vector<String> deleted_ids;
    std::vector<MeshMessage> loaded;
    for (const auto& record : records) {
        if (record.startsWith("DEL|")) {
            const String id = record_field(record, 1);
            if (id.length() > 0) {
                deleted_ids.push_back(id);
                erase_message_by_id(loaded, id);
            }
            continue;
        }
        MeshMessage message;
        if (parse_message_record(record, message) && !id_in_list(deleted_ids, from_string(message.id))) {
            loaded.push_back(message);
        }
    }
    snapshot_.messages.clear();
    for (const auto& message : loaded) {
        snapshot_.messages.insert(snapshot_.messages.begin(), message);
    }
    snapshot_.state.selected_message = 0;
    snapshot_.state.persisted_message_count = static_cast<unsigned>(snapshot_.messages.size());
    snapshot_.state.last_event = "storage: restored messages";
}

void MeshService::loadNodeHistory() {
    const auto records = storage_.readNodeRecords(64);
    if (records.empty()) {
        snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
        radio_.setNodes(snapshot_.nodes);
        return;
    }

    for (const auto& record : records) {
        NodeInfo node;
        if (!parse_node_record(record, node)) {
            continue;
        }
        auto existing = std::find_if(snapshot_.nodes.begin(), snapshot_.nodes.end(),
                                     [&](const NodeInfo& item) {
                                         return item.short_id == node.short_id;
                                     });
        if (existing == snapshot_.nodes.end()) {
            snapshot_.nodes.push_back(node);
        } else {
            *existing = node;
        }
    }
    snapshot_.state.node_count = static_cast<int>(snapshot_.nodes.size());
    snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
    radio_.setNodes(snapshot_.nodes);
    snapshot_.state.last_event = "storage: restored nodes";
}

void MeshService::addMessage(const MeshMessage& message, bool persist) {
    MeshMessage stored = message;
    stored.persisted = persist;
    if (stored.status.empty() || stored.status == "new") {
        stored.status = stored.outgoing ? "queued" : "received";
    }
    snapshot_.messages.insert(snapshot_.messages.begin(), stored);
    if (snapshot_.messages.size() > 8) {
        snapshot_.messages.pop_back();
    }
    snapshot_.state.selected_message = 0;
    if (persist && persistMessage(stored)) {
        ++snapshot_.state.persisted_message_count;
    }
    if (!stored.outgoing) {
        ble_.notifyMessagesWaiting();
    }
}

bool MeshService::persistMessage(const MeshMessage& message) {
    return storage_.appendMessageRecord(message_record(message));
}

bool MeshService::persistDelete(const String& message_id) {
    if (message_id.length() == 0) {
        return false;
    }
    return storage_.appendMessageRecord(delete_record(message_id));
}

void MeshService::handleRadioPayload(const String& payload) {
    if (payload.startsWith("ADV1|")) {
        NodeInfo contact;
        contact.short_id = to_string(record_field(payload, 1));
        contact.name = to_string(record_field(payload, 7));
        if (contact.name.empty()) {
            contact.name = contact.short_id;
        }
        contact.lastmod = static_cast<unsigned>(record_field(payload, 2).toInt());
        contact.latitude = record_field(payload, 3).toDouble();
        contact.longitude = record_field(payload, 4).toDouble();
        contact.has_position = contact.latitude != 0.0 && contact.longitude != 0.0;
        parse_hex_bytes(record_field(payload, 5), contact.public_key);
        const String type = record_field(payload, 6);
        if (type.length() > 0) {
            contact.contact_type = static_cast<uint8_t>(std::max<int>(0, std::min<int>(255, type.toInt())));
        }
        const auto radio = radio_.poll();
        upsertContact(contact, radio.rssi, radio.snr, true);
        snapshot_.state.last_event = "mesh: advert " + contact.name;
        appendLog(from_string(snapshot_.state.last_event));
        meshcore::app_ingest_service_snapshot(snapshot_);
        return;
    }

    if (payload.startsWith("PATH1|")) {
        const String short_id = record_field(payload, 1);
        const String path_len = record_field(payload, 2);
        const String path_hex = record_field(payload, 3);
        NodeInfo* node = find_node(snapshot_.nodes, short_id);
        if (node != nullptr && path_len.length() > 0) {
            node->out_path_len = static_cast<uint8_t>(std::max<int>(0, std::min<int>(255, path_len.toInt())));
            if (node->out_path_len != meshcore::NodeInfo::out_path_unknown) {
                const std::size_t bytes = path_byte_len(node->out_path_len);
                node->out_path = {};
                parse_hex_bytes(path_hex, node->out_path, bytes);
            }
            node->last_seen_seconds = 0;
            node->lastmod = millis() / 1000;
            persistNode(*node);
            radio_.setNodes(snapshot_.nodes);
            snapshot_.state.last_event = "mesh: path " + node->name;
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
        }
        return;
    }

    MeshMessage message;
    if (!parse_radio_payload(payload,
                             from_string(snapshot_.state.local_node_id),
                             millis() / 1000,
                             next_incoming_id_++,
                             message)) {
        return;
    }
    const auto radio = radio_.poll();
    upsertNode(from_string(message.sender), node_name_for_sender(from_string(message.sender)), radio.rssi, radio.snr, true);
    if (message.subject == "Position") {
        const int split = from_string(message.body).indexOf(',');
        if (split > 0) {
            const String body = from_string(message.body);
            updateNodePosition(from_string(message.sender),
                               body.substring(0, split).toDouble(),
                               body.substring(split + 1).toDouble(),
                               true);
        }
    }
    addMessage(message, true);
    select_conversation_for_message(snapshot_, message);
    snapshot_.state.last_event = "mesh: rx " + message.sender;
    audio_.beep(988, 50, snapshot_.state.audio_enabled);
    appendLog(from_string(snapshot_.state.last_event));
}

void MeshService::upsertContact(const NodeInfo& contact, int rssi, float snr, bool persist) {
    if (!has_public_key(contact.public_key) || contact.short_id == snapshot_.state.local_node_id) {
        return;
    }

    NodeInfo merged = contact;
    if (merged.short_id.empty()) {
        merged.short_id = to_string(short_id_from_public_key(merged.public_key));
    }
    if (merged.name.empty()) {
        merged.name = merged.short_id;
    }
    merged.rssi = rssi;
    merged.snr = snr;
    merged.last_seen_seconds = 0;

    NodeInfo* existing = find_node_by_key(snapshot_.nodes, merged.public_key);
    if (existing == nullptr) {
        existing = find_node(snapshot_.nodes, from_string(merged.short_id));
    }

    if (existing == nullptr) {
        snapshot_.nodes.insert(snapshot_.nodes.begin(), merged);
        if (snapshot_.nodes.size() > 16) {
            snapshot_.nodes.pop_back();
        }
        existing = snapshot_.nodes.empty() ? nullptr : &snapshot_.nodes.front();
    } else {
        *existing = merged;
    }

    snapshot_.state.node_count = static_cast<int>(snapshot_.nodes.size());
    snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
    radio_.setNodes(snapshot_.nodes);
    if (persist && existing != nullptr) {
        persistNode(*existing);
    }
}

void MeshService::upsertNode(const String& short_id, const String& name, int rssi, float snr, bool persist) {
    if (short_id.length() == 0 || short_id == from_string(snapshot_.state.local_node_id)) {
        return;
    }
    auto existing = std::find_if(snapshot_.nodes.begin(), snapshot_.nodes.end(),
                                 [&](const NodeInfo& item) {
                                     return item.short_id == to_string(short_id);
                                 });
    bool created = false;
    if (existing == snapshot_.nodes.end()) {
        snapshot_.nodes.insert(snapshot_.nodes.begin(),
                               {to_string(name.length() > 0 ? name : short_id), to_string(short_id), rssi, snr, 0, false, 0.0, 0.0});
        if (snapshot_.nodes.size() > 16) {
            snapshot_.nodes.pop_back();
        }
        created = true;
    } else {
        existing->name = to_string(name.length() > 0 ? name : short_id);
        existing->rssi = rssi;
        existing->snr = snr;
        existing->last_seen_seconds = 0;
    }
    snapshot_.state.node_count = static_cast<int>(snapshot_.nodes.size());
    snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
    radio_.setNodes(snapshot_.nodes);
    if (persist) {
        const auto& node = created ? snapshot_.nodes.front() : *existing;
        persistNode(node);
    }
}

bool MeshService::persistNode(const NodeInfo& node) {
    return storage_.appendNodeRecord(node_record(node));
}

void MeshService::updateNodePosition(const String& short_id, double latitude, double longitude, bool persist) {
    if (short_id.length() == 0 || latitude == 0.0 || longitude == 0.0) {
        return;
    }
    auto existing = std::find_if(snapshot_.nodes.begin(), snapshot_.nodes.end(),
                                 [&](const NodeInfo& item) {
                                     return item.short_id == to_string(short_id);
                                 });
    if (existing == snapshot_.nodes.end()) {
        snapshot_.nodes.insert(snapshot_.nodes.begin(),
                               {to_string(node_name_for_sender(short_id)), to_string(short_id), -90, 0.0f, 0, true, latitude, longitude});
        if (snapshot_.nodes.size() > 16) {
            snapshot_.nodes.pop_back();
        }
        existing = snapshot_.nodes.begin();
    } else {
        existing->has_position = true;
        existing->latitude = latitude;
        existing->longitude = longitude;
        existing->last_seen_seconds = 0;
    }
    snapshot_.state.node_count = static_cast<int>(snapshot_.nodes.size());
    snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
    radio_.setNodes(snapshot_.nodes);
    if (persist) {
        persistNode(*existing);
    }
}

void MeshService::pollSerialConsole() {
    while (Serial.available() > 0) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r' || ch == '\n') {
            if (serial_buffer_.length() > 0) {
                handleSerialCommand(serial_buffer_);
            }
            serial_buffer_ = "";
            continue;
        }
        if (serial_buffer_.length() < 160) {
            serial_buffer_ += ch;
        }
    }
}

void MeshService::handleSerialCommand(const String& command) {
    const String trimmed = command;
    if (trimmed.length() == 0) {
        return;
    }

    if (trimmed == "help") {
        Serial.println("commands: help, status, identity, logs, messages, nodes, channels, channel set <index> <name> [32hex|auto], channel clear <index>, channel select <index|name>, clear-messages, clear-nodes, name <text>, node <id>, rename-node <id> <name>, region, power <dbm>, path, path <1-3>, gps, audio, ble, ble on, ble off, ble restart, ble test app, ble test query, ble test contacts, ble test sync, room, admin, wifi, wifi set <ssid> <password>, wifi start, wifi restart, wifi clear, radio defaults, radio set <freq> <bw> <sf> <cr>, radio scan, radio scan stop, radio listen [seconds], radio last, radio rf-switch <0|1>, radio cad [count], radio cad scan [count], send <node> <text>, send-channel <channel> <text>, send-pos <node>, hil commands");
        return;
    }
    if (trimmed == "ble" || trimmed == "ble status") {
        const auto ble = ble_.status();
        Serial.printf("ble enabled=%d started=%d connected=%d state=%s rx=%u tx=%u queued=%u conn=%u disconn=%u auth_ok=%u auth_fail=%u mtu=%u contacts_total=%u last_rx=0x%02x/%u last_tx=0x%02x/%u\n",
                      ble.enabled,
                      ble.started,
                      ble.connected,
                      ble.state.c_str(),
                      ble.rx_frames,
                      ble.tx_frames,
                      ble.tx_queued,
                      ble.connect_events,
                      ble.disconnect_events,
                      ble.auth_success,
                      ble.auth_fail,
                      static_cast<unsigned>(ble.mtu),
                      ble.last_contacts_total,
                      static_cast<unsigned>(ble.last_rx_type),
                      ble.last_rx_len,
                      static_cast<unsigned>(ble.last_tx_type),
                      ble.last_tx_len);
        return;
    }
    if (trimmed == "ble on" || trimmed == "ble start") {
        snapshot_.state.ble_enabled = true;
        snapshot_.state.ble_state = "starting";
        ble_.begin(snapshot_);
        const auto ble = ble_.status();
        snapshot_.state.ble_enabled = ble.enabled;
        snapshot_.state.ble_connected = ble.connected;
        snapshot_.state.ble_state = ble.state.c_str();
        snapshot_.state.ble_rx_frames = ble.rx_frames;
        snapshot_.state.ble_tx_frames = ble.tx_frames;
        saveSettings();
        snapshot_.state.last_event = "serial: ble on";
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("ble enabled=%d started=%d connected=%d state=%s\n",
                      ble.enabled,
                      ble.started,
                      ble.connected,
                      ble.state.c_str());
        return;
    }
    if (trimmed == "ble off" || trimmed == "ble stop") {
        ble_.end();
        const auto ble = ble_.status();
        snapshot_.state.ble_enabled = false;
        snapshot_.state.ble_connected = false;
        snapshot_.state.ble_state = ble.state.c_str();
        snapshot_.state.ble_rx_frames = ble.rx_frames;
        snapshot_.state.ble_tx_frames = ble.tx_frames;
        saveSettings();
        snapshot_.state.last_event = "serial: ble off";
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.println("ble disabled");
        return;
    }
    if (trimmed == "ble restart") {
        ble_.end();
        snapshot_.state.ble_enabled = true;
        snapshot_.state.ble_state = "starting";
        ble_.begin(snapshot_);
        const auto ble = ble_.status();
        snapshot_.state.ble_enabled = ble.enabled;
        snapshot_.state.ble_connected = ble.connected;
        snapshot_.state.ble_state = ble.state.c_str();
        snapshot_.state.ble_rx_frames = ble.rx_frames;
        snapshot_.state.ble_tx_frames = ble.tx_frames;
        saveSettings();
        snapshot_.state.last_event = "serial: ble restarted";
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("ble enabled=%d started=%d connected=%d state=%s\n",
                      ble.enabled,
                      ble.started,
                      ble.connected,
                      ble.state.c_str());
        return;
    }
    if (trimmed == "ble test app" || trimmed == "ble test query" || trimmed == "ble test sync" ||
        trimmed == "ble test contacts") {
        std::vector<uint8_t> frame;
        if (trimmed == "ble test app") {
            static constexpr uint8_t app_frame[] = {0x01, 0, 0, 0, 0, 0, 0, 0, 'C', 'o', 'd', 'e', 'x'};
            frame.assign(app_frame, app_frame + sizeof(app_frame));
        } else if (trimmed == "ble test query") {
            frame = {0x16, 0x0b};
        } else if (trimmed == "ble test contacts") {
            frame = {0x04};
        } else {
            frame = {0x0a};
        }
        ble_.enqueueWrite(frame.data(), frame.size());
        processBleCommands();
        ble_.loop();
        const auto ble = ble_.status();
        snapshot_.state.ble_connected = ble.connected;
        snapshot_.state.ble_state = ble.state.c_str();
        snapshot_.state.ble_rx_frames = ble.rx_frames;
        snapshot_.state.ble_tx_frames = ble.tx_frames;
        meshcore::app_ingest_service_snapshot(snapshot_);
        String detail;
        if (trimmed == "ble test sync") {
            if (ble.last_tx_type == 0x0a) {
                detail = " sync_resp=empty";
            } else if (ble.last_tx_type == 0x10 || ble.last_tx_type == 0x11) {
                detail = " sync_resp=message";
            } else {
                detail = " sync_resp=unexpected";
            }
        }
        Serial.printf("ble test ok state=%s rx=%u tx=%u queued=%u contacts_total=%u last_rx=0x%02x/%u last_tx=0x%02x/%u%s\n",
                      ble.state.c_str(),
                      ble.rx_frames,
                      ble.tx_frames,
                      ble.tx_queued,
                      ble.last_contacts_total,
                      static_cast<unsigned>(ble.last_rx_type),
                      ble.last_rx_len,
                      static_cast<unsigned>(ble.last_tx_type),
                      ble.last_tx_len,
                      detail.c_str());
        return;
    }
    if (trimmed == "wifi" || trimmed == "wifi status") {
        Serial.println(wifi_ota_status());
        return;
    }
    if (trimmed.startsWith("wifi set ")) {
        const int rest = String("wifi set ").length();
        const int split = trimmed.indexOf(' ', rest);
        if (split <= rest || split >= trimmed.length() - 1) {
            Serial.println("usage: wifi set <ssid> <password>");
            return;
        }
        const String ssid = trimmed.substring(rest, split);
        const String password = trimmed.substring(split + 1);
        if (!wifi_ota_save_credentials(ssid, password)) {
            Serial.println("wifi ota: failed to save credentials");
            return;
        }
        Serial.println("wifi ota: credentials saved");
        wifi_ota_start();
        return;
    }
    if (trimmed.startsWith("wifi host ")) {
        const String hostname = trimmed.substring(String("wifi host ").length());
        if (hostname.length() == 0) {
            Serial.println("usage: wifi host <name>");
            return;
        }
        if (!wifi_ota_save_hostname(hostname)) {
            Serial.println("wifi ota: failed to save hostname");
            return;
        }
        Serial.println("wifi ota: hostname saved");
        wifi_ota_start();
        return;
    }
    if (trimmed == "wifi start") {
        wifi_ota_start();
        return;
    }
    if (trimmed == "wifi restart") {
        wifi_ota_restart();
        return;
    }
    if (trimmed == "wifi stop") {
        wifi_ota_stop();
        return;
    }
    if (trimmed == "wifi clear") {
        wifi_ota_clear_credentials();
        Serial.println("wifi ota: credentials cleared");
        return;
    }
    if (trimmed == "hil" || trimmed == "hil commands") {
        print_hil_commands();
        return;
    }
    if (trimmed == "hil ping") {
        Serial.printf("hil: pong app=%s version=%s uptime_ms=%lu heap=%u\n",
                      APP_NAME,
                      APP_VERSION,
                      static_cast<unsigned long>(millis()),
                      ESP.getFreeHeap());
        return;
    }
    if (trimmed == "hil health" || trimmed == "sys health") {
        const auto ble = ble_.status();
        Serial.printf("hil: health startup=%d screen=%s core=%s radio=%s ble_enabled=%d ble_started=%d ble_connected=%d heap=%u psram_free=%u psram_total=%u rx=%u tx=%u messages=%u nodes=%u channels=%u uptime=%us\n",
                      startupComplete() ? 1 : 0,
                      meshcore::screen_title(meshcore::app_active_screen()),
                      core_.upstreamAvailable() ? "upstream" : "shim",
                      snapshot_.state.radio_state.c_str(),
                      ble.enabled,
                      ble.started,
                      ble.connected,
                      snapshot_.state.heap_free_bytes,
                      snapshot_.state.psram_free_bytes,
                      snapshot_.state.psram_total_bytes,
                      snapshot_.state.packet_rx_count,
                      snapshot_.state.packet_tx_count,
                      static_cast<unsigned>(snapshot_.messages.size()),
                      static_cast<unsigned>(snapshot_.nodes.size()),
                      static_cast<unsigned>(snapshot_.channels.size()),
                      snapshot_.state.uptime_seconds);
        return;
    }
    if (trimmed == "hil dump-state") {
        print_hil_state(snapshot_, startupComplete());
        return;
    }
    if (trimmed == "ui screen") {
        Serial.printf("hil: screen=%s\n", meshcore::screen_title(meshcore::app_active_screen()));
        return;
    }
    if (trimmed == "ui home" || trimmed == "ui close") {
        meshcore::ui_request_show(meshcore::ScreenId::Home);
        Serial.println("hil: ui scheduled=Home");
        return;
    }
    if (trimmed.startsWith("ui show ")) {
        meshcore::ScreenId screen = meshcore::ScreenId::Home;
        if (!screen_from_token(trimmed.substring(String("ui show ").length()), screen)) {
            Serial.println("hil: error unknown screen");
            return;
        }
        meshcore::ui_request_show(screen);
        Serial.printf("hil: ui scheduled=%s\n", meshcore::screen_title(screen));
        return;
    }
    if (trimmed.startsWith("ui action ")) {
        const int action_index = trimmed.substring(String("ui action ").length()).toInt();
        const auto screen = meshcore::app_active_screen();
        const auto actions = meshcore::screen_actions(screen);
        if (action_index < 0 || action_index >= static_cast<int>(actions.size())) {
            Serial.println("hil: error action index");
            return;
        }
        meshcore::app_handle_action(screen, actions[static_cast<std::size_t>(action_index)]);
        snapshot_ = meshcore::app_snapshot();
        meshcore::ui_show(meshcore::app_active_screen());
        Serial.printf("hil: ui action=%d screen=%s event=%s\n",
                      action_index,
                      meshcore::screen_title(meshcore::app_active_screen()),
                      snapshot_.state.last_event.c_str());
        return;
    }
    if (trimmed.startsWith("ui scroll ")) {
        const int delta = trimmed.substring(String("ui scroll ").length()).toInt();
        const auto screen = meshcore::app_active_screen();
        meshcore::app_scroll_selection(screen, delta == 0 ? 1 : delta);
        snapshot_ = meshcore::app_snapshot();
        meshcore::ui_show(screen);
        Serial.printf("hil: ui scrolled screen=%s delta=%d\n", meshcore::screen_title(screen), delta == 0 ? 1 : delta);
        return;
    }
    if (trimmed.startsWith("ui key ")) {
        const String keys = trimmed.substring(String("ui key ").length());
        for (int i = 0; i < keys.length(); ++i) {
            meshcore::app_handle_key(keys[i]);
        }
        snapshot_ = meshcore::app_snapshot();
        meshcore::ui_show(meshcore::app_active_screen());
        Serial.printf("hil: ui key count=%d screen=%s\n", keys.length(), meshcore::screen_title(meshcore::app_active_screen()));
        return;
    }
    if (trimmed.startsWith("mesh inject-node ")) {
        const int rest = String("mesh inject-node ").length();
        const int split = trimmed.indexOf(' ', rest);
        if (split <= rest || split >= trimmed.length() - 1) {
            Serial.println("usage: mesh inject-node <id> <name>");
            return;
        }
        const String id = trimmed.substring(rest, split);
        const String name = trimmed.substring(split + 1);
        upsertNode(id, name, -57, 7.5f, true);
        snapshot_.state.last_event = "hil: injected node";
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("hil: injected node id=%s name=%s\n", id.c_str(), name.c_str());
        return;
    }
    if (trimmed.startsWith("mesh inject-direct ")) {
        const int rest = String("mesh inject-direct ").length();
        const int split = trimmed.indexOf(' ', rest);
        if (split <= rest || split >= trimmed.length() - 1) {
            Serial.println("usage: mesh inject-direct <sender> <text>");
            return;
        }
        const String sender = trimmed.substring(rest, split);
        const String body = trimmed.substring(split + 1);
        upsertNode(sender, node_name_for_sender(sender), -55, 8.0f, true);
        char id[16];
        std::snprintf(id, sizeof(id), "h%u", next_incoming_id_++);
        MeshMessage message{id, to_string(sender), "Direct", to_string(body), currentEpochSeconds(), false};
        addMessage(message, true);
        select_conversation_for_message(snapshot_, message);
        snapshot_.state.last_event = "hil: injected direct";
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("hil: injected direct sender=%s id=%s\n", sender.c_str(), id);
        return;
    }
    if (trimmed.startsWith("mesh inject-channel ")) {
        const int rest = String("mesh inject-channel ").length();
        const int first = trimmed.indexOf(' ', rest);
        const int second = first > 0 ? trimmed.indexOf(' ', first + 1) : -1;
        if (first <= rest || second <= first || second >= trimmed.length() - 1) {
            Serial.println("usage: mesh inject-channel <channel> <sender> <text>");
            return;
        }
        const String channel = trimmed.substring(rest, first);
        const String sender = trimmed.substring(first + 1, second);
        const String body = trimmed.substring(second + 1);
        upsertNode(sender, node_name_for_sender(sender), -56, 7.0f, true);
        char id[16];
        std::snprintf(id, sizeof(id), "h%u", next_incoming_id_++);
        MeshMessage message{id, to_string(sender), to_string("Channel " + channel), to_string(body), currentEpochSeconds(), false};
        addMessage(message, true);
        select_conversation_for_message(snapshot_, message);
        snapshot_.state.last_event = "hil: injected channel";
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("hil: injected channel channel=%s sender=%s id=%s\n", channel.c_str(), sender.c_str(), id);
        return;
    }
    if (trimmed == "sys reboot") {
        Serial.println("hil: rebooting");
        delay(100);
        ESP.restart();
        return;
    }
    if (trimmed == "logs") {
        for (const auto& log : snapshot_.logs) {
            Serial.println(log.c_str());
        }
        return;
    }
    if (trimmed == "messages") {
        for (const auto& message : snapshot_.messages) {
            Serial.printf("%s %s %s: %s\n",
                          message.outgoing ? "tx" : "rx",
                          message.id.c_str(),
                          message.sender.c_str(),
                          message.body.c_str());
        }
        return;
    }
    if (trimmed == "nodes") {
        for (const auto& node : snapshot_.nodes) {
            Serial.printf("%s %s rssi=%d snr=%.1f seen=%us\n",
                          node.short_id.c_str(),
                          node.name.c_str(),
                          node.rssi,
                          node.snr,
                          node.last_seen_seconds);
        }
        return;
    }
    if (trimmed == "channels") {
        for (std::size_t i = 0; i < snapshot_.channels.size(); ++i) {
            const auto& channel = snapshot_.channels[i];
            Serial.printf("%u %c %s secret=%s\n",
                          static_cast<unsigned>(i),
                          channel.active ? '*' : '-',
                          channel.name.c_str(),
                          bytes_hex(channel.secret).c_str());
        }
        return;
    }
    if (trimmed.startsWith("channel select ")) {
        String token = trimmed.substring(String("channel select ").length());
        token.trim();
        if (token.startsWith("#")) {
            token = token.substring(1);
        }
        int channel_index = -1;
        if (token.length() > 0 && std::isdigit(static_cast<unsigned char>(token[0]))) {
            channel_index = token.toInt();
        } else {
            channel_index = find_channel_index(snapshot_.channels, to_string(token));
        }
        if (channel_index < 0 || channel_index >= static_cast<int>(snapshot_.channels.size())) {
            Serial.println("channel not found");
            return;
        }
        select_channel(snapshot_, channel_index);
        radio_.setChannels(snapshot_.channels);
        saveSettings();
        ble_.updateSnapshot(snapshot_);
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("channel selected %d %s\n",
                      channel_index,
                      snapshot_.channels[static_cast<std::size_t>(channel_index)].name.c_str());
        return;
    }
    if (trimmed.startsWith("channel set ")) {
        String rest = trimmed.substring(String("channel set ").length());
        rest.trim();
        const int first_space = rest.indexOf(' ');
        const int second_space = first_space >= 0 ? rest.indexOf(' ', first_space + 1) : -1;
        if (first_space <= 0) {
            Serial.println("usage: channel set <index> <name> [32hex|auto]");
            return;
        }
        const int channel_index = rest.substring(0, first_space).toInt();
        String channel_name = second_space > first_space
                                  ? rest.substring(first_space + 1, second_space)
                                  : rest.substring(first_space + 1);
        String secret_hex = second_space > first_space ? rest.substring(second_space + 1) : String("auto");
        channel_name.trim();
        secret_hex.trim();
        const bool hash_channel = channel_name.startsWith("#");
        if (hash_channel) {
            channel_name = channel_name.substring(1);
        }
        if (channel_index < 0 || channel_index >= static_cast<int>(max_meshcore_channels)) {
            Serial.println("channel index out of range");
            return;
        }
        std::array<unsigned char, 16> secret{};
        if (secret_hex.equalsIgnoreCase("auto")) {
            if (channel_name == "public" || channel_name == "Public") {
                secret = public_channel_secret();
            } else if (channel_name == "test" || hash_channel) {
                secret = hashtag_channel_secret("#" + channel_name);
            } else {
                Serial.println("channel secret required for non-hashtag channels");
                return;
            }
        } else if (!parse_hex_bytes(secret_hex, secret)) {
            Serial.println("channel secret must be 32 hex chars or auto");
            return;
        }
        if (static_cast<std::size_t>(channel_index) >= snapshot_.channels.size()) {
            snapshot_.channels.resize(static_cast<std::size_t>(channel_index) + 1);
        }
        auto& channel = snapshot_.channels[static_cast<std::size_t>(channel_index)];
        channel.name = to_string(channel_name);
        channel.secret = secret;
        channel.active = snapshot_.state.selected_channel == channel_index && !channel.name.empty();
        ensure_channel_defaults(snapshot_.channels);
        if (snapshot_.state.selected_channel == channel_index) {
            select_channel(snapshot_, channel_index);
        }
        radio_.setChannels(snapshot_.channels);
        saveSettings();
        ble_.updateSnapshot(snapshot_);
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("channel set %d %s secret=%s\n",
                      channel_index,
                      snapshot_.channels[static_cast<std::size_t>(channel_index)].name.c_str(),
                      bytes_hex(snapshot_.channels[static_cast<std::size_t>(channel_index)].secret).c_str());
        return;
    }
    if (trimmed.startsWith("channel clear ")) {
        String token = trimmed.substring(String("channel clear ").length());
        token.trim();
        const int channel_index = token.toInt();
        if (channel_index < 2 || channel_index >= static_cast<int>(snapshot_.channels.size())) {
            Serial.println("channel clear requires an existing index >= 2");
            return;
        }
        auto& channel = snapshot_.channels[static_cast<std::size_t>(channel_index)];
        channel.name.clear();
        channel.secret = {};
        channel.active = false;
        if (snapshot_.state.selected_channel == channel_index) {
            select_test_channel(snapshot_);
        }
        trim_empty_channel_tail(snapshot_.channels);
        ensure_channel_defaults(snapshot_.channels);
        radio_.setChannels(snapshot_.channels);
        saveSettings();
        ble_.updateSnapshot(snapshot_);
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("channel cleared %d count=%u\n",
                      channel_index,
                      static_cast<unsigned>(snapshot_.channels.size()));
        return;
    }
    if (trimmed == "clear-messages") {
        const bool cleared = storage_.clearMessageRecords();
        snapshot_.messages.clear();
        snapshot_.state.selected_message = 0;
        snapshot_.state.persisted_message_count = 0;
        snapshot_.state.last_event = cleared ? "storage: messages cleared" : "storage: clear failed";
        appendLog(from_string(snapshot_.state.last_event));
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.println(snapshot_.state.last_event.c_str());
        return;
    }
    if (trimmed == "clear-nodes") {
        const bool cleared = storage_.clearNodeRecords();
        snapshot_.nodes.clear();
        snapshot_.state.selected_node = 0;
        snapshot_.state.node_count = 0;
        snapshot_.state.persisted_node_count = 0;
        snapshot_.state.last_event = cleared ? "storage: nodes cleared" : "storage: node clear failed";
        appendLog(from_string(snapshot_.state.last_event));
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.println(snapshot_.state.last_event.c_str());
        return;
    }
    if (trimmed == "status") {
        const String key_prefix = public_key_hex(snapshot_.state.public_key);
        const String bandwidth_khz = bandwidth_khz_string(snapshot_.state.radio_bandwidth_hz);
        Serial.printf("name=%s node=%s key=%s region=%s freq=%ukHz bw=%skHz sf=%u cr=%u core=%s radio=%s ble=%s battery=%d%% heap=%u psram=%u/%u gps=%s sd=%s messages=%u persisted=%u nodes=%u saved_nodes=%u uptime=%us rxraw=%u rxok=%u rxfail=%u tx=%u txfail=%u lastpkt=%u lasttype=%u lastdec=%s rssi=%d noise=%d snr=%.1f rxactive=%d dio1=%d busy=%d irq=0x%04X begin=%d rxstart=%d read=%d rf_sw=%d tcxo=%dmV\n",
                      snapshot_.state.device_name.c_str(),
                      snapshot_.state.local_node_id.c_str(),
                      key_prefix.c_str(),
                      snapshot_.state.region.c_str(),
                      snapshot_.state.radio_frequency_khz,
                      bandwidth_khz.c_str(),
                      snapshot_.state.radio_spreading_factor,
                      snapshot_.state.radio_coding_rate,
                      core_.upstreamAvailable() ? "upstream" : "shim",
                      snapshot_.state.radio_state.c_str(),
                      snapshot_.state.ble_state.c_str(),
                      snapshot_.state.battery_percent,
                      snapshot_.state.heap_free_bytes,
                      snapshot_.state.psram_free_bytes,
                      snapshot_.state.psram_total_bytes,
                      snapshot_.state.gps_state.c_str(),
                      snapshot_.state.storage_state.c_str(),
                      static_cast<unsigned>(snapshot_.messages.size()),
                      snapshot_.state.persisted_message_count,
                      static_cast<unsigned>(snapshot_.nodes.size()),
                      snapshot_.state.persisted_node_count,
                      snapshot_.state.uptime_seconds,
                      snapshot_.state.radio_rx_raw_count,
                      snapshot_.state.radio_rx_decoded_count,
                      snapshot_.state.radio_rx_decode_fail_count,
                      snapshot_.state.packet_tx_count,
                      snapshot_.state.radio_tx_fail_count,
                      snapshot_.state.radio_last_packet_len,
                      snapshot_.state.radio_last_packet_type,
                      snapshot_.state.radio_last_decode.c_str(),
                      snapshot_.state.last_rssi,
                      snapshot_.state.noise_floor,
                      static_cast<float>(snapshot_.state.last_snr_quarters) / 4.0f,
                      snapshot_.state.radio_rx_active ? 1 : 0,
                      snapshot_.state.radio_dio1_level,
                      snapshot_.state.radio_busy_level,
                      snapshot_.state.radio_irq_flags,
                      snapshot_.state.radio_begin_result,
                      snapshot_.state.radio_rx_start_result,
                      snapshot_.state.radio_read_result,
                      snapshot_.state.radio_dio2_as_rf_switch ? 1 : 0,
                      snapshot_.state.radio_tcxo_mv);
        return;
    }
    if (trimmed == "radio" || trimmed == "radio diag" || trimmed == "get radio") {
        const String bandwidth_khz = bandwidth_khz_string(snapshot_.state.radio_bandwidth_hz);
        Serial.printf("radio freq=%ukHz bw=%skHz sf=%u cr=%u power=%ddBm path=%d state=%s rxraw=%u rxok=%u rxfail=%u tx=%u txfail=%u lastpkt=%u lasttype=%u lastdec=%s rssi=%d noise=%d snr=%.1f rxactive=%d dio1=%d busy=%d irq=0x%04X begin=%d rxstart=%d read=%d rf_sw=%d tcxo=%dmV\n",
                      snapshot_.state.radio_frequency_khz,
                      bandwidth_khz.c_str(),
                      snapshot_.state.radio_spreading_factor,
                      snapshot_.state.radio_coding_rate,
                      snapshot_.state.tx_power_dbm,
                      snapshot_.state.path_hash_mode + 1,
                      snapshot_.state.radio_state.c_str(),
                      snapshot_.state.radio_rx_raw_count,
                      snapshot_.state.radio_rx_decoded_count,
                      snapshot_.state.radio_rx_decode_fail_count,
                      snapshot_.state.packet_tx_count,
                      snapshot_.state.radio_tx_fail_count,
                      snapshot_.state.radio_last_packet_len,
                      snapshot_.state.radio_last_packet_type,
                      snapshot_.state.radio_last_decode.c_str(),
                      snapshot_.state.last_rssi,
                      snapshot_.state.noise_floor,
                      static_cast<float>(snapshot_.state.last_snr_quarters) / 4.0f,
                      snapshot_.state.radio_rx_active ? 1 : 0,
                      snapshot_.state.radio_dio1_level,
                      snapshot_.state.radio_busy_level,
                      snapshot_.state.radio_irq_flags,
                      snapshot_.state.radio_begin_result,
                      snapshot_.state.radio_rx_start_result,
                      snapshot_.state.radio_read_result,
                      snapshot_.state.radio_dio2_as_rf_switch ? 1 : 0,
                      snapshot_.state.radio_tcxo_mv);
        return;
    }
    if (trimmed == "radio last") {
        const auto radio = radio_.poll();
        Serial.printf("radio last len=%u type=%u dec=%s rssi=%d snr=%.1f hex=%s\n",
                      radio.last_packet_len,
                      radio.last_packet_type,
                      radio.last_decode.c_str(),
                      radio.rssi,
                      radio.snr,
                      radio.last_packet_hex.c_str());
        return;
    }
    if (trimmed == "radio scan" || trimmed == "radio scan status") {
        printRadioScanStatus();
        if (!radio_scan_active_ && trimmed == "radio scan") {
            startRadioScan();
            Serial.println("radio scan started");
            printRadioScanStatus();
        }
        return;
    }
    if (trimmed == "radio scan start") {
        startRadioScan();
        Serial.println("radio scan started");
        printRadioScanStatus();
        return;
    }
    if (trimmed == "radio scan stop") {
        stopRadioScan(true, "stopped");
        Serial.println("radio scan stopped");
        printRadioScanStatus();
        return;
    }
    if (trimmed == "radio listen" || trimmed.startsWith("radio listen ")) {
        if (radio_scan_active_) {
            stopRadioScan(true, "listen");
        }
        int seconds = 10;
        if (trimmed.startsWith("radio listen ")) {
            seconds = trimmed.substring(13).toInt();
        }
        seconds = std::max(1, std::min(seconds, 60));
        const auto result = radio_.listenWindow(static_cast<uint32_t>(seconds) * 1000U);
        last_hardware_poll_ms_ = 0;
        pollHardware(millis());
        Serial.printf("radio listen seconds=%d samples=%u dio1_high=%u busy_high=%u rx_done=%u preamble=%u header=%u raw_delta=%u ok_delta=%u fail_delta=%u rssi_min=%d rssi_max=%d irq=0x%04X read=%d lastdec=%s\n",
                      seconds,
                      static_cast<unsigned>(result.samples),
                      static_cast<unsigned>(result.dio1_high_samples),
                      static_cast<unsigned>(result.busy_high_samples),
                      static_cast<unsigned>(result.rx_done_flags),
                      static_cast<unsigned>(result.preamble_flags),
                      static_cast<unsigned>(result.header_flags),
                      result.raw_delta,
                      result.decoded_delta,
                      result.fail_delta,
                      result.min_rssi == 127 ? -127 : result.min_rssi,
                      result.max_rssi,
                      result.last_irq_flags,
                      result.last_read_result,
                      result.last_decode.c_str());
        return;
    }
    if (trimmed.startsWith("radio rf-switch ")) {
        const String value = trimmed.substring(String("radio rf-switch ").length());
        if (value != "0" && value != "1") {
            Serial.println("usage: radio rf-switch <0|1>");
            return;
        }
        const bool enabled = value == "1";
        const bool ok = radio_.setDio2AsRfSwitch(enabled);
        last_hardware_poll_ms_ = 0;
        pollHardware(millis());
        Serial.printf("radio rf-switch set=%d ok=%d state=%s\n",
                      enabled ? 1 : 0,
                      ok ? 1 : 0,
                      snapshot_.state.radio_state.c_str());
        return;
    }
    if (trimmed == "radio cad scan" || trimmed.startsWith("radio cad scan ")) {
        if (radio_scan_active_) {
            stopRadioScan(true, "cad scan");
        }
        int count = 4;
        if (trimmed.startsWith("radio cad scan ")) {
            count = trimmed.substring(15).toInt();
        }
        count = std::max(1, std::min(count, 16));

        const uint32_t original_frequency_khz = snapshot_.state.radio_frequency_khz;
        const uint32_t original_bandwidth_hz = snapshot_.state.radio_bandwidth_hz;
        const unsigned original_spreading_factor = snapshot_.state.radio_spreading_factor;
        const unsigned original_coding_rate = snapshot_.state.radio_coding_rate;
        const std::string original_region = snapshot_.state.region;

        constexpr uint8_t preset_count = sizeof(radio_scan_presets) / sizeof(radio_scan_presets[0]);
        unsigned total_detected = 0;
        unsigned total_errors = 0;
        for (uint8_t i = 0; i < preset_count; ++i) {
            const auto& preset = radio_scan_presets[i];
            snapshot_.state.radio_frequency_khz = preset.frequency_khz;
            snapshot_.state.radio_bandwidth_hz = preset.bandwidth_hz;
            snapshot_.state.radio_spreading_factor = preset.spreading_factor;
            snapshot_.state.radio_coding_rate = preset.coding_rate;
            snapshot_.state.region = region_for_frequency_khz(preset.frequency_khz);
            radio_.configure(snapshot_.state);

            unsigned detected = 0;
            unsigned errors = 0;
            RadioCadResult last;
            for (int sample = 0; sample < count; ++sample) {
                last = radio_.scanChannelActivity();
                if (last.detected) {
                    ++detected;
                }
                if (last.error) {
                    ++errors;
                }
                delay(2);
            }
            total_detected += detected;
            total_errors += errors;
            Serial.printf("radio cad preset=%s freq=%ukHz bw=%skHz sf=%u cr=%u count=%d detected=%u errors=%u last=%d rssi=%d irq=0x%04X\n",
                          preset.name,
                          preset.frequency_khz,
                          bandwidth_khz_string(preset.bandwidth_hz).c_str(),
                          preset.spreading_factor,
                          preset.coding_rate,
                          count,
                          detected,
                          errors,
                          last.result,
                          last.rssi,
                          last.irq_flags);
        }

        snapshot_.state.radio_frequency_khz = original_frequency_khz;
        snapshot_.state.radio_bandwidth_hz = original_bandwidth_hz;
        snapshot_.state.radio_spreading_factor = original_spreading_factor;
        snapshot_.state.radio_coding_rate = original_coding_rate;
        snapshot_.state.region = original_region;
        radio_.configure(snapshot_.state);
        Serial.printf("radio cad sweep done presets=%u count=%d detected=%u errors=%u\n",
                      preset_count,
                      count,
                      total_detected,
                      total_errors);
        return;
    }
    if (trimmed == "radio cad" || trimmed.startsWith("radio cad ")) {
        if (radio_scan_active_) {
            stopRadioScan(true, "cad");
        }
        int count = 8;
        if (trimmed.startsWith("radio cad ")) {
            count = trimmed.substring(10).toInt();
        }
        count = std::max(1, std::min(count, 64));
        unsigned detected = 0;
        unsigned errors = 0;
        RadioCadResult last;
        for (int i = 0; i < count; ++i) {
            last = radio_.scanChannelActivity();
            if (last.detected) {
                ++detected;
            }
            if (last.error) {
                ++errors;
            }
            delay(5);
        }
        const String bandwidth_khz = bandwidth_khz_string(snapshot_.state.radio_bandwidth_hz);
        Serial.printf("radio cad count=%d detected=%u errors=%u last=%d rssi=%d irq=0x%04X freq=%ukHz bw=%skHz sf=%u cr=%u\n",
                      count,
                      detected,
                      errors,
                      last.result,
                      last.rssi,
                      last.irq_flags,
                      snapshot_.state.radio_frequency_khz,
                      bandwidth_khz.c_str(),
                      snapshot_.state.radio_spreading_factor,
                      snapshot_.state.radio_coding_rate);
        return;
    }
    if (trimmed.startsWith("radio set ") || trimmed.startsWith("set radio ")) {
        if (radio_scan_active_) {
            stopRadioScan(false, "cancelled");
        }
        String rest = trimmed.substring(10);
        rest.replace(",", " ");
        rest.trim();
        while (rest.indexOf("  ") >= 0) {
            rest.replace("  ", " ");
        }
        const int first = rest.indexOf(' ');
        const int second = first < 0 ? -1 : rest.indexOf(' ', first + 1);
        const int third = second < 0 ? -1 : rest.indexOf(' ', second + 1);
        if (first < 0 || second < 0 || third < 0) {
            Serial.println("usage: radio set <freq_khz> <bw_khz> <sf> <cr> or set radio <freq_mhz>,<bw_khz>,<sf>,<cr>");
            return;
        }
        const float frequency_value = rest.substring(0, first).toFloat();
        uint32_t frequency_khz = 0;
        if (frequency_value < 2500.0f) {
            frequency_khz = static_cast<uint32_t>((frequency_value * 1000.0f) + 0.5f);
        } else if (frequency_value > 2500000.0f) {
            frequency_khz = static_cast<uint32_t>((frequency_value / 1000.0f) + 0.5f);
        } else {
            frequency_khz = static_cast<uint32_t>(frequency_value);
        }
        const float bandwidth_khz = rest.substring(first + 1, second).toFloat();
        const uint32_t bandwidth_hz = static_cast<uint32_t>((bandwidth_khz * 1000.0f) + 0.5f);
        const uint32_t spreading_factor = static_cast<uint32_t>(rest.substring(second + 1, third).toInt());
        const uint32_t coding_rate = static_cast<uint32_t>(rest.substring(third + 1).toInt());
        if (frequency_khz < 150000 || frequency_khz > 2500000 ||
            bandwidth_hz < 7000 || bandwidth_hz > 500000 ||
            spreading_factor < 5 || spreading_factor > 12 ||
            coding_rate < 5 || coding_rate > 8) {
            Serial.println("radio set out of range");
            return;
        }
        snapshot_.state.radio_frequency_khz = frequency_khz;
        snapshot_.state.radio_bandwidth_hz = bandwidth_hz;
        snapshot_.state.radio_spreading_factor = spreading_factor;
        snapshot_.state.radio_coding_rate = coding_rate;
        snapshot_.state.region = region_for_frequency_khz(frequency_khz);
        radio_.configure(snapshot_.state);
        saveSettings();
        snapshot_.state.last_event = "serial: radio params updated";
        appendLog(from_string(snapshot_.state.last_event));
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.println(snapshot_.state.last_event.c_str());
        return;
    }
    if (trimmed == "radio defaults" || trimmed == "radio default" || trimmed == "radio meshcore") {
        if (radio_scan_active_) {
            stopRadioScan(false, "cancelled");
        }
        apply_region_defaults(snapshot_.state);
        radio_.configure(snapshot_.state);
        saveSettings();
        ble_.updateSnapshot(snapshot_);
        snapshot_.state.last_event = "serial: radio defaults restored";
        appendLog(from_string(snapshot_.state.last_event));
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("radio defaults freq=%ukHz bw=%skHz sf=%u cr=%u\n",
                      snapshot_.state.radio_frequency_khz,
                      bandwidth_khz_string(snapshot_.state.radio_bandwidth_hz).c_str(),
                      snapshot_.state.radio_spreading_factor,
                      snapshot_.state.radio_coding_rate);
        return;
    }
    if (trimmed == "radio rx-reset" || trimmed == "radio reset" || trimmed == "radio reset-agc") {
        const bool ok = radio_.resetReceiver();
        const auto radio = radio_.poll();
        snapshot_.state.radio_state = radio.state.c_str();
        snapshot_.state.radio_rx_active = radio.rx_active;
        snapshot_.state.radio_irq_flags = radio.irq_flags;
        snapshot_.state.radio_rx_start_result = radio.rx_start_result;
        snapshot_.state.last_event = ok ? "serial: radio receiver reset" : "serial: radio receiver reset failed";
        appendLog(from_string(snapshot_.state.last_event));
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("radio rx-reset ok=%d state=%s rxactive=%d irq=0x%04X rxstart=%d\n",
                      ok ? 1 : 0,
                      radio.state.c_str(),
                      radio.rx_active ? 1 : 0,
                      radio.irq_flags,
                      radio.rx_start_result);
        return;
    }
    if (trimmed == "identity") {
        Serial.printf("public_key=%s\n", public_key_hex(snapshot_.state.public_key, snapshot_.state.public_key.size()).c_str());
        return;
    }
    if (trimmed.startsWith("name ")) {
        const String name = trimmed.substring(5);
        if (name.length() > 0 && name.length() <= 32) {
            snapshot_.state.device_name = to_string(name);
            saveSettings();
            snapshot_.state.last_event = "serial: name updated";
            meshcore::app_ingest_service_snapshot(snapshot_);
        } else {
            Serial.println("name must be 1..32 chars");
        }
        return;
    }
    if (trimmed.startsWith("rename-node ")) {
        const int split = trimmed.indexOf(' ', 12);
        if (split > 0) {
            const String id = trimmed.substring(12, split);
            const String name = trimmed.substring(split + 1);
            if (id.length() > 0 && name.length() > 0 && name.length() <= 32) {
                upsertNode(id, name, -90, 0.0f, true);
                snapshot_.state.last_event = "serial: node renamed";
                meshcore::app_ingest_service_snapshot(snapshot_);
            } else {
                Serial.println("usage: rename-node <id> <name>");
            }
        } else {
            Serial.println("usage: rename-node <id> <name>");
        }
        return;
    }
    if (trimmed.startsWith("node ")) {
        const String node = trimmed.substring(5);
        if (node.length() > 0 && node.length() <= 16) {
            snapshot_.state.local_node_id = to_string(node);
            radio_.setLocalNodeId(node);
            saveSettings();
            snapshot_.state.last_event = "serial: node id updated";
            meshcore::app_ingest_service_snapshot(snapshot_);
        } else {
            Serial.println("node id must be 1..16 chars");
        }
        return;
    }
    if (trimmed == "region") {
        if (snapshot_.state.region == "915 MHz") {
            snapshot_.state.region = "868 MHz";
        } else if (snapshot_.state.region == "868 MHz") {
            snapshot_.state.region = "433 MHz";
        } else {
            snapshot_.state.region = "915 MHz";
        }
        apply_region_defaults(snapshot_.state);
        radio_.configure(snapshot_.state);
        saveSettings();
        snapshot_.state.last_event = "serial: region " + snapshot_.state.region;
        meshcore::app_ingest_service_snapshot(snapshot_);
        return;
    }
    if (trimmed.startsWith("power ")) {
        const int value = trimmed.substring(6).toInt();
        if (value >= 2 && value <= 22) {
            snapshot_.state.tx_power_dbm = value;
            radio_.configure(snapshot_.state);
            saveSettings();
            snapshot_.state.last_event = "serial: tx power " + std::to_string(value);
            meshcore::app_ingest_service_snapshot(snapshot_);
        } else {
            Serial.println("power must be 2..22");
        }
        return;
    }
    if (trimmed == "path") {
        snapshot_.state.path_hash_mode = (snapshot_.state.path_hash_mode + 1) % 3;
        radio_.configure(snapshot_.state);
        saveSettings();
        snapshot_.state.last_event = "serial: path hash " + std::to_string(snapshot_.state.path_hash_mode + 1);
        meshcore::app_ingest_service_snapshot(snapshot_);
        return;
    }
    if (trimmed.startsWith("path ")) {
        const int value = trimmed.substring(5).toInt();
        if (value < 1 || value > 3) {
            Serial.println("path must be 1..3");
            return;
        }
        snapshot_.state.path_hash_mode = value - 1;
        radio_.configure(snapshot_.state);
        saveSettings();
        snapshot_.state.last_event = "serial: path hash " + std::to_string(value);
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.printf("path hash %d\n", value);
        return;
    }
    if (trimmed == "gps") {
        snapshot_.state.gps_enabled = !snapshot_.state.gps_enabled;
        saveSettings();
        snapshot_.state.last_event = snapshot_.state.gps_enabled ? "serial: gps on" : "serial: gps off";
        meshcore::app_ingest_service_snapshot(snapshot_);
        return;
    }
    if (trimmed == "room") {
        snapshot_.state.room_logged_in = !snapshot_.state.room_logged_in;
        saveSettings();
        snapshot_.state.last_event = snapshot_.state.room_logged_in ? "serial: room login" : "serial: room logout";
        meshcore::app_ingest_service_snapshot(snapshot_);
        return;
    }
    if (trimmed == "admin") {
        snapshot_.state.repeater_admin = !snapshot_.state.repeater_admin;
        saveSettings();
        snapshot_.state.last_event = snapshot_.state.repeater_admin ? "serial: admin open" : "serial: admin closed";
        meshcore::app_ingest_service_snapshot(snapshot_);
        return;
    }
    if (trimmed == "audio") {
        snapshot_.state.audio_enabled = !snapshot_.state.audio_enabled;
        saveSettings();
        snapshot_.state.last_event = snapshot_.state.audio_enabled ? "serial: audio on" : "serial: audio off";
        meshcore::app_ingest_service_snapshot(snapshot_);
        return;
    }
    if (trimmed.startsWith("send ")) {
        const int split = trimmed.indexOf(' ', 5);
        if (split > 0) {
            const String node = trimmed.substring(5, split);
            const String body = trimmed.substring(split + 1);
            if (sendDirectMessage(node, body)) {
                char id[16];
                std::snprintf(id, sizeof(id), "s%u", next_incoming_id_++);
                addMessage({id,
                            snapshot_.state.local_node_id,
                            to_string("Direct to " + node),
                            to_string(body),
                            millis() / 1000,
                            true},
                           true);
                snapshot_.state.last_event = "serial: send queued";
            } else {
                snapshot_.state.last_event = "serial: send failed";
            }
            meshcore::app_ingest_service_snapshot(snapshot_);
            return;
        }
    }
    if (trimmed.startsWith("send-channel ")) {
        const int rest = String("send-channel ").length();
        const int split = trimmed.indexOf(' ', rest);
        if (split <= rest || split >= trimmed.length() - 1) {
            Serial.println("usage: send-channel <channel> <text>");
            return;
        }
        String channel_name = trimmed.substring(rest, split);
        if (channel_name.startsWith("#")) {
            channel_name = channel_name.substring(1);
        }
        const String body = trimmed.substring(split + 1);
        const int channel_index = find_channel_index(snapshot_.channels, to_string(channel_name));
        if (channel_index < 0) {
            Serial.println("channel not found");
            return;
        }
        select_channel(snapshot_, channel_index);
        const bool sent = sendDirectMessage("broadcast", body);
        if (sent) {
            char id[16];
            std::snprintf(id, sizeof(id), "s%u", next_incoming_id_++);
            addMessage({id,
                        snapshot_.state.local_node_id,
                        to_string("Channel " + channel_name),
                        to_string(body),
                        millis() / 1000,
                        true},
                       true);
            snapshot_.state.last_event = "serial: channel send queued";
        } else {
            snapshot_.state.last_event = "serial: channel send failed";
        }
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.println(snapshot_.state.last_event.c_str());
        return;
    }
    if (trimmed.startsWith("send-pos ")) {
        const String node = trimmed.substring(9);
        const auto result = radio_.sendPosition(node, snapshot_.state.latitude, snapshot_.state.longitude);
        snapshot_.state.radio_state = result.status.c_str();
        snapshot_.state.last_event = result.accepted ? "serial: position queued" : "serial: position failed";
        appendLog(from_string(snapshot_.state.last_event));
        meshcore::app_ingest_service_snapshot(snapshot_);
        Serial.println(result.status.c_str());
        return;
    }

    Serial.printf("unknown command: %s\n", trimmed.c_str());
}

void MeshService::handleInputKey(char key) {
    if (meshcore::app_edit_active()) {
        const auto edit_field = meshcore::app_snapshot().state.edit_field;
        if (meshcore::app_handle_key(key)) {
            snapshot_ = meshcore::app_snapshot();
            if ((key == '\r' || key == '\n') && snapshot_.state.last_applied_edit == edit_field) {
                switch (edit_field) {
                    case meshcore::EditField::RadioFrequency:
                    case meshcore::EditField::RadioBandwidth:
                    case meshcore::EditField::RadioSpreadingFactor:
                    case meshcore::EditField::RadioCodingRate:
                        radio_.configure(snapshot_.state);
                        saveSettings();
                        break;
                    case meshcore::EditField::ChannelName:
                    case meshcore::EditField::ChannelSecret:
                        radio_.setChannels(snapshot_.channels);
                        saveSettings();
                        break;
                    case meshcore::EditField::DeviceName:
                    case meshcore::EditField::DevicePin:
                    case meshcore::EditField::Latitude:
                    case meshcore::EditField::Longitude:
                    case meshcore::EditField::CustomVarValue:
                        saveSettings();
                        break;
                    case meshcore::EditField::ComposeText:
                    case meshcore::EditField::NoEdit:
                        break;
                }
            }
        }
        return;
    }

    if (key == INPUT_TRACKBALL_DOWN || key == INPUT_TRACKBALL_RIGHT ||
        key == INPUT_TRACKBALL_UP || key == INPUT_TRACKBALL_LEFT) {
        const auto screen = meshcore::app_active_screen();
        if (is_chat_screen(screen)) {
            const int delta = (key == INPUT_TRACKBALL_UP || key == INPUT_TRACKBALL_LEFT) ? -1 : 1;
            if (select_next_chat_message(snapshot_, screen, delta)) {
                snapshot_.state.last_event = "trackball: message select";
                meshcore::app_ingest_service_snapshot(snapshot_);
            }
        } else if (screen == meshcore::ScreenId::Nodes || screen == meshcore::ScreenId::Contacts) {
            if (!snapshot_.nodes.empty()) {
                const int delta = (key == INPUT_TRACKBALL_UP || key == INPUT_TRACKBALL_LEFT) ? -1 : 1;
                const int size = static_cast<int>(snapshot_.nodes.size());
                snapshot_.state.selected_node = (snapshot_.state.selected_node + delta + size) % size;
                snapshot_.state.compose_recipient = snapshot_.nodes[static_cast<std::size_t>(snapshot_.state.selected_node)].short_id;
                snapshot_.state.last_event = "trackball: node select";
                meshcore::app_ingest_service_snapshot(snapshot_);
            }
        } else if (screen == meshcore::ScreenId::Channels || screen == meshcore::ScreenId::ChannelEditor) {
            if (!snapshot_.channels.empty()) {
                const int delta = (key == INPUT_TRACKBALL_UP || key == INPUT_TRACKBALL_LEFT) ? -1 : 1;
                const int size = static_cast<int>(snapshot_.channels.size());
                snapshot_.state.selected_channel = (snapshot_.state.selected_channel + delta + size) % size;
                snapshot_.state.compose_recipient = "broadcast";
                snapshot_.state.last_event = "trackball: channel select";
                meshcore::app_ingest_service_snapshot(snapshot_);
            }
        } else if (screen == meshcore::ScreenId::Diagnostics) {
            const int delta = (key == INPUT_TRACKBALL_UP || key == INPUT_TRACKBALL_LEFT) ? -1 : 1;
            const int total = static_cast<int>(5 + snapshot_.logs.size());
            const int max_scroll = std::max(0, total - 5);
            snapshot_.state.diagnostics_scroll = std::max(0, std::min(max_scroll, snapshot_.state.diagnostics_scroll + delta));
            snapshot_.state.last_event = "trackball: diag scroll";
            meshcore::app_ingest_service_snapshot(snapshot_);
        } else if (screen == meshcore::ScreenId::Map) {
            if (key == INPUT_TRACKBALL_UP || key == INPUT_TRACKBALL_RIGHT) {
                snapshot_.state.map_zoom = std::min(snapshot_.state.map_zoom + 1, 12);
            } else {
                snapshot_.state.map_zoom = std::max(snapshot_.state.map_zoom - 1, 1);
            }
            snapshot_.state.last_event = "trackball: map zoom";
            meshcore::app_ingest_service_snapshot(snapshot_);
        }
        return;
    }

    if (key == '\r' || key == '\n') {
        if (snapshot_.state.compose_text.length() > 0) {
            const String node = from_string(snapshot_.state.compose_recipient);
            const String body = from_string(snapshot_.state.compose_text);
            if (sendDirectMessage(node, body)) {
                audio_.beep(1568, 35, snapshot_.state.audio_enabled);
                char id[16];
                std::snprintf(id, sizeof(id), "k%u", next_incoming_id_++);
                const bool channel_target = is_broadcast_target(node);
                String subject = channel_target ? String("Channel ") + from_string(snapshot_.state.channel) : String("Direct to ") + node;
                if (channel_target && !snapshot_.channels.empty()) {
                    const auto channel_index = static_cast<std::size_t>(std::min<int>(
                        std::max<int>(snapshot_.state.selected_channel, 0),
                        static_cast<int>(snapshot_.channels.size() - 1)));
                    subject = String("Channel ") + from_string(snapshot_.channels[channel_index].name);
                }
                addMessage({id, snapshot_.state.local_node_id, to_string(subject), to_string(body), millis() / 1000, true}, true);
                snapshot_.state.compose_text.clear();
                snapshot_.state.radio_state = "tx queued";
                snapshot_.state.last_event = "keyboard: send queued";
            } else {
                snapshot_.state.radio_state = "tx failed";
                snapshot_.state.last_event = "keyboard: send failed";
            }
            meshcore::app_ingest_service_snapshot(snapshot_);
        }
        return;
    }

    if (key == 8 || key == 127) {
        if (!snapshot_.state.compose_text.empty()) {
            snapshot_.state.compose_text.pop_back();
            snapshot_.state.last_event = "keyboard: edit";
            meshcore::app_ingest_service_snapshot(snapshot_);
        }
        return;
    }

    if (key >= 32 && key <= 126) {
        if (snapshot_.state.compose_text.size() < 160) {
            snapshot_.state.compose_text.push_back(key);
            snapshot_.state.last_event = "keyboard: text";
            meshcore::app_ingest_service_snapshot(snapshot_);
        }
    }
}

void MeshService::processBleCommands() {
    if (!ble_.status().started) {
        return;
    }
    ble_.loop();
    BleCompanionCommand command;
    while (ble_.readCommand(command)) {
        handleBleCommand(command);
    }
}

void MeshService::handleBleCommand(const BleCompanionCommand& command) {
    constexpr uint8_t err_unsupported = 0x01;
    constexpr uint8_t err_not_found = 0x02;
    constexpr uint8_t err_table_full = 0x03;
    constexpr uint8_t err_illegal_arg = 0x06;

    ble_.updateSnapshot(snapshot_);
    snapshot_.state.ble_last_command = ble_command_label(command);
    snapshot_.state.ble_last_error = "none";
    switch (command.type) {
        case BleCompanionCommand::Type::AppStart:
            ble_synced_message_ids_.clear();
            ble_.sendSelfInfo();
            snapshot_.state.last_event = "ble: app connected";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::DeviceQuery:
            ble_.sendDeviceInfo();
            break;
        case BleCompanionCommand::Type::GetDeviceTime:
            ble_.sendCurrentTime(currentEpochSeconds());
            break;
        case BleCompanionCommand::Type::SetDeviceTime:
            clock_epoch_base_ = command.timestamp;
            clock_millis_base_ = millis();
            ble_.sendOk();
            snapshot_.state.last_event = "ble: time updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::SetAdvertName:
            if (command.text.length() == 0 || command.text.length() > 31) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            snapshot_.state.device_name = to_string(command.text);
            saveSettings();
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: advert name updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::SetAdvertLatLon:
            if (command.latitude_i < -90000000 || command.latitude_i > 90000000 ||
                command.longitude_i < -180000000 || command.longitude_i > 180000000) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            snapshot_.state.latitude = static_cast<double>(command.latitude_i) / 1000000.0;
            snapshot_.state.longitude = static_cast<double>(command.longitude_i) / 1000000.0;
            snapshot_.state.advert_location_policy = 1;
            saveSettings();
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: advert location updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::SendSelfAdvert: {
            const auto result = radio_.sendSelfAdvert(from_string(snapshot_.state.device_name),
                                                      snapshot_.state.advert_location_policy != 0 &&
                                                          snapshot_.state.latitude != 0.0 &&
                                                          snapshot_.state.longitude != 0.0,
                                                      snapshot_.state.latitude,
                                                      snapshot_.state.longitude,
                                                      currentEpochSeconds());
            if (result.accepted) {
                ble_.sendOk();
            } else {
                ble_.sendError(err_table_full);
            }
            snapshot_.state.radio_state = result.status.c_str();
            snapshot_.state.last_event = result.accepted ? "ble: advert sent" : "ble: advert failed";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::SetRadioParams:
            if (command.frequency_khz < 150000 || command.frequency_khz > 2500000 ||
                command.bandwidth_hz < 7000 || command.bandwidth_hz > 500000 ||
                command.spreading_factor < 5 || command.spreading_factor > 12 ||
                command.coding_rate < 5 || command.coding_rate > 8 ||
                (command.client_repeat && !is_valid_client_repeat_frequency(command.frequency_khz))) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            snapshot_.state.radio_frequency_khz = command.frequency_khz;
            snapshot_.state.radio_bandwidth_hz = command.bandwidth_hz;
            snapshot_.state.radio_spreading_factor = command.spreading_factor;
            snapshot_.state.radio_coding_rate = command.coding_rate;
            snapshot_.state.client_repeat = command.client_repeat;
            snapshot_.state.region = region_for_frequency_khz(command.frequency_khz);
            radio_.configure(snapshot_.state);
            saveSettings();
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: radio params updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::SetRadioTxPower:
            if (command.tx_power_dbm < -9 || command.tx_power_dbm > 22) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            snapshot_.state.tx_power_dbm = command.tx_power_dbm;
            radio_.configure(snapshot_.state);
            saveSettings();
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: tx power updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::SetPathHashMode:
            if (command.path_hash_mode < 0 || command.path_hash_mode >= 3) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            snapshot_.state.path_hash_mode = command.path_hash_mode;
            radio_.configure(snapshot_.state);
            saveSettings();
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: path hash updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::SetDevicePin:
            if (command.device_pin != 0 &&
                (command.device_pin < 100000 || command.device_pin > 999999)) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            snapshot_.state.device_pin_set = command.device_pin != 0;
            saveSettings();
            ble_.sendOk();
            snapshot_.state.last_event = "ble: device pin updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::SetTuningParams:
            snapshot_.state.rx_delay_base_ms = command.rx_delay_base_ms;
            snapshot_.state.airtime_factor_ms = command.airtime_factor_ms;
            saveSettings();
            ble_.sendOk();
            snapshot_.state.last_event = "ble: tuning updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::GetTuningParams:
            ble_.sendTuningParams(snapshot_.state.rx_delay_base_ms,
                                  snapshot_.state.airtime_factor_ms);
            break;
        case BleCompanionCommand::Type::SetOtherParams:
            snapshot_.state.manual_add_contacts = command.manual_add_contacts;
            snapshot_.state.advert_location_policy = command.advert_location_policy;
            snapshot_.state.multi_acks = command.multi_acks;
            saveSettings();
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: other params updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::ExportPrivateKey: {
            std::array<uint8_t, 64> private_key{};
            if (core_.exportPrivateKey(private_key)) {
                ble_.sendPrivateKey(private_key);
            } else {
                ble_.sendError(err_unsupported);
            }
            break;
        }
        case BleCompanionCommand::Type::ImportPrivateKey:
            if (!core_.importPrivateKey(command.private_key)) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            syncCoreState();
            saveSettings();
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: identity imported";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::GetStats: {
            if (command.stats_type > 2) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            const int8_t snr_quarters = static_cast<int8_t>(std::max<int>(
                -128,
                std::min<int>(127, static_cast<int>(snapshot_.nodes.empty()
                                                        ? 0
                                                        : snapshot_.nodes.front().snr * 4.0f))));
            ble_.sendStats(command.stats_type,
                           static_cast<uint16_t>(std::max(0, snapshot_.state.battery_mv)),
                           snapshot_.state.uptime_seconds,
                           0,
                           0,
                           -127,
                           static_cast<int8_t>(snapshot_.nodes.empty() ? -127 : snapshot_.nodes.front().rssi),
                           snr_quarters);
            break;
        }
        case BleCompanionCommand::Type::SetAutoAddConfig:
            snapshot_.state.autoadd_config = command.autoadd_config;
            snapshot_.state.autoadd_max_hops = std::min<unsigned>(64, command.autoadd_max_hops);
            saveSettings();
            ble_.sendOk();
            snapshot_.state.last_event = "ble: auto-add updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::GetAutoAddConfig:
            ble_.sendAutoAddConfig(static_cast<uint8_t>(snapshot_.state.autoadd_config),
                                   static_cast<uint8_t>(std::min<unsigned>(64, snapshot_.state.autoadd_max_hops)));
            break;
        case BleCompanionCommand::Type::GetAllowedRepeatFreq:
            ble_.sendAllowedRepeatFreq();
            break;
        case BleCompanionCommand::Type::GetDefaultFloodScope:
            ble_.sendDefaultFloodScope(snapshot_.state.default_flood_name,
                                       snapshot_.state.default_flood_secret);
            break;
        case BleCompanionCommand::Type::SetDefaultFloodScope:
            snapshot_.state.default_flood_name = to_string(command.text);
            std::copy(command.secret.begin(),
                      command.secret.end(),
                      snapshot_.state.default_flood_secret.begin());
            snapshot_.state.default_flood_scope = snapshot_.state.default_flood_name.empty() ? 0 : 1;
            ++snapshot_.state.flood_scope_key;
            saveSettings();
            ble_.sendOk();
            snapshot_.state.last_event = "ble: default flood updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        case BleCompanionCommand::Type::GetBattery:
            ble_.sendBattery();
            break;
        case BleCompanionCommand::Type::GetChannel:
            ble_.sendChannelInfo(command.channel_index);
            break;
        case BleCompanionCommand::Type::GetContacts:
            ble_.sendContactsStart(snapshot_.nodes.size());
            for (const auto& node : snapshot_.nodes) {
                if (has_public_key(node.public_key)) {
                    ble_.sendContact(node);
                }
            }
            ble_.sendEndOfContacts();
            break;
        case BleCompanionCommand::Type::GetContactByKey: {
            const auto* node = find_node_by_key(snapshot_.nodes, command.contact.public_key);
            if (node == nullptr) {
                ble_.sendError(err_not_found);
            } else {
                ble_.sendContact(*node);
            }
            break;
        }
        case BleCompanionCommand::Type::HasConnection: {
            const auto* node = find_node_by_key(snapshot_.nodes, command.contact.public_key);
            if (node != nullptr && node->out_path_len != meshcore::NodeInfo::out_path_unknown) {
                ble_.sendOk();
            } else {
                ble_.sendError(err_not_found);
            }
            break;
        }
        case BleCompanionCommand::Type::GetAdvertPath: {
            const auto* node = find_node_by_key(snapshot_.nodes, command.contact.public_key);
            if (node == nullptr) {
                ble_.sendError(err_not_found);
            } else {
                ble_.sendAdvertPath(*node, node->last_seen_seconds);
            }
            break;
        }
        case BleCompanionCommand::Type::AddUpdateContact: {
            if (!has_public_key(command.contact.public_key)) {
                ble_.sendError(err_illegal_arg);
                break;
            }

            NodeInfo contact = command.contact;
            if (contact.short_id.empty()) {
                contact.short_id = to_string(short_id_from_public_key(contact.public_key));
            }
            if (contact.name.empty()) {
                contact.name = contact.short_id;
            }

            NodeInfo* existing = find_node_by_key(snapshot_.nodes, contact.public_key);
            if (existing == nullptr) {
                existing = find_node(snapshot_.nodes, from_string(contact.short_id));
            }
            if (existing == nullptr) {
                snapshot_.nodes.insert(snapshot_.nodes.begin(), contact);
                if (snapshot_.nodes.size() > 16) {
                    snapshot_.nodes.pop_back();
                }
                existing = snapshot_.nodes.empty() ? nullptr : &snapshot_.nodes.front();
            } else {
                const int rssi = existing->rssi;
                const float snr = existing->snr;
                const unsigned last_seen = existing->last_seen_seconds;
                *existing = contact;
                existing->rssi = rssi;
                existing->snr = snr;
                existing->last_seen_seconds = last_seen;
            }
            snapshot_.state.node_count = static_cast<int>(snapshot_.nodes.size());
            snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
            radio_.setNodes(snapshot_.nodes);
            if (existing != nullptr) {
                persistNode(*existing);
            }
            ble_.sendOk();
            snapshot_.state.last_event = "ble: contact updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::RemoveContact: {
            const auto before = snapshot_.nodes.size();
            snapshot_.nodes.erase(std::remove_if(snapshot_.nodes.begin(), snapshot_.nodes.end(),
                                                [&](const NodeInfo& node) {
                                                    return same_public_key(node.public_key, command.contact.public_key);
                                                }),
                                  snapshot_.nodes.end());
            if (snapshot_.nodes.size() == before) {
                ble_.sendError(err_not_found);
                break;
            }
            snapshot_.state.node_count = static_cast<int>(snapshot_.nodes.size());
            snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
            radio_.setNodes(snapshot_.nodes);
            storage_.clearNodeRecords();
            for (const auto& node : snapshot_.nodes) {
                persistNode(node);
            }
            ble_.sendOk();
            snapshot_.state.last_event = "ble: contact removed";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::ResetPath: {
            NodeInfo* node = find_node_by_key(snapshot_.nodes, command.contact.public_key);
            if (node == nullptr) {
                ble_.sendError(err_not_found);
                break;
            }
            node->out_path_len = meshcore::NodeInfo::out_path_unknown;
            node->out_path = {};
            persistNode(*node);
            radio_.setNodes(snapshot_.nodes);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: contact path reset";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::Logout: {
            NodeInfo* node = find_node_by_key(snapshot_.nodes, command.contact.public_key);
            if (node != nullptr) {
                node->out_path_len = meshcore::NodeInfo::out_path_unknown;
                node->out_path = {};
                persistNode(*node);
                radio_.setNodes(snapshot_.nodes);
            }
            ble_.sendOk();
            snapshot_.state.last_event = "ble: contact logged out";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::SendContactMessage: {
            constexpr uint8_t txt_type_plain = 0x00;
            if (command.text_type != txt_type_plain || command.text.length() == 0) {
                ble_.sendError(err_unsupported);
                break;
            }
            const auto* contact = find_node_by_key_prefix(snapshot_.nodes, command.contact.public_key, 6);
            if (contact == nullptr) {
                ble_.sendError(err_not_found);
                break;
            }
            const bool sent = sendDirectMessage(from_string(contact->short_id), command.text);
            if (!sent) {
                ble_.sendError(err_table_full);
                snapshot_.state.radio_state = "tx failed";
                snapshot_.state.last_event = "ble: direct tx failed";
                meshcore::app_ingest_service_snapshot(snapshot_);
                break;
            }
            char id[16];
            std::snprintf(id, sizeof(id), "b%u", next_incoming_id_++);
            const unsigned timestamp = command.timestamp != 0 ? command.timestamp : millis() / 1000;
            addMessage({id,
                        snapshot_.state.local_node_id,
                        "Direct to " + contact->short_id,
                        to_string(command.text),
                        timestamp,
                        true},
                       true);
            ble_.sendSent(contact->out_path_len == meshcore::NodeInfo::out_path_unknown, 0, 0);
            snapshot_.state.radio_state = "tx queued";
            snapshot_.state.last_event = "ble: direct tx queued";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::SetChannel: {
            if (command.channel_index >= max_meshcore_channels) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            const std::size_t channel_index = static_cast<std::size_t>(command.channel_index);
            if (channel_index >= snapshot_.channels.size()) {
                snapshot_.channels.resize(channel_index + 1);
            }
            auto& channel = snapshot_.channels[channel_index];
            String channel_name = command.channel_name;
            channel_name.trim();
            const bool hash_channel = channel_name.startsWith("#");
            if (hash_channel) {
                channel.secret = secret_is_empty(command.secret)
                                     ? hashtag_channel_secret(channel_name)
                                     : command.secret;
                channel_name = channel_name.substring(1);
            } else {
                channel.secret = command.secret;
            }
            channel.name = to_string(channel_name);
            channel.active = snapshot_.state.selected_channel == static_cast<int>(channel_index) && !channel.name.empty();
            if (channel_index == 0 && channel.name.empty()) {
                channel.name = "public";
                channel.secret = public_channel_secret();
                channel.active = snapshot_.state.selected_channel == 0;
            }
            ensure_channel_defaults(snapshot_.channels);
            if (snapshot_.state.selected_channel >= static_cast<int>(snapshot_.channels.size())) {
                snapshot_.state.selected_channel = 0;
            }
            if (snapshot_.state.selected_channel == static_cast<int>(channel_index) &&
                !snapshot_.channels[channel_index].name.empty()) {
                snapshot_.state.channel = snapshot_.channels[channel_index].name;
            }
            saveSettings();
            radio_.setChannels(snapshot_.channels);
            ble_.updateSnapshot(snapshot_);
            ble_.sendOk();
            snapshot_.state.last_event = "ble: channel updated";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::SendChannelMessage: {
            if (command.channel_index >= snapshot_.channels.size() || command.text.length() == 0) {
                ble_.sendError(err_illegal_arg);
                break;
            }
            const auto& channel = snapshot_.channels[command.channel_index];
            if (channel.name.empty()) {
                ble_.sendError(err_not_found);
                break;
            }
            select_channel(snapshot_, command.channel_index);
            snapshot_.state.compose_text = to_string(command.text);
            const bool sent = sendDirectMessage("broadcast", command.text);
            if (!sent) {
                ble_.sendError(err_table_full);
                snapshot_.state.radio_state = "tx failed";
                snapshot_.state.last_event = "ble: channel tx failed";
                meshcore::app_ingest_service_snapshot(snapshot_);
                break;
            }
            char id[16];
            std::snprintf(id, sizeof(id), "b%u", next_incoming_id_++);
            const unsigned timestamp = command.timestamp != 0 ? command.timestamp : millis() / 1000;
            addMessage({id,
                        snapshot_.state.local_node_id,
                        "Channel " + channel.name,
                        to_string(command.text),
                        timestamp,
                        true},
                       true);
            ble_.sendOk();
            snapshot_.state.radio_state = "tx queued";
            snapshot_.state.last_event = "ble: channel tx queued";
            appendLog(from_string(snapshot_.state.last_event));
            meshcore::app_ingest_service_snapshot(snapshot_);
            break;
        }
        case BleCompanionCommand::Type::GetMessage:
            syncNextBleMessage();
            break;
        case BleCompanionCommand::Type::Unknown:
        default:
            ble_.sendError(err_unsupported);
            break;
    }
}

void MeshService::syncNextBleMessage() {
    for (auto it = snapshot_.messages.rbegin(); it != snapshot_.messages.rend(); ++it) {
        if (it->outgoing || id_in_list(ble_synced_message_ids_, it->id)) {
            continue;
        }
        const uint32_t timestamp = it->timestamp != 0 ? it->timestamp : millis() / 1000;
        const std::string prefix = "Channel ";
        if (it->subject.rfind(prefix, 0) == 0) {
            uint8_t channel_index = static_cast<uint8_t>(std::min<int>(
                std::max<int>(snapshot_.state.selected_channel, 0),
                static_cast<int>(snapshot_.channels.empty() ? 0 : snapshot_.channels.size() - 1)));
            const std::string name = it->subject.substr(prefix.size());
            for (std::size_t i = 0; i < snapshot_.channels.size(); ++i) {
                if (snapshot_.channels[i].name == name) {
                    channel_index = static_cast<uint8_t>(i);
                    break;
                }
            }
            ble_.sendChannelMessage(channel_index, from_string(it->body), timestamp);
            ble_synced_message_ids_.push_back(it->id);
            return;
        }

        const auto* contact = find_node(snapshot_.nodes, from_string(it->sender));
        if (contact == nullptr || !has_public_key(contact->public_key)) {
            ble_synced_message_ids_.push_back(it->id);
            continue;
        }
        ble_.sendContactMessage(*contact, from_string(it->body), timestamp);
        ble_synced_message_ids_.push_back(it->id);
        return;
    }
    ble_.sendNoMoreMessages();
}

void MeshService::syncCoreState() {
    const auto& identity = core_.identity();
    snapshot_.state.public_key = identity.public_key;
    radio_.setLocalIdentity(core_.localIdentity());
}

void MeshService::startRadioScan() {
    radio_scan_original_frequency_khz_ = snapshot_.state.radio_frequency_khz;
    radio_scan_original_bandwidth_hz_ = snapshot_.state.radio_bandwidth_hz;
    radio_scan_original_spreading_factor_ = static_cast<uint8_t>(snapshot_.state.radio_spreading_factor);
    radio_scan_original_coding_rate_ = static_cast<uint8_t>(snapshot_.state.radio_coding_rate);
    radio_scan_original_region_ = snapshot_.state.region.c_str();
    radio_scan_index_ = 0;
    radio_scan_total_raw_ = 0;
    radio_scan_total_ok_ = 0;
    radio_scan_best_raw_ = 0;
    radio_scan_best_ok_ = 0;
    radio_scan_status_next_ms_ = 0;
    radio_scan_current_peak_rssi_ = -127;
    radio_scan_best_rssi_ = -127;
    radio_scan_best_name_ = "";
    radio_scan_best_rssi_name_ = "";
    radio_scan_active_ = true;
    snapshot_.state.radio_scan_active = true;
    snapshot_.state.radio_scan_count = sizeof(radio_scan_presets) / sizeof(radio_scan_presets[0]);
    snapshot_.state.radio_scan_raw_count = 0;
    snapshot_.state.radio_scan_decoded_count = 0;
    applyRadioScanPreset();
    snapshot_.state.last_event = "radio: receive scan started";
    appendLog(from_string(snapshot_.state.last_event));
    meshcore::app_ingest_service_snapshot(snapshot_);
}

void MeshService::stopRadioScan(bool restore_original, const char* reason) {
    if (restore_original) {
        snapshot_.state.radio_frequency_khz = radio_scan_original_frequency_khz_;
        snapshot_.state.radio_bandwidth_hz = radio_scan_original_bandwidth_hz_;
        snapshot_.state.radio_spreading_factor = radio_scan_original_spreading_factor_;
        snapshot_.state.radio_coding_rate = radio_scan_original_coding_rate_;
        snapshot_.state.region = to_string(radio_scan_original_region_);
        radio_.configure(snapshot_.state);
    }
    radio_scan_active_ = false;
    radio_scan_next_ms_ = 0;
    radio_scan_status_next_ms_ = 0;
    snapshot_.state.radio_scan_active = false;
    snapshot_.state.radio_scan_index = radio_scan_index_;
    snapshot_.state.radio_scan_count = sizeof(radio_scan_presets) / sizeof(radio_scan_presets[0]);
    snapshot_.state.radio_scan_raw_count = radio_scan_total_raw_;
    snapshot_.state.radio_scan_decoded_count = radio_scan_total_ok_;
    String status = reason == nullptr ? String("stopped") : String(reason);
    if (radio_scan_best_raw_ > 0 || radio_scan_best_ok_ > 0) {
        status += " best ";
        status += radio_scan_best_name_;
        status += " raw ";
        status += String(radio_scan_best_raw_);
    }
    if (radio_scan_best_rssi_ > -127) {
        status += " peak ";
        status += radio_scan_best_rssi_name_;
        status += " ";
        status += String(radio_scan_best_rssi_);
        status += "dBm";
    }
    snapshot_.state.radio_scan_status = to_string(status);
    meshcore::app_ingest_service_snapshot(snapshot_);
}

void MeshService::applyRadioScanPreset() {
    constexpr uint8_t preset_count = sizeof(radio_scan_presets) / sizeof(radio_scan_presets[0]);
    if (radio_scan_index_ >= preset_count) {
        return;
    }
    const auto& preset = radio_scan_presets[radio_scan_index_];
    snapshot_.state.radio_frequency_khz = preset.frequency_khz;
    snapshot_.state.radio_bandwidth_hz = preset.bandwidth_hz;
    snapshot_.state.radio_spreading_factor = preset.spreading_factor;
    snapshot_.state.radio_coding_rate = preset.coding_rate;
    snapshot_.state.region = region_for_frequency_khz(preset.frequency_khz);
    radio_.configure(snapshot_.state);
    const auto status = radio_.poll();
    radio_scan_base_rx_raw_ = status.rx_raw_count;
    radio_scan_base_rx_ok_ = status.rx_decoded_count;
    radio_scan_current_peak_rssi_ = -127;
    if (status.current_rssi > radio_scan_current_peak_rssi_) {
        radio_scan_current_peak_rssi_ = status.current_rssi;
    }
    radio_scan_next_ms_ = millis() + radio_scan_dwell_ms;
    radio_scan_status_next_ms_ = millis() + 1000;
    snapshot_.state.radio_scan_active = true;
    snapshot_.state.radio_scan_index = radio_scan_index_ + 1;
    snapshot_.state.radio_scan_count = preset_count;
    refreshRadioScanStatus(0, 0);
    meshcore::app_ingest_service_snapshot(snapshot_);
}

void MeshService::updateRadioScan(uint32_t now_ms) {
    if (!radio_scan_active_) {
        return;
    }
    const auto status = radio_.poll();
    const uint32_t raw_delta = status.rx_raw_count >= radio_scan_base_rx_raw_
                                   ? status.rx_raw_count - radio_scan_base_rx_raw_
                                   : 0;
    const uint32_t ok_delta = status.rx_decoded_count >= radio_scan_base_rx_ok_
                                  ? status.rx_decoded_count - radio_scan_base_rx_ok_
                                  : 0;
    if (status.current_rssi > radio_scan_current_peak_rssi_) {
        radio_scan_current_peak_rssi_ = status.current_rssi;
    }
    if (radio_scan_status_next_ms_ == 0 ||
        static_cast<int32_t>(now_ms - radio_scan_status_next_ms_) >= 0) {
        refreshRadioScanStatus(raw_delta, ok_delta);
        radio_scan_status_next_ms_ = now_ms + 1000;
        meshcore::app_ingest_service_snapshot(snapshot_);
    }
    if (static_cast<int32_t>(now_ms - radio_scan_next_ms_) < 0) {
        return;
    }

    constexpr uint8_t preset_count = sizeof(radio_scan_presets) / sizeof(radio_scan_presets[0]);
    const auto& preset = radio_scan_presets[radio_scan_index_];
    radio_scan_total_raw_ += raw_delta;
    radio_scan_total_ok_ += ok_delta;
    if (raw_delta > radio_scan_best_raw_ || ok_delta > radio_scan_best_ok_) {
        radio_scan_best_raw_ = raw_delta;
        radio_scan_best_ok_ = ok_delta;
        radio_scan_best_name_ = preset.name;
    }
    if (radio_scan_current_peak_rssi_ > radio_scan_best_rssi_) {
        radio_scan_best_rssi_ = radio_scan_current_peak_rssi_;
        radio_scan_best_rssi_name_ = preset.name;
    }
    Serial.printf("radio scan preset=%s freq=%ukHz bw=%skHz sf=%u cr=%u raw+%u ok+%u peak=%ddBm irq=0x%04X\n",
                  preset.name,
                  preset.frequency_khz,
                  bandwidth_khz_string(preset.bandwidth_hz).c_str(),
                  preset.spreading_factor,
                  preset.coding_rate,
                  raw_delta,
                  ok_delta,
                  radio_scan_current_peak_rssi_,
                  status.irq_flags);

    ++radio_scan_index_;
    if (radio_scan_index_ >= preset_count) {
        stopRadioScan(true, "done");
        Serial.printf("radio scan done raw=%u ok=%u best=%s bestraw=%u bestok=%u\n",
                      radio_scan_total_raw_,
                      radio_scan_total_ok_,
                      radio_scan_best_name_.length() > 0 ? radio_scan_best_name_.c_str() : "none",
                      radio_scan_best_raw_,
                      radio_scan_best_ok_);
        return;
    }
    applyRadioScanPreset();
}

void MeshService::refreshRadioScanStatus(uint32_t raw_delta, uint32_t ok_delta) {
    constexpr uint8_t preset_count = sizeof(radio_scan_presets) / sizeof(radio_scan_presets[0]);
    if (!radio_scan_active_ || radio_scan_index_ >= preset_count) {
        return;
    }
    const auto& preset = radio_scan_presets[radio_scan_index_];
    snapshot_.state.radio_scan_index = radio_scan_index_ + 1;
    snapshot_.state.radio_scan_count = preset_count;
    snapshot_.state.radio_scan_raw_count = radio_scan_total_raw_ + raw_delta;
    snapshot_.state.radio_scan_decoded_count = radio_scan_total_ok_ + ok_delta;

    String detail = "scan ";
    detail += String(radio_scan_index_ + 1);
    detail += "/";
    detail += String(preset_count);
    detail += " ";
    detail += preset.name;
    detail += " ";
    detail += String(preset.frequency_khz);
    detail += "/";
    detail += bandwidth_khz_string(preset.bandwidth_hz);
    detail += " SF";
    detail += String(preset.spreading_factor);
    if (radio_scan_current_peak_rssi_ > -127) {
        detail += " peak ";
        detail += String(radio_scan_current_peak_rssi_);
        detail += "dBm";
    }
    if (raw_delta > 0 || ok_delta > 0) {
        detail += " raw+";
        detail += String(raw_delta);
        detail += " ok+";
        detail += String(ok_delta);
    }
    snapshot_.state.radio_scan_status = to_string(detail);
}

void MeshService::printRadioScanStatus() {
    Serial.printf("radio scan active=%d step=%u/%u status=%s raw=%u ok=%u best=%s bestraw=%u bestok=%u peak=%s %ddBm\n",
                  radio_scan_active_ ? 1 : 0,
                  snapshot_.state.radio_scan_index,
                  snapshot_.state.radio_scan_count,
                  snapshot_.state.radio_scan_status.c_str(),
                  snapshot_.state.radio_scan_raw_count,
                  snapshot_.state.radio_scan_decoded_count,
                  radio_scan_best_name_.length() > 0 ? radio_scan_best_name_.c_str() : "none",
                  radio_scan_best_raw_,
                  radio_scan_best_ok_,
                  radio_scan_best_rssi_name_.length() > 0 ? radio_scan_best_rssi_name_.c_str() : "none",
                  radio_scan_best_rssi_);
}

uint32_t MeshService::currentEpochSeconds() const {
    if (clock_epoch_base_ == 0) {
        return millis() / 1000;
    }
    return clock_epoch_base_ + ((millis() - clock_millis_base_) / 1000);
}

void MeshService::pollRadio(uint32_t now_ms) {
    if (last_radio_poll_ms_ != 0 && now_ms - last_radio_poll_ms_ < 25) {
        return;
    }
    last_radio_poll_ms_ = now_ms;

    String payload;
    radio_.setNodes(snapshot_.nodes);
    if (radio_.receivePacket(payload)) {
        handleRadioPayload(payload);
    }
    updateRadioScan(now_ms);
}

void MeshService::pollHardware(uint32_t now_ms) {
    if (last_hardware_poll_ms_ != 0 && now_ms - last_hardware_poll_ms_ < 1000) {
        return;
    }
    last_hardware_poll_ms_ = now_ms;

    const auto battery = battery_.poll();
    if (battery.valid) {
        snapshot_.state.battery_percent = battery.percent;
        snapshot_.state.battery_mv = static_cast<int>(battery.volts * 1000.0f);
    }
    snapshot_.state.uptime_seconds = now_ms / 1000;
    snapshot_.state.current_epoch_seconds = currentEpochSeconds();

    const auto gps = gps_.poll(snapshot_.state.gps_enabled);
    snapshot_.state.gps_state = gps.state.c_str();
    if (gps.enabled && gps.has_fix) {
        snapshot_.state.latitude = gps.latitude;
        snapshot_.state.longitude = gps.longitude;
    }

    const auto storage = storage_.poll();
    snapshot_.state.sd_mounted = storage.mounted;
    snapshot_.state.storage_writable = storage.writable;
    snapshot_.state.storage_state = storage.state.c_str();

#if defined(ARDUINO_ARCH_ESP32)
    snapshot_.state.heap_free_bytes = ESP.getFreeHeap();
    snapshot_.state.psram_total_bytes = ESP.getPsramSize();
    snapshot_.state.psram_free_bytes = ESP.getFreePsram();
#endif

    const auto radio = radio_.poll();
    snapshot_.state.connected = radio.ready;
    snapshot_.state.radio_state = radio.state.c_str();
    snapshot_.state.last_rssi = radio.rssi;
    snapshot_.state.last_snr_quarters = static_cast<int>(radio.snr * 4.0f);
    snapshot_.state.packet_rx_count = radio.rx_decoded_count;
    snapshot_.state.packet_tx_count = radio.tx_count;
    snapshot_.state.radio_rx_raw_count = radio.rx_raw_count;
    snapshot_.state.radio_rx_decoded_count = radio.rx_decoded_count;
    snapshot_.state.radio_rx_decode_fail_count = radio.rx_decode_fail_count;
    snapshot_.state.radio_tx_fail_count = radio.tx_fail_count;
    snapshot_.state.radio_last_packet_len = radio.last_packet_len;
    snapshot_.state.radio_last_packet_type = radio.last_packet_type;
    snapshot_.state.radio_rx_active = radio.rx_active;
    snapshot_.state.radio_dio1_level = radio.dio1_level;
    snapshot_.state.radio_busy_level = radio.busy_level;
    snapshot_.state.radio_begin_result = radio.begin_result;
    snapshot_.state.radio_rx_start_result = radio.rx_start_result;
    snapshot_.state.radio_read_result = radio.read_result;
    snapshot_.state.radio_irq_flags = radio.irq_flags;
    snapshot_.state.radio_dio2_as_rf_switch = radio.dio2_as_rf_switch;
    snapshot_.state.radio_tcxo_mv = radio.tcxo_mv;
    snapshot_.state.noise_floor = radio.current_rssi;
    snapshot_.state.radio_last_decode = to_string(radio.last_decode);
    snapshot_.state.queue_len = radio.tx_busy ? 1 : 0;
    snapshot_.state.error_flags = radio.rx_decode_fail_count + radio.tx_fail_count;
    if (!snapshot_.nodes.empty()) {
        auto& node = snapshot_.nodes[static_cast<std::size_t>(snapshot_.state.selected_node) % snapshot_.nodes.size()];
        node.rssi = radio.rssi;
        node.snr = radio.snr;
    }

    core_.ingestSnapshot(snapshot_);
    syncCoreState();
    if ((last_advert_ms_ == 0 && now_ms > 10000) ||
        (last_advert_ms_ != 0 && now_ms - last_advert_ms_ > 300000)) {
        last_advert_ms_ = now_ms;
        radio_.sendSelfAdvert(from_string(snapshot_.state.device_name),
                              snapshot_.state.gps_enabled &&
                              snapshot_.state.latitude != 0.0 &&
                                  snapshot_.state.longitude != 0.0,
                              snapshot_.state.latitude,
                              snapshot_.state.longitude,
                              currentEpochSeconds());
    }
    ble_.updateSnapshot(snapshot_);
    const auto ble = ble_.status();
    snapshot_.state.ble_enabled = ble.enabled;
    snapshot_.state.ble_connected = ble.connected;
    snapshot_.state.ble_state = ble.state.c_str();
    snapshot_.state.ble_rx_frames = ble.rx_frames;
    snapshot_.state.ble_tx_frames = ble.tx_frames;
    meshcore::app_ingest_service_snapshot(snapshot_);
}
