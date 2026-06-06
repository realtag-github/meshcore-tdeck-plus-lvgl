#include "hardware_services.h"

#include "board_pins.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>

#if defined(ARDUINO_ARCH_ESP32)
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#endif

#ifndef APP_ENABLE_SD_STORAGE
#define APP_ENABLE_SD_STORAGE 0
#endif

#ifndef BOARD_LORA_DIO2_AS_RF_SWITCH
#define BOARD_LORA_DIO2_AS_RF_SWITCH 0
#endif

#ifndef BOARD_LORA_TCXO_VOLTAGE
#define BOARD_LORA_TCXO_VOLTAGE 1.8f
#endif

#if APP_ENABLE_RADIOLIB
#include <RadioLib.h>
#include <Mesh.h>
#include <Packet.h>
#include <Utils.h>
#include <helpers/AdvertDataHelpers.h>
#endif

namespace {

constexpr int board_lora_tcxo_mv = static_cast<int>((BOARD_LORA_TCXO_VOLTAGE * 1000.0f) + 0.5f);

constexpr uint8_t lilygo_keyboard_addr = 0x55;
constexpr uint8_t lilygo_keyboard_brightness_cmd = 0x01;
constexpr uint8_t lilygo_keyboard_default_brightness_cmd = 0x02;
constexpr const char* field_log_path = "/meshcore.log";
constexpr const char* message_log_path = "/meshcore_messages.log";
constexpr const char* node_log_path = "/meshcore_nodes.log";

#if APP_ENABLE_RADIOLIB
SX1262 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
volatile bool radio_packet_received = false;
constexpr uint16_t sx126x_irq_rx_done = 0x0002;
constexpr uint16_t sx126x_irq_preamble_detected = 0x0004;
constexpr uint16_t sx126x_irq_header_valid = 0x0010;

void radio_packet_received_isr() {
    radio_packet_received = true;
}
#endif

int clamp_percent(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

int battery_percent_from_volts(float volts) {
    const float normalized = (volts - 3.3f) / (4.2f - 3.3f);
    return clamp_percent(static_cast<int>(normalized * 100.0f));
}

String nmea_field(const String& line, int field_index) {
    int start = 0;
    int current = 0;
    for (int i = 0; i <= line.length(); ++i) {
        if (i == line.length() || line[i] == ',') {
            if (current == field_index) {
                return line.substring(start, i);
            }
            start = i + 1;
            ++current;
        }
    }
    return "";
}

double parse_nmea_coord(const String& value, const String& hemisphere, int degree_digits) {
    if (value.length() < degree_digits + 3) {
        return 0.0;
    }
    const double degrees = value.substring(0, degree_digits).toDouble();
    const double minutes = value.substring(degree_digits).toDouble();
    double decimal = degrees + (minutes / 60.0);
    if (hemisphere == "S" || hemisphere == "W") {
        decimal = -decimal;
    }
    return decimal;
}

float radio_frequency_mhz(unsigned frequency_khz, const String& region) {
    if (frequency_khz >= 150000 && frequency_khz <= 2500000) {
        return static_cast<float>(frequency_khz) / 1000.0f;
    }
    const float parsed = region.toFloat();
    if (parsed >= 150.0f && parsed <= 2500.0f) {
        return parsed;
    }
    if (region.startsWith("433")) {
        return 433.175f;
    }
    if (region.startsWith("868")) {
        return 868.125f;
    }
    return 915.0f;
}

String radio_frame_field(String value) {
    value.replace("|", "/");
    value.replace("\r", " ");
    value.replace("\n", " ");
    return value;
}

#if APP_ENABLE_RADIOLIB
constexpr int max_group_text_len = 150;

bool encode_meshcore_raw_custom(const String& payload, uint8_t* dest, size_t dest_size, size_t& encoded_len) {
    if (payload.length() == 0 || payload.length() > MAX_PACKET_PAYLOAD || dest_size < MAX_TRANS_UNIT) {
        return false;
    }

    mesh::Packet packet;
    packet.header = static_cast<uint8_t>((PAYLOAD_TYPE_RAW_CUSTOM << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT);
    packet.path_len = 0;
    packet.payload_len = static_cast<uint16_t>(payload.length());
    std::memcpy(packet.payload, payload.c_str(), payload.length());

    encoded_len = packet.writeTo(dest);
    return encoded_len > 0 && encoded_len <= dest_size;
}

uint8_t path_hash_size_from_mode(int mode) {
    if (mode < 0) {
        return 1;
    }
    if (mode > 2) {
        return 3;
    }
    return static_cast<uint8_t>(mode + 1);
}

mesh::GroupChannel group_channel_from_secret(const std::array<unsigned char, 16>& secret) {
    mesh::GroupChannel channel{};
    std::memset(channel.secret, 0, sizeof(channel.secret));
    std::memcpy(channel.secret, secret.data(), secret.size());
    mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, secret.size());
    return channel;
}

bool encode_meshcore_group_text(const meshcore::ChannelInfo& channel_info,
                                const String& sender_name,
                                const String& text,
                                uint32_t timestamp,
                                int path_hash_mode,
                                uint8_t* dest,
                                size_t dest_size,
                                size_t& encoded_len) {
    if (channel_info.name.empty() || sender_name.length() == 0 || text.length() == 0 ||
        dest_size < MAX_TRANS_UNIT) {
        return false;
    }

    const String prefix = sender_name + ": ";
    if (prefix.length() >= max_group_text_len) {
        return false;
    }
    const int remaining_text = max_group_text_len - static_cast<int>(prefix.length());
    const int clipped_text_len = std::min<int>(text.length(), remaining_text);
    if (clipped_text_len <= 0) {
        return false;
    }

    uint8_t plain[5 + max_group_text_len + 1] = {};
    std::memcpy(plain, &timestamp, sizeof(timestamp));
    plain[4] = 0;
    std::memcpy(&plain[5], prefix.c_str(), prefix.length());
    std::memcpy(&plain[5 + prefix.length()], text.c_str(), clipped_text_len);
    const int plain_len = 5 + prefix.length() + clipped_text_len;

    const auto channel = group_channel_from_secret(channel_info.secret);
    mesh::Packet packet;
    packet.header = static_cast<uint8_t>((PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD);
    packet.setPathHashSizeAndCount(path_hash_size_from_mode(path_hash_mode), 0);
    int payload_len = 0;
    std::memcpy(&packet.payload[payload_len], channel.hash, sizeof(channel.hash));
    payload_len += sizeof(channel.hash);
    payload_len += mesh::Utils::encryptThenMAC(channel.secret, &packet.payload[payload_len], plain, plain_len);
    if (payload_len <= 0 || payload_len > MAX_PACKET_PAYLOAD) {
        return false;
    }
    packet.payload_len = static_cast<uint16_t>(payload_len);

    encoded_len = packet.writeTo(dest);
    return encoded_len > 0 && encoded_len <= dest_size;
}

bool has_public_key(const std::array<unsigned char, meshcore::NodeInfo::public_key_size>& public_key) {
    for (const auto byte : public_key) {
        if (byte != 0 && byte != 0xff) {
            return true;
        }
    }
    return false;
}

bool has_local_identity(const std::array<uint8_t, 96>& identity) {
    for (const auto byte : identity) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

bool encode_meshcore_contact_text(const meshcore::NodeInfo& contact,
                                  const std::array<uint8_t, 96>& local_identity,
                                  const String& text,
                                  uint32_t timestamp,
                                  int path_hash_mode,
                                  uint8_t* dest,
                                  size_t dest_size,
                                  size_t& encoded_len) {
    if (!has_public_key(contact.public_key) || !has_local_identity(local_identity) ||
        text.length() == 0 || text.length() > max_group_text_len || dest_size < MAX_TRANS_UNIT) {
        return false;
    }

    mesh::LocalIdentity self;
    self.readFrom(local_identity.data(), local_identity.size());
    const mesh::Identity peer(contact.public_key.data());
    uint8_t secret[PUB_KEY_SIZE] = {};
    self.calcSharedSecret(secret, peer);

    uint8_t plain[5 + max_group_text_len + 1] = {};
    std::memcpy(plain, &timestamp, sizeof(timestamp));
    plain[4] = 0;
    std::memcpy(&plain[5], text.c_str(), text.length() + 1);
    const int plain_len = 5 + text.length();

    mesh::Packet packet;
    packet.header = static_cast<uint8_t>(PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    int payload_len = 0;
    payload_len += peer.copyHashTo(&packet.payload[payload_len]);
    payload_len += self.copyHashTo(&packet.payload[payload_len]);
    payload_len += mesh::Utils::encryptThenMAC(secret, &packet.payload[payload_len], plain, plain_len);
    if (payload_len <= 0 || payload_len > MAX_PACKET_PAYLOAD) {
        return false;
    }
    packet.payload_len = static_cast<uint16_t>(payload_len);

    if (contact.out_path_len == meshcore::NodeInfo::out_path_unknown) {
        packet.header |= ROUTE_TYPE_FLOOD;
        packet.setPathHashSizeAndCount(path_hash_size_from_mode(path_hash_mode), 0);
    } else {
        packet.header |= ROUTE_TYPE_DIRECT;
        packet.path_len = mesh::Packet::copyPath(packet.path,
                                                 contact.out_path.data(),
                                                 contact.out_path_len);
    }

    encoded_len = packet.writeTo(dest);
    return encoded_len > 0 && encoded_len <= dest_size;
}

bool encode_meshcore_self_advert(const std::array<uint8_t, 96>& local_identity,
                                 const String& name,
                                 bool has_position,
                                 double latitude,
                                 double longitude,
                                 uint32_t timestamp,
                                 int path_hash_mode,
                                 uint8_t* dest,
                                 size_t dest_size,
                                 size_t& encoded_len) {
    if (!has_local_identity(local_identity) || name.length() == 0 || dest_size < MAX_TRANS_UNIT) {
        return false;
    }

    mesh::LocalIdentity self;
    self.readFrom(local_identity.data(), local_identity.size());

    uint8_t app_data[MAX_ADVERT_DATA_SIZE] = {};
    uint8_t app_data_len = 0;
    if (has_position) {
        AdvertDataBuilder builder(ADV_TYPE_CHAT, name.c_str(), latitude, longitude);
        app_data_len = builder.encodeTo(app_data);
    } else {
        AdvertDataBuilder builder(ADV_TYPE_CHAT, name.c_str());
        app_data_len = builder.encodeTo(app_data);
    }

    mesh::Packet packet;
    packet.header = static_cast<uint8_t>((PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD);
    packet.setPathHashSizeAndCount(path_hash_size_from_mode(path_hash_mode), 0);

    int payload_len = 0;
    std::memcpy(&packet.payload[payload_len], self.pub_key, PUB_KEY_SIZE);
    payload_len += PUB_KEY_SIZE;
    std::memcpy(&packet.payload[payload_len], &timestamp, sizeof(timestamp));
    payload_len += 4;
    uint8_t* signature = &packet.payload[payload_len];
    payload_len += SIGNATURE_SIZE;
    std::memcpy(&packet.payload[payload_len], app_data, app_data_len);
    payload_len += app_data_len;
    packet.payload_len = static_cast<uint16_t>(payload_len);

    uint8_t signed_message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE] = {};
    int signed_len = 0;
    std::memcpy(&signed_message[signed_len], self.pub_key, PUB_KEY_SIZE);
    signed_len += PUB_KEY_SIZE;
    std::memcpy(&signed_message[signed_len], &timestamp, sizeof(timestamp));
    signed_len += 4;
    std::memcpy(&signed_message[signed_len], app_data, app_data_len);
    signed_len += app_data_len;
    self.sign(signature, signed_message, signed_len);

    encoded_len = packet.writeTo(dest);
    return encoded_len > 0 && encoded_len <= dest_size;
}

String payload_to_string(const uint8_t* data, uint16_t len) {
    String value;
    value.reserve(len);
    for (uint16_t i = 0; i < len; ++i) {
        if (data[i] == 0) {
            break;
        }
        value += static_cast<char>(data[i]);
    }
    return value;
}

String bytes_to_hex(const uint8_t* data, size_t len) {
    String value;
    value.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02X", data[i]);
        value += hex;
    }
    return value;
}

String radio_record_field(String value) {
    value.replace("|", "/");
    value.replace("\r", " ");
    value.replace("\n", " ");
    return value;
}

bool decode_meshcore_group_text(const mesh::Packet& packet,
                                const std::vector<meshcore::ChannelInfo>& channels,
                                String& payload) {
    if (packet.payload_len <= PATH_HASH_SIZE + CIPHER_MAC_SIZE) {
        return false;
    }

    for (const auto& channel_info : channels) {
        if (channel_info.name.empty()) {
            continue;
        }

        const auto channel = group_channel_from_secret(channel_info.secret);
        if (std::memcmp(packet.payload, channel.hash, PATH_HASH_SIZE) != 0) {
            continue;
        }

        uint8_t decrypted[MAX_PACKET_PAYLOAD + 1] = {};
        const int len = mesh::Utils::MACThenDecrypt(channel.secret,
                                                    decrypted,
                                                    &packet.payload[PATH_HASH_SIZE],
                                                    packet.payload_len - PATH_HASH_SIZE);
        if (len < 5) {
            continue;
        }

        const uint8_t text_type = decrypted[4];
        if ((text_type >> 2) != 0) {
            continue;
        }

        uint32_t timestamp = 0;
        std::memcpy(&timestamp, decrypted, sizeof(timestamp));
        decrypted[std::min<int>(len, MAX_PACKET_PAYLOAD)] = 0;
        payload = "CH1|";
        payload += channel_info.name.c_str();
        payload += "|";
        payload += String(timestamp);
        payload += "|";
        payload += reinterpret_cast<const char*>(&decrypted[5]);
        return true;
    }
    return false;
}

bool decode_meshcore_advert(const mesh::Packet& packet,
                            const std::array<uint8_t, 96>& local_identity,
                            String& payload,
                            String* detail = nullptr) {
    if (packet.getPayloadType() != PAYLOAD_TYPE_ADVERT) {
        if (detail != nullptr) {
            *detail = "not advert";
        }
        return false;
    }
    if (packet.payload_len < PUB_KEY_SIZE + 4 + SIGNATURE_SIZE + 1) {
        if (detail != nullptr) {
            *detail = "advert short payload " + String(packet.payload_len);
        }
        return false;
    }

    int offset = 0;
    mesh::Identity id(&packet.payload[offset]);
    offset += PUB_KEY_SIZE;
    uint32_t timestamp = 0;
    std::memcpy(&timestamp, &packet.payload[offset], sizeof(timestamp));
    offset += 4;
    const uint8_t* signature = &packet.payload[offset];
    offset += SIGNATURE_SIZE;
    const uint8_t* app_data = &packet.payload[offset];
    int app_data_len = packet.payload_len - offset;
    if (app_data_len > MAX_ADVERT_DATA_SIZE) {
        app_data_len = MAX_ADVERT_DATA_SIZE;
    }
    if (has_local_identity(local_identity) &&
        std::memcmp(id.pub_key, &local_identity[PRV_KEY_SIZE], PUB_KEY_SIZE) == 0) {
        if (detail != nullptr) {
            *detail = "advert self";
        }
        return false;
    }

    uint8_t signed_message[PUB_KEY_SIZE + 4 + MAX_ADVERT_DATA_SIZE] = {};
    int signed_len = 0;
    std::memcpy(&signed_message[signed_len], id.pub_key, PUB_KEY_SIZE);
    signed_len += PUB_KEY_SIZE;
    std::memcpy(&signed_message[signed_len], &timestamp, sizeof(timestamp));
    signed_len += 4;
    std::memcpy(&signed_message[signed_len], app_data, app_data_len);
    signed_len += app_data_len;
    if (!id.verify(signature, signed_message, signed_len)) {
        if (detail != nullptr) {
            *detail = "advert bad signature app=" + String(app_data_len);
        }
        return false;
    }

    AdvertDataParser parser(app_data, static_cast<uint8_t>(app_data_len));
    if (!parser.isValid()) {
        if (detail != nullptr) {
            *detail = "advert invalid app=" + String(app_data_len);
        }
        return false;
    }

    const String pub_hex = bytes_to_hex(id.pub_key, PUB_KEY_SIZE);
    payload = "ADV1|0x";
    payload += bytes_to_hex(id.pub_key, 4);
    payload += "|";
    payload += String(timestamp);
    payload += "|";
    payload += String(parser.hasLatLon() ? parser.getLat() : 0.0, 6);
    payload += "|";
    payload += String(parser.hasLatLon() ? parser.getLon() : 0.0, 6);
    payload += "|";
    payload += pub_hex;
    payload += "|";
    payload += String(parser.getType());
    payload += "|";
    payload += parser.hasName() ? radio_record_field(parser.getName()) : String("");
    if (detail != nullptr) {
        *detail = "advert ok";
    }
    return true;
}

bool transmit_packet(mesh::Packet& packet) {
    uint8_t encoded[MAX_TRANS_UNIT] = {};
    const uint8_t encoded_len = packet.writeTo(encoded);
    return encoded_len > 0 && radio.transmit(encoded, encoded_len) == RADIOLIB_ERR_NONE;
}

void send_meshcore_ack_or_path(const mesh::Packet& received,
                               const meshcore::NodeInfo& sender,
                               const std::array<uint8_t, 96>& local_identity,
                               const uint8_t* secret,
                               const uint8_t* decrypted,
                               int decrypted_len,
                               int path_hash_mode) {
    if (decrypted_len <= 5 || !has_public_key(sender.public_key) ||
        !has_local_identity(local_identity)) {
        return;
    }

    uint32_t ack_hash = 0;
    mesh::Utils::sha256(reinterpret_cast<uint8_t*>(&ack_hash),
                        sizeof(ack_hash),
                        decrypted,
                        5 + std::strlen(reinterpret_cast<const char*>(&decrypted[5])),
                        sender.public_key.data(),
                        PUB_KEY_SIZE);

    if (received.isRouteFlood()) {
        const uint8_t received_hash_size = (received.path_len >> 6) + 1;
        const uint8_t received_hash_count = received.path_len & 63;
        const uint8_t received_path_bytes = received_hash_size * received_hash_count;
        if (received_path_bytes + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE + PATH_HASH_SIZE * 2 + 6 > MAX_PACKET_PAYLOAD) {
            return;
        }

        mesh::Packet path_packet;
        path_packet.header = static_cast<uint8_t>((PAYLOAD_TYPE_PATH << PH_TYPE_SHIFT) | ROUTE_TYPE_FLOOD);
        path_packet.setPathHashSizeAndCount(path_hash_size_from_mode(path_hash_mode), 0);

        int payload_len = 0;
        path_packet.payload[payload_len++] = sender.public_key[0];
        path_packet.payload[payload_len++] = local_identity[PRV_KEY_SIZE];
        uint8_t path_data[MAX_PACKET_PAYLOAD] = {};
        int path_len = 0;
        path_data[path_len++] = received.path_len;
        std::memcpy(&path_data[path_len], received.path, received_path_bytes);
        path_len += received_path_bytes;
        path_data[path_len++] = PAYLOAD_TYPE_ACK;
        std::memcpy(&path_data[path_len], &ack_hash, sizeof(ack_hash));
        path_len += sizeof(ack_hash);

        payload_len += mesh::Utils::encryptThenMAC(secret,
                                                   &path_packet.payload[payload_len],
                                                   path_data,
                                                   path_len);
        path_packet.payload_len = static_cast<uint16_t>(payload_len);
        transmit_packet(path_packet);
        return;
    }

    mesh::Packet ack;
    ack.header = static_cast<uint8_t>(PAYLOAD_TYPE_ACK << PH_TYPE_SHIFT);
    std::memcpy(ack.payload, &ack_hash, sizeof(ack_hash));
    ack.payload_len = sizeof(ack_hash);
    if (sender.out_path_len == meshcore::NodeInfo::out_path_unknown) {
        ack.header |= ROUTE_TYPE_FLOOD;
        ack.setPathHashSizeAndCount(path_hash_size_from_mode(path_hash_mode), 0);
    } else {
        ack.header |= ROUTE_TYPE_DIRECT;
        ack.path_len = mesh::Packet::copyPath(ack.path, sender.out_path.data(), sender.out_path_len);
    }
    transmit_packet(ack);
}

bool decode_meshcore_contact_text(const mesh::Packet& packet,
                                  const std::array<uint8_t, 96>& local_identity,
                                  const std::vector<meshcore::NodeInfo>& nodes,
                                  int path_hash_mode,
                                  String& payload) {
    if (!has_local_identity(local_identity) ||
        packet.payload_len <= (PATH_HASH_SIZE * 2) + CIPHER_MAC_SIZE ||
        packet.getPayloadType() != PAYLOAD_TYPE_TXT_MSG) {
        return false;
    }

    mesh::LocalIdentity self;
    self.readFrom(local_identity.data(), local_identity.size());
    int offset = 0;
    const uint8_t* dest_hash = &packet.payload[offset];
    offset += PATH_HASH_SIZE;
    const uint8_t* src_hash = &packet.payload[offset];
    offset += PATH_HASH_SIZE;
    if (!self.isHashMatch(dest_hash, PATH_HASH_SIZE)) {
        return false;
    }

    for (const auto& node : nodes) {
        if (!has_public_key(node.public_key) || node.public_key[0] != src_hash[0]) {
            continue;
        }

        const mesh::Identity peer(node.public_key.data());
        uint8_t secret[PUB_KEY_SIZE] = {};
        self.calcSharedSecret(secret, peer);

        uint8_t decrypted[MAX_PACKET_PAYLOAD + 1] = {};
        const int len = mesh::Utils::MACThenDecrypt(secret,
                                                    decrypted,
                                                    &packet.payload[offset],
                                                    packet.payload_len - offset);
        if (len <= 5) {
            continue;
        }

        const uint8_t text_type = decrypted[4] >> 2;
        if (text_type != 0) {
            continue;
        }

        uint32_t timestamp = 0;
        std::memcpy(&timestamp, decrypted, sizeof(timestamp));
        decrypted[std::min<int>(len, MAX_PACKET_PAYLOAD)] = 0;
        payload = "MC2|";
        payload += node.short_id.c_str();
        payload += "|";
        payload += String(timestamp);
        payload += "|";
        payload += reinterpret_cast<const char*>(&decrypted[5]);
        send_meshcore_ack_or_path(packet, node, local_identity, secret, decrypted, len, path_hash_mode);
        return true;
    }
    return false;
}

bool decode_meshcore_path(const mesh::Packet& packet,
                          const std::array<uint8_t, 96>& local_identity,
                          const std::vector<meshcore::NodeInfo>& nodes,
                          String& payload) {
    if (!has_local_identity(local_identity) ||
        packet.payload_len <= (PATH_HASH_SIZE * 2) + CIPHER_MAC_SIZE ||
        packet.getPayloadType() != PAYLOAD_TYPE_PATH) {
        return false;
    }

    mesh::LocalIdentity self;
    self.readFrom(local_identity.data(), local_identity.size());
    int offset = 0;
    const uint8_t* dest_hash = &packet.payload[offset];
    offset += PATH_HASH_SIZE;
    const uint8_t* src_hash = &packet.payload[offset];
    offset += PATH_HASH_SIZE;
    if (!self.isHashMatch(dest_hash, PATH_HASH_SIZE)) {
        return false;
    }

    for (const auto& node : nodes) {
        if (!has_public_key(node.public_key) || node.public_key[0] != src_hash[0]) {
            continue;
        }

        const mesh::Identity peer(node.public_key.data());
        uint8_t secret[PUB_KEY_SIZE] = {};
        self.calcSharedSecret(secret, peer);

        uint8_t decrypted[MAX_PACKET_PAYLOAD + 1] = {};
        const int len = mesh::Utils::MACThenDecrypt(secret,
                                                    decrypted,
                                                    &packet.payload[offset],
                                                    packet.payload_len - offset);
        if (len < 1) {
            continue;
        }

        const uint8_t returned_path_len = decrypted[0];
        const uint8_t hash_size = (returned_path_len >> 6) + 1;
        const uint8_t hash_count = returned_path_len & 63;
        const uint8_t path_bytes = hash_size * hash_count;
        if (path_bytes > meshcore::NodeInfo::max_path_size || len < 1 + path_bytes) {
            continue;
        }

        payload = "PATH1|";
        payload += node.short_id.c_str();
        payload += "|";
        payload += String(returned_path_len);
        payload += "|";
        payload += bytes_to_hex(&decrypted[1], path_bytes);
        return true;
    }
    return false;
}

bool starts_with_ascii_frame(const uint8_t* buffer, int len, const char* prefix) {
    const int prefix_len = std::strlen(prefix);
    return len >= prefix_len && std::memcmp(buffer, prefix, prefix_len) == 0;
}
#endif

}  // namespace

bool BoardPowerService::begin() {
#if PIN_POWERON >= 0
    pinMode(PIN_POWERON, OUTPUT);
    digitalWrite(PIN_POWERON, HIGH);
#endif
#if PIN_DISPLAY_BL >= 0
    pinMode(PIN_DISPLAY_BL, OUTPUT);
    digitalWrite(PIN_DISPLAY_BL, HIGH);
#endif
    return true;
}

bool BatteryService::begin() {
#if PIN_BATTERY_ADC >= 0
    pinMode(PIN_BATTERY_ADC, INPUT);
    status_.valid = true;
#else
    status_.valid = false;
#endif
    return true;
}

BatteryStatus BatteryService::poll() {
#if PIN_BATTERY_ADC >= 0
    const int raw = analogRead(PIN_BATTERY_ADC);
    const float adc_volts = (static_cast<float>(raw) / 4095.0f) * 3.3f;
    status_.volts = adc_volts * 2.0f;
    status_.percent = battery_percent_from_volts(status_.volts);
    status_.valid = true;
#else
    status_.valid = false;
#endif
    return status_;
}

bool GpsService::begin() {
    status_.enabled = false;
    status_.has_fix = false;
    status_.state = "off";
#if PIN_GPS_RX >= 0 && PIN_GPS_TX >= 0
    Serial1.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
#endif
    return true;
}

GpsStatus GpsService::poll(bool enabled) {
    status_.enabled = enabled;
    if (!enabled) {
        status_.has_fix = false;
        status_.state = "off";
        return status_;
    }

#if PIN_GPS_RX >= 0 && PIN_GPS_TX >= 0
    while (Serial1.available() > 0) {
        const char ch = static_cast<char>(Serial1.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            parseLine(line_);
            line_ = "";
            continue;
        }
        if (line_.length() < 96) {
            line_ += ch;
        }
    }
    if (!status_.has_fix) {
        status_.state = "searching";
    }
#else
    status_.state = "not configured";
#endif
    return status_;
}

void GpsService::parseLine(const String& line) {
    if (line.startsWith("$GPGGA") || line.startsWith("$GNGGA")) {
        parseGga(line);
    } else if (line.startsWith("$GPRMC") || line.startsWith("$GNRMC")) {
        parseRmc(line);
    }
}

void GpsService::parseGga(const String& line) {
    const String lat = nmea_field(line, 2);
    const String ns = nmea_field(line, 3);
    const String lon = nmea_field(line, 4);
    const String ew = nmea_field(line, 5);
    const String fix = nmea_field(line, 6);
    const String sats = nmea_field(line, 7);
    if (fix.toInt() <= 0 || lat.length() == 0 || lon.length() == 0) {
        status_.has_fix = false;
        status_.state = "no fix";
        return;
    }
    status_.latitude = parse_nmea_coord(lat, ns, 2);
    status_.longitude = parse_nmea_coord(lon, ew, 3);
    status_.satellites = static_cast<unsigned>(sats.toInt());
    status_.has_fix = true;
    status_.state = "fix";
}

void GpsService::parseRmc(const String& line) {
    const String valid = nmea_field(line, 2);
    const String lat = nmea_field(line, 3);
    const String ns = nmea_field(line, 4);
    const String lon = nmea_field(line, 5);
    const String ew = nmea_field(line, 6);
    if (valid != "A" || lat.length() == 0 || lon.length() == 0) {
        status_.has_fix = false;
        status_.state = "no fix";
        return;
    }
    status_.latitude = parse_nmea_coord(lat, ns, 2);
    status_.longitude = parse_nmea_coord(lon, ew, 3);
    status_.has_fix = true;
    status_.state = "fix";
}

bool StorageService::begin() {
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0
#if defined(ARDUINO_ARCH_ESP32)
    SPI.begin(PIN_SDCARD_SCLK, PIN_SDCARD_MISO, PIN_SDCARD_MOSI, PIN_SDCARD_CS);
    status_.mounted = SD.begin(PIN_SDCARD_CS, SPI);
    status_.writable = status_.mounted;
    status_.state = status_.mounted ? "mounted" : "mount failed";
#else
    status_.mounted = false;
    status_.writable = false;
    status_.state = "host";
#endif
#else
    status_.mounted = false;
    status_.writable = false;
    status_.state = APP_ENABLE_SD_STORAGE ? "not configured" : "disabled";
#endif
    return true;
}

StorageStatus StorageService::poll() {
    return status_;
}

bool StorageService::appendLog(const String& line) {
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0 && defined(ARDUINO_ARCH_ESP32)
    if (!status_.mounted || !status_.writable) {
        return false;
    }
    File file = SD.open(field_log_path, FILE_APPEND);
    if (!file) {
        status_.writable = false;
        status_.state = "log open failed";
        return false;
    }
    file.println(line);
    file.close();
    return true;
#else
    (void)line;
    return false;
#endif
}

bool StorageService::appendMessageRecord(const String& line) {
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0 && defined(ARDUINO_ARCH_ESP32)
    if (!status_.mounted || !status_.writable) {
        return false;
    }
    File file = SD.open(message_log_path, FILE_APPEND);
    if (!file) {
        status_.writable = false;
        status_.state = "message open failed";
        return false;
    }
    file.println(line);
    file.close();
    return true;
#else
    (void)line;
    return false;
#endif
}

std::vector<String> StorageService::readMessageRecords(std::size_t max_records) {
    std::vector<String> records;
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0 && defined(ARDUINO_ARCH_ESP32)
    if (!status_.mounted || max_records == 0) {
        return records;
    }
    File file = SD.open(message_log_path, FILE_READ);
    if (!file) {
        return records;
    }
    while (file.available() > 0) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            continue;
        }
        records.push_back(line);
        if (records.size() > max_records) {
            records.erase(records.begin());
        }
    }
    file.close();
#else
    (void)max_records;
#endif
    return records;
}

bool StorageService::clearMessageRecords() {
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0 && defined(ARDUINO_ARCH_ESP32)
    if (!status_.mounted || !status_.writable) {
        return false;
    }
    if (SD.exists(message_log_path) && !SD.remove(message_log_path)) {
        status_.state = "message clear failed";
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool StorageService::appendNodeRecord(const String& line) {
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0 && defined(ARDUINO_ARCH_ESP32)
    if (!status_.mounted || !status_.writable) {
        return false;
    }
    File file = SD.open(node_log_path, FILE_APPEND);
    if (!file) {
        status_.writable = false;
        status_.state = "node open failed";
        return false;
    }
    file.println(line);
    file.close();
    return true;
#else
    (void)line;
    return false;
#endif
}

std::vector<String> StorageService::readNodeRecords(std::size_t max_records) {
    std::vector<String> records;
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0 && defined(ARDUINO_ARCH_ESP32)
    if (!status_.mounted || max_records == 0) {
        return records;
    }
    File file = SD.open(node_log_path, FILE_READ);
    if (!file) {
        return records;
    }
    while (file.available() > 0) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            continue;
        }
        records.push_back(line);
        if (records.size() > max_records) {
            records.erase(records.begin());
        }
    }
    file.close();
#else
    (void)max_records;
#endif
    return records;
}

bool StorageService::clearNodeRecords() {
#if APP_ENABLE_SD_STORAGE && PIN_SDCARD_CS >= 0 && defined(ARDUINO_ARCH_ESP32)
    if (!status_.mounted || !status_.writable) {
        return false;
    }
    if (SD.exists(node_log_path) && !SD.remove(node_log_path)) {
        status_.state = "node clear failed";
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool AudioService::begin() {
#if PIN_SPEAKER >= 0
    pinMode(PIN_SPEAKER, OUTPUT);
    digitalWrite(PIN_SPEAKER, LOW);
#endif
    return true;
}

void AudioService::beep(unsigned frequency_hz, unsigned duration_ms, bool enabled) {
#if PIN_SPEAKER >= 0
    if (!enabled) {
        return;
    }
    tone(PIN_SPEAKER, frequency_hz, duration_ms);
#else
    (void)frequency_hz;
    (void)duration_ms;
    (void)enabled;
#endif
}

bool InputService::begin() {
#if PIN_KEYBOARD_INT >= 0
    pinMode(PIN_KEYBOARD_INT, INPUT_PULLUP);
#endif
#if PIN_TRACKBALL_UP >= 0
    pinMode(PIN_TRACKBALL_UP, INPUT_PULLUP);
#endif
#if PIN_TRACKBALL_DOWN >= 0
    pinMode(PIN_TRACKBALL_DOWN, INPUT_PULLUP);
#endif
#if PIN_TRACKBALL_LEFT >= 0
    pinMode(PIN_TRACKBALL_LEFT, INPUT_PULLUP);
#endif
#if PIN_TRACKBALL_RIGHT >= 0
    pinMode(PIN_TRACKBALL_RIGHT, INPUT_PULLUP);
#endif
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setTimeOut(20);
    Wire.beginTransmission(lilygo_keyboard_addr);
    keyboard_present_ = Wire.endTransmission() == 0;
    if (keyboard_present_) {
        Wire.beginTransmission(lilygo_keyboard_addr);
        Wire.write(lilygo_keyboard_default_brightness_cmd);
        Wire.write(127);
        Wire.endTransmission();
        Wire.beginTransmission(lilygo_keyboard_addr);
        Wire.write(lilygo_keyboard_brightness_cmd);
        Wire.write(64);
        Wire.endTransmission();
    }
    return true;
}

void InputService::poll() {
    if (keyboard_present_) {
        Wire.requestFrom(lilygo_keyboard_addr, static_cast<uint8_t>(1));
        while (Wire.available() > 0) {
            const char key = static_cast<char>(Wire.read());
            if (key != 0x00 && key != static_cast<char>(0xff)) {
                pushKey(key);
            }
        }
    }
    pollTrackball();
}

bool InputService::readKey(char& key) {
    if (head_ == tail_) {
        return false;
    }
    key = queue_[head_];
    head_ = static_cast<uint8_t>((head_ + 1) % queue_size);
    return true;
}

void InputService::pushKey(char key) {
    const uint8_t next_tail = static_cast<uint8_t>((tail_ + 1) % queue_size);
    if (next_tail == head_) {
        return;
    }
    queue_[tail_] = key;
    tail_ = next_tail;
}

void InputService::pollTrackball() {
#if PIN_TRACKBALL_UP >= 0
    const bool up = digitalRead(PIN_TRACKBALL_UP) == HIGH;
    if (last_up_ && !up) {
        pushKey(INPUT_TRACKBALL_UP);
    }
    last_up_ = up;
#endif
#if PIN_TRACKBALL_DOWN >= 0
    const bool down = digitalRead(PIN_TRACKBALL_DOWN) == HIGH;
    if (last_down_ && !down) {
        pushKey(INPUT_TRACKBALL_DOWN);
    }
    last_down_ = down;
#endif
#if PIN_TRACKBALL_LEFT >= 0
    const bool left = digitalRead(PIN_TRACKBALL_LEFT) == HIGH;
    if (last_left_ && !left) {
        pushKey(INPUT_TRACKBALL_LEFT);
    }
    last_left_ = left;
#endif
#if PIN_TRACKBALL_RIGHT >= 0
    const bool right = digitalRead(PIN_TRACKBALL_RIGHT) == HIGH;
    if (last_right_ && !right) {
        pushKey(INPUT_TRACKBALL_RIGHT);
    }
    last_right_ = right;
#endif
}

bool RadioService::begin(const meshcore::AppState& state) {
    configure(state);
    status_.tx_busy = false;
#if APP_ENABLE_RADIOLIB
    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);
    const int result = radio.begin(radio_frequency_mhz(frequency_khz_, region_),
                                   static_cast<float>(bandwidth_hz_) / 1000.0f,
                                   static_cast<uint8_t>(spreading_factor_),
                                   static_cast<uint8_t>(coding_rate_),
                                   RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                                   tx_power_dbm_,
                                   16,
                                   BOARD_LORA_TCXO_VOLTAGE);
    status_.begin_result = result;
    status_.dio2_as_rf_switch = BOARD_LORA_DIO2_AS_RF_SWITCH != 0;
    status_.tcxo_mv = board_lora_tcxo_mv;
    hardware_ready_ = result == RADIOLIB_ERR_NONE;
    status_.ready = hardware_ready_;
    if (hardware_ready_) {
        radio.setCRC(1);
        radio.setCurrentLimit(140);
        radio.setDio2AsRfSwitch(BOARD_LORA_DIO2_AS_RF_SWITCH);
        radio.setRxBoostedGainMode(true);
        radio.setOutputPower(tx_power_dbm_);
        radio.setPacketReceivedAction(radio_packet_received_isr);
        startReceive();
    } else {
        status_.state = "sx1262 err " + String(result);
    }
#else
    hardware_ready_ = false;
    status_.ready = false;
    status_.state = "radiolib disabled";
#endif
    return true;
}

RadioStatus RadioService::poll() {
    pollTransmitComplete();
#if APP_ENABLE_RADIOLIB
    if (hardware_ready_) {
        status_.rx_active = rx_active_;
        status_.dio1_level = digitalRead(PIN_LORA_DIO1);
        status_.busy_level = digitalRead(PIN_LORA_BUSY);
        status_.irq_flags = radio.getIrqFlags();
        if (rx_active_) {
            const int current_rssi = static_cast<int>(radio.getRSSI(false));
            if (current_rssi > -150 && current_rssi < 20) {
                status_.current_rssi = current_rssi;
            }
        }
    }
#endif
    return status_;
}

RadioTxResult RadioService::startPacketTransmit(const uint8_t* data,
                                                size_t len,
                                                const char* queued_state,
                                                const char* done_state,
                                                const char* error_prefix) {
#if APP_ENABLE_RADIOLIB
    if (!hardware_ready_) {
        status_.tx_busy = false;
        status_.state = "radio not ready";
        return {false, status_.state};
    }
    if (tx_in_progress_) {
        return {false, "radio tx busy"};
    }
    rx_active_ = false;
    radio_packet_received = false;
    status_.tx_busy = true;
    status_.state = queued_state;
    const int result = radio.startTransmit(data, len);
    if (result != RADIOLIB_ERR_NONE) {
        tx_in_progress_ = false;
        status_.tx_busy = false;
        ++status_.tx_fail_count;
        status_.state = String(error_prefix) + String(result);
        startReceive();
        return {false, status_.state};
    }
    tx_in_progress_ = true;
    ++status_.tx_count;
    tx_done_state_ = done_state;
    tx_timeout_state_ = String(error_prefix) + "timeout";
    tx_deadline_ms_ = millis() + 8000U;
    return {true, queued_state};
#else
    (void)data;
    (void)len;
    (void)queued_state;
    (void)done_state;
    (void)error_prefix;
    status_.tx_busy = false;
    status_.state = "radio not ready";
    return {false, status_.state};
#endif
}

void RadioService::pollTransmitComplete() {
#if APP_ENABLE_RADIOLIB
    if (!tx_in_progress_) {
        return;
    }
    const uint32_t now = millis();
    const bool timed_out = static_cast<int32_t>(now - tx_deadline_ms_) >= 0;
    if (!timed_out && digitalRead(PIN_LORA_DIO1) == LOW) {
        return;
    }
    const int result = radio.finishTransmit();
    tx_in_progress_ = false;
    status_.tx_busy = false;
    status_.state = timed_out ? tx_timeout_state_ : tx_done_state_;
    if (!timed_out && result != RADIOLIB_ERR_NONE) {
        status_.state = "tx finish err " + String(result);
    }
    if (timed_out || result != RADIOLIB_ERR_NONE) {
        ++status_.tx_fail_count;
    }
    startReceive();
#endif
}

RadioTxResult RadioService::sendDirect(const String& node_id, const String& text) {
    if (!status_.ready) {
        return {false, "radio not ready"};
    }
    if (node_id.length() == 0 || text.length() == 0) {
        return {false, "missing target/text"};
    }
#if APP_ENABLE_RADIOLIB
    if (hardware_ready_) {
        String frame = "MC1|";
        frame += radio_frame_field(local_node_id_);
        frame += "|";
        frame += radio_frame_field(node_id);
        frame += "|";
        frame += radio_frame_field(text);
        uint8_t encoded[MAX_TRANS_UNIT] = {};
        size_t encoded_len = 0;
        if (!encode_meshcore_raw_custom(frame, encoded, sizeof(encoded), encoded_len)) {
            status_.state = "tx encode failed";
            return {false, status_.state};
        }
        return startPacketTransmit(encoded,
                                   encoded_len,
                                   "meshcore raw tx queued",
                                   "meshcore raw tx done",
                                   "tx err ");
    }
#endif
    status_.tx_busy = false;
    status_.state = "radio not ready";
    return {false, status_.state};
}

RadioTxResult RadioService::sendContactMessage(const meshcore::NodeInfo& contact,
                                               const String& text,
                                               uint32_t timestamp) {
    if (!status_.ready) {
        return {false, "radio not ready"};
    }
    if (contact.short_id.empty() || text.length() == 0) {
        return {false, "missing contact/text"};
    }
#if APP_ENABLE_RADIOLIB
    if (hardware_ready_) {
        uint8_t encoded[MAX_TRANS_UNIT] = {};
        size_t encoded_len = 0;
        if (!encode_meshcore_contact_text(contact,
                                          local_identity_,
                                          text,
                                          timestamp,
                                          path_hash_mode_,
                                          encoded,
                                          sizeof(encoded),
                                          encoded_len)) {
            status_.state = "contact encode failed";
            return {false, status_.state};
        }
        return startPacketTransmit(encoded,
                                   encoded_len,
                                   "meshcore contact tx queued",
                                   "meshcore contact tx done",
                                   "contact tx err ");
    }
#else
    (void)contact;
    (void)text;
    (void)timestamp;
#endif
    status_.tx_busy = false;
    status_.state = "radio not ready";
    return {false, status_.state};
}

RadioTxResult RadioService::sendChannelMessage(uint8_t channel_index,
                                               const String& sender_name,
                                               const String& text,
                                               uint32_t timestamp) {
    if (!status_.ready) {
        return {false, "radio not ready"};
    }
    if (channel_index >= channels_.size() || text.length() == 0) {
        return {false, "missing channel/text"};
    }
#if APP_ENABLE_RADIOLIB
    if (hardware_ready_) {
        uint8_t encoded[MAX_TRANS_UNIT] = {};
        size_t encoded_len = 0;
        if (!encode_meshcore_group_text(channels_[channel_index],
                                        sender_name,
                                        text,
                                        timestamp,
                                        path_hash_mode_,
                                        encoded,
                                        sizeof(encoded),
                                        encoded_len)) {
            status_.state = "channel encode failed";
            return {false, status_.state};
        }
        return startPacketTransmit(encoded,
                                   encoded_len,
                                   "meshcore channel tx queued",
                                   "meshcore channel tx done",
                                   "channel tx err ");
    }
#endif
    status_.tx_busy = false;
    status_.state = "radio not ready";
    return {false, status_.state};
}

RadioTxResult RadioService::sendSelfAdvert(const String& name,
                                           bool has_position,
                                           double latitude,
                                           double longitude,
                                           uint32_t timestamp) {
    if (!status_.ready) {
        return {false, "radio not ready"};
    }
#if APP_ENABLE_RADIOLIB
    if (hardware_ready_) {
        uint8_t encoded[MAX_TRANS_UNIT] = {};
        size_t encoded_len = 0;
        if (!encode_meshcore_self_advert(local_identity_,
                                         name,
                                         has_position,
                                         latitude,
                                         longitude,
                                         timestamp,
                                         path_hash_mode_,
                                         encoded,
                                         sizeof(encoded),
                                         encoded_len)) {
            status_.state = "advert encode failed";
            return {false, status_.state};
        }
        return startPacketTransmit(encoded,
                                   encoded_len,
                                   "meshcore advert tx queued",
                                   "meshcore advert tx done",
                                   "advert tx err ");
    }
#else
    (void)name;
    (void)has_position;
    (void)latitude;
    (void)longitude;
    (void)timestamp;
#endif
    status_.tx_busy = false;
    status_.state = "radio not ready";
    return {false, status_.state};
}

RadioTxResult RadioService::sendPosition(const String& node_id, double latitude, double longitude) {
    if (!status_.ready) {
        return {false, "radio not ready"};
    }
    if (node_id.length() == 0) {
        return {false, "missing target"};
    }
    String frame = "POS1|";
    frame += radio_frame_field(local_node_id_);
    frame += "|";
    frame += radio_frame_field(node_id);
    frame += "|";
    frame += String(latitude, 6);
    frame += "|";
    frame += String(longitude, 6);
#if APP_ENABLE_RADIOLIB
    if (hardware_ready_) {
        uint8_t encoded[MAX_TRANS_UNIT] = {};
        size_t encoded_len = 0;
        if (!encode_meshcore_raw_custom(frame, encoded, sizeof(encoded), encoded_len)) {
            status_.state = "pos encode failed";
            return {false, status_.state};
        }
        return startPacketTransmit(encoded,
                                   encoded_len,
                                   "meshcore pos tx queued",
                                   "meshcore pos tx done",
                                   "pos tx err ");
    }
#endif
    status_.tx_busy = false;
    status_.state = "radio not ready";
    return {false, status_.state};
}

bool RadioService::receivePacket(String& payload) {
#if APP_ENABLE_RADIOLIB
    if (!hardware_ready_) {
        return false;
    }
    if (tx_in_progress_) {
        pollTransmitComplete();
        return false;
    }
    if (!rx_active_) {
        startReceive();
    }
    if (!radio_packet_received) {
        const uint16_t irq_flags = radio.getIrqFlags();
        if ((irq_flags & sx126x_irq_rx_done) == 0) {
            return false;
        }
        status_.last_decode = "irq poll";
    }
    radio_packet_received = false;

    uint8_t buffer[MAX_TRANS_UNIT] = {};
    int len = radio.getPacketLength();
    if (len <= 0) {
        rx_active_ = false;
        status_.state = "rx empty";
        startReceive();
        return false;
    }
    if (len > static_cast<int>(sizeof(buffer))) {
        len = sizeof(buffer) - 1;
    }
    const int result = radio.readData(buffer, static_cast<size_t>(len));
    status_.read_result = result;
    rx_active_ = false;
    if (result == RADIOLIB_ERR_NONE) {
        status_.last_packet_hex = bytes_to_hex(buffer, static_cast<size_t>(len));
        status_.rssi = static_cast<int>(radio.getRSSI());
        status_.snr = radio.getSNR();
        ++status_.rx_raw_count;
        status_.last_packet_len = static_cast<unsigned>(len);
        status_.last_packet_type = 255;
        startReceive();
        if (len <= 0) {
            status_.state = "rx empty";
            status_.last_decode = "empty";
            ++status_.rx_decode_fail_count;
            return false;
        }

        if (starts_with_ascii_frame(buffer, len, "MC1|") ||
            starts_with_ascii_frame(buffer, len, "POS1|")) {
            payload = reinterpret_cast<const char*>(buffer);
            ++status_.rx_decoded_count;
            status_.last_decode = "legacy";
            status_.state = "rx legacy packet";
            return payload.length() > 0;
        }

        mesh::Packet packet;
        if (packet.readFrom(buffer, static_cast<uint8_t>(len))) {
            status_.last_packet_type = packet.getPayloadType();
            if (packet.getPayloadType() == PAYLOAD_TYPE_RAW_CUSTOM) {
                payload = payload_to_string(packet.payload, packet.payload_len);
                ++status_.rx_decoded_count;
                status_.last_decode = "raw custom";
                status_.state = "rx meshcore raw";
                return payload.length() > 0;
            }
            if (packet.getPayloadType() == PAYLOAD_TYPE_GRP_TXT &&
                decode_meshcore_group_text(packet, channels_, payload)) {
                ++status_.rx_decoded_count;
                status_.last_decode = "channel";
                status_.state = "rx meshcore channel";
                return payload.length() > 0;
            }
            if (packet.getPayloadType() == PAYLOAD_TYPE_TXT_MSG &&
                decode_meshcore_contact_text(packet, local_identity_, nodes_, path_hash_mode_, payload)) {
                ++status_.rx_decoded_count;
                status_.last_decode = "contact";
                status_.state = "rx meshcore contact";
                return payload.length() > 0;
            }
            String advert_detail;
            if (packet.getPayloadType() == PAYLOAD_TYPE_ADVERT &&
                decode_meshcore_advert(packet, local_identity_, payload, &advert_detail)) {
                ++status_.rx_decoded_count;
                status_.last_decode = "advert";
                status_.state = "rx meshcore advert";
                return payload.length() > 0;
            }
            if (packet.getPayloadType() == PAYLOAD_TYPE_ADVERT && advert_detail.length() > 0) {
                ++status_.rx_decode_fail_count;
                status_.last_decode = advert_detail;
                status_.state = "rx meshcore advert fail";
                return false;
            }
            if (packet.getPayloadType() == PAYLOAD_TYPE_PATH &&
                decode_meshcore_path(packet, local_identity_, nodes_, payload)) {
                ++status_.rx_decoded_count;
                status_.last_decode = "path";
                status_.state = "rx meshcore path";
                return payload.length() > 0;
            }
            ++status_.rx_decode_fail_count;
            status_.last_decode = String("decode fail type ") + String(packet.getPayloadType());
            status_.state = "rx meshcore type " + String(packet.getPayloadType());
            return false;
        }

        ++status_.rx_decode_fail_count;
        status_.last_decode = "invalid packet";
        status_.state = "rx invalid packet";
        return false;
    }
    status_.state = "rx err " + String(result);
    status_.last_decode = status_.state;
    ++status_.rx_decode_fail_count;
    startReceive();
#else
    (void)payload;
#endif
    return false;
}

RadioCadResult RadioService::scanChannelActivity() {
    RadioCadResult result;
#if APP_ENABLE_RADIOLIB
    if (!hardware_ready_) {
        result.result = -1;
        return result;
    }
    if (tx_in_progress_) {
        pollTransmitComplete();
        result.result = -2;
        return result;
    }
    rx_active_ = false;
    radio.standby();
    delay(1);
    result.result = radio.scanChannel();
    result.irq_flags = radio.getIrqFlags();
    result.rssi = static_cast<int>(radio.getRSSI(false));
    result.detected = result.result == RADIOLIB_PREAMBLE_DETECTED ||
                      (result.irq_flags & sx126x_irq_preamble_detected) != 0 ||
                      (result.irq_flags & sx126x_irq_header_valid) != 0;
    result.channel_free = result.result == RADIOLIB_CHANNEL_FREE;
    result.error = !result.detected && !result.channel_free && result.result != RADIOLIB_ERR_NONE;
    status_.irq_flags = result.irq_flags;
    if (result.rssi > -150 && result.rssi < 20) {
        status_.current_rssi = result.rssi;
    }
    startReceive();
#endif
    return result;
}

RadioListenResult RadioService::listenWindow(uint32_t duration_ms) {
    RadioListenResult result;
    result.duration_ms = duration_ms;
#if APP_ENABLE_RADIOLIB
    if (!hardware_ready_) {
        result.last_decode = "radio not ready";
        return result;
    }
    if (tx_in_progress_) {
        const uint32_t tx_deadline = millis() + 9000U;
        while (tx_in_progress_ && static_cast<int32_t>(millis() - tx_deadline) < 0) {
            pollTransmitComplete();
            if (!tx_in_progress_) {
                break;
            }
            delay(10);
            yield();
        }
        if (tx_in_progress_) {
            result.last_decode = "tx busy";
            return result;
        }
    }

    const unsigned raw_before = status_.rx_raw_count;
    const unsigned decoded_before = status_.rx_decoded_count;
    const unsigned fail_before = status_.rx_decode_fail_count;
    const uint32_t deadline = millis() + duration_ms;
    startReceive();
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        const uint16_t irq = radio.getIrqFlags();
        result.last_irq_flags = irq;
        if ((irq & sx126x_irq_rx_done) != 0) {
            ++result.rx_done_flags;
        }
        if ((irq & sx126x_irq_preamble_detected) != 0) {
            ++result.preamble_flags;
        }
        if ((irq & sx126x_irq_header_valid) != 0) {
            ++result.header_flags;
        }
        if (digitalRead(PIN_LORA_DIO1) == HIGH) {
            ++result.dio1_high_samples;
        }
        if (digitalRead(PIN_LORA_BUSY) == HIGH) {
            ++result.busy_high_samples;
        }
        const int rssi = static_cast<int>(radio.getRSSI(false));
        if (rssi > -150 && rssi < 20) {
            result.min_rssi = std::min(result.min_rssi, rssi);
            result.max_rssi = std::max(result.max_rssi, rssi);
            status_.current_rssi = rssi;
        }
        String payload;
        receivePacket(payload);
        ++result.samples;
        delay(20);
        yield();
    }
    result.raw_delta = status_.rx_raw_count - raw_before;
    result.decoded_delta = status_.rx_decoded_count - decoded_before;
    result.fail_delta = status_.rx_decode_fail_count - fail_before;
    result.last_read_result = status_.read_result;
    result.last_decode = status_.last_decode;
#else
    (void)duration_ms;
    result.last_decode = "radiolib disabled";
#endif
    return result;
}

bool RadioService::setDio2AsRfSwitch(bool enabled) {
#if APP_ENABLE_RADIOLIB
    if (!hardware_ready_) {
        return false;
    }
    rx_active_ = false;
    radio.standby();
    const int result = radio.setDio2AsRfSwitch(enabled);
    if (result == RADIOLIB_ERR_NONE) {
        status_.dio2_as_rf_switch = enabled;
        status_.state = enabled ? "dio2 rf switch on" : "dio2 rf switch off";
        startReceive();
        return true;
    }
    status_.state = "dio2 rf err " + String(result);
    startReceive();
#else
    (void)enabled;
#endif
    return false;
}

bool RadioService::resetReceiver() {
#if APP_ENABLE_RADIOLIB
    if (!hardware_ready_ || tx_in_progress_) {
        return false;
    }

    rx_active_ = false;
    radio_packet_received = false;
    radio.sleep(true);
    radio.standby(RADIOLIB_SX126X_STANDBY_RC, true);

    uint8_t calibrate_all = RADIOLIB_SX126X_CALIBRATE_ALL;
    radio.mod->SPIwriteStream(RADIOLIB_SX126X_CMD_CALIBRATE, &calibrate_all, 1, true, false);
    radio.mod->hal->delay(5);
    const uint32_t deadline = millis() + 50U;
    while (radio.mod->hal->digitalRead(radio.mod->getGpio())) {
        if (static_cast<int32_t>(millis() - deadline) >= 0) {
            break;
        }
        radio.mod->hal->yield();
    }

    radio.calibrateImage(radio_frequency_mhz(frequency_khz_, region_));
    radio.setCRC(1);
    radio.setCurrentLimit(140);
    radio.setDio2AsRfSwitch(BOARD_LORA_DIO2_AS_RF_SWITCH);
    radio.setRxBoostedGainMode(true);
    radio.setOutputPower(tx_power_dbm_);
    status_.current_rssi = -127;
    status_.irq_flags = 0;
    status_.state = "sx1262 rx reset";
    startReceive();
    return rx_active_;
#else
    return false;
#endif
}

void RadioService::startReceive() {
#if APP_ENABLE_RADIOLIB
    if (!hardware_ready_ || tx_in_progress_) {
        rx_active_ = false;
        return;
    }
    radio_packet_received = false;
    const int result = radio.startReceive();
    status_.rx_start_result = result;
    rx_active_ = result == RADIOLIB_ERR_NONE;
    status_.rx_active = rx_active_;
    status_.state = rx_active_ ? "sx1262 rx" : "rx start err " + String(result);
#endif
}

void RadioService::configure(const meshcore::AppState& state) {
    region_ = state.region.c_str();
    frequency_khz_ = state.radio_frequency_khz;
    bandwidth_hz_ = state.radio_bandwidth_hz;
    if (frequency_khz_ < 150000 || frequency_khz_ > 2500000) {
        frequency_khz_ = 910525;
    }
    if (bandwidth_hz_ < 7000 || bandwidth_hz_ > 500000) {
        bandwidth_hz_ = 250000;
    }
    spreading_factor_ = std::max<unsigned>(5, std::min<unsigned>(12, state.radio_spreading_factor));
    coding_rate_ = std::max<unsigned>(5, std::min<unsigned>(8, state.radio_coding_rate));
    tx_power_dbm_ = state.tx_power_dbm;
    path_hash_mode_ = state.path_hash_mode;
    status_.current_rssi = -127;
#if APP_ENABLE_RADIOLIB
    if (hardware_ready_) {
        radio.setFrequency(radio_frequency_mhz(frequency_khz_, region_));
        radio.setBandwidth(static_cast<float>(bandwidth_hz_) / 1000.0f);
        radio.setSpreadingFactor(static_cast<uint8_t>(spreading_factor_));
        radio.setCodingRate(static_cast<uint8_t>(coding_rate_));
        radio.setOutputPower(tx_power_dbm_);
        startReceive();
    }
#endif
    (void)region_;
    (void)tx_power_dbm_;
    (void)path_hash_mode_;
}

void RadioService::setLocalNodeId(const String& node_id) {
    if (node_id.length() > 0) {
        local_node_id_ = node_id;
    }
}

void RadioService::setLocalIdentity(const std::array<uint8_t, 96>& identity) {
    local_identity_ = identity;
}

void RadioService::setChannels(const std::vector<meshcore::ChannelInfo>& channels) {
    channels_ = channels;
}

void RadioService::setNodes(const std::vector<meshcore::NodeInfo>& nodes) {
    nodes_ = nodes;
}
