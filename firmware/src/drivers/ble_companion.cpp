#include "ble_companion.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifndef APP_ENABLE_BLE
#define APP_ENABLE_BLE 0
#endif

#if APP_ENABLE_BLE
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#endif

namespace {

constexpr const char* meshcore_service_uuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* meshcore_rx_uuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* meshcore_tx_uuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr uint32_t companion_ble_pin = 123456;
constexpr uint16_t companion_ble_mtu = 172;
constexpr uint32_t companion_ble_tx_interval_ms = 60;
constexpr uint8_t companion_max_contacts_for_app = 50;
constexpr uint8_t companion_max_group_channels = 40;

constexpr uint8_t cmd_app_start = 0x01;
constexpr uint8_t cmd_send_txt_msg = 0x02;
constexpr uint8_t cmd_send_channel_txt_msg = 0x03;
constexpr uint8_t cmd_get_contacts = 0x04;
constexpr uint8_t cmd_get_device_time = 0x05;
constexpr uint8_t cmd_set_device_time = 0x06;
constexpr uint8_t cmd_send_self_advert = 0x07;
constexpr uint8_t cmd_set_advert_name = 0x08;
constexpr uint8_t cmd_add_update_contact = 0x09;
constexpr uint8_t cmd_sync_next_message = 0x0a;
constexpr uint8_t cmd_set_radio_params = 0x0b;
constexpr uint8_t cmd_set_radio_tx_power = 0x0c;
constexpr uint8_t cmd_reset_path = 0x0d;
constexpr uint8_t cmd_set_advert_latlon = 0x0e;
constexpr uint8_t cmd_remove_contact = 0x0f;
constexpr uint8_t cmd_get_batt_and_storage = 0x14;
constexpr uint8_t cmd_set_tuning_params = 0x15;
constexpr uint8_t cmd_device_query = 0x16;
constexpr uint8_t cmd_export_private_key = 0x17;
constexpr uint8_t cmd_import_private_key = 0x18;
constexpr uint8_t cmd_has_connection = 0x1c;
constexpr uint8_t cmd_logout = 0x1d;
constexpr uint8_t cmd_set_other_params = 0x26;
constexpr uint8_t cmd_set_device_pin = 0x25;
constexpr uint8_t cmd_get_contact_by_key = 0x1e;
constexpr uint8_t cmd_get_channel = 0x1f;
constexpr uint8_t cmd_set_channel = 0x20;
constexpr uint8_t cmd_get_advert_path = 0x2a;
constexpr uint8_t cmd_get_tuning_params = 0x2b;
constexpr uint8_t cmd_get_stats = 0x38;
constexpr uint8_t cmd_set_autoadd_config = 0x3a;
constexpr uint8_t cmd_get_autoadd_config = 0x3b;
constexpr uint8_t cmd_get_allowed_repeat_freq = 0x3c;
constexpr uint8_t cmd_set_path_hash_mode = 0x3d;
constexpr uint8_t cmd_set_default_flood_scope = 0x3f;
constexpr uint8_t cmd_get_default_flood_scope = 0x40;

constexpr uint8_t resp_ok = 0x00;
constexpr uint8_t resp_err = 0x01;
constexpr uint8_t resp_contacts_start = 0x02;
constexpr uint8_t resp_contact = 0x03;
constexpr uint8_t resp_end_of_contacts = 0x04;
constexpr uint8_t resp_self_info = 0x05;
constexpr uint8_t resp_sent = 0x06;
constexpr uint8_t resp_current_time = 0x09;
constexpr uint8_t resp_no_more_messages = 0x0a;
constexpr uint8_t resp_batt_and_storage = 0x0c;
constexpr uint8_t resp_device_info = 0x0d;
constexpr uint8_t resp_private_key = 0x0e;
constexpr uint8_t resp_contact_msg_recv_v3 = 0x10;
constexpr uint8_t resp_channel_msg_recv_v3 = 0x11;
constexpr uint8_t resp_channel_info = 0x12;
constexpr uint8_t resp_tuning_params = 0x17;
constexpr uint8_t resp_stats = 0x18;
constexpr uint8_t resp_autoadd_config = 0x19;
constexpr uint8_t resp_allowed_repeat_freq = 0x1a;
constexpr uint8_t resp_advert_path = 0x16;
constexpr uint8_t resp_default_flood_scope = 0x1c;
constexpr uint8_t push_msg_waiting = 0x83;
constexpr uint8_t txt_type_plain = 0x00;
constexpr uint8_t adv_type_chat = 0x01;

constexpr uint8_t err_not_found = 0x02;

#if APP_ENABLE_BLE
BleCompanionService* active_service = nullptr;
BLECharacteristic* tx_characteristic = nullptr;
BLEServer* ble_server = nullptr;
uint32_t advertising_restart_ms = 0;

class CompanionSecurityCallbacks final : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override {
        return companion_ble_pin;
    }

    void onPassKeyNotify(uint32_t) override {}

    bool onConfirmPIN(uint32_t) override {
        return true;
    }

    bool onSecurityRequest() override {
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        if (active_service != nullptr) {
            active_service->recordAuthentication(cmpl.success);
        }
        Serial.printf("ble event: auth %s\n", cmpl.success ? "ok" : "fail");
        if (!cmpl.success) {
            if (ble_server != nullptr) {
                ble_server->disconnect(ble_server->getConnId());
            }
            advertising_restart_ms = millis() + 1000U;
        }
    }
};

class CompanionServerCallbacks final : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        if (active_service != nullptr) {
            active_service->setConnecting();
        }
        Serial.println("ble event: connect");
    }

    void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
        const uint16_t mtu = server != nullptr ? server->getPeerMTU(param->connect.conn_id) : 0;
        if (active_service != nullptr) {
            active_service->setConnecting(mtu);
        }
        Serial.printf("ble event: connect conn=%u mtu=%u\n",
                      static_cast<unsigned>(param->connect.conn_id),
                      static_cast<unsigned>(mtu));
    }

    void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
        if (active_service != nullptr) {
            active_service->setMtu(param->mtu.mtu);
        }
        Serial.printf("ble event: mtu=%u\n", static_cast<unsigned>(param->mtu.mtu));
    }

    void onDisconnect(BLEServer*) override {
        if (active_service != nullptr) {
            active_service->setConnected(false);
        }
        Serial.println("ble event: disconnect");
        advertising_restart_ms = millis() + 1000U;
    }
};

class CompanionRxCallbacks final : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        handleWrite(characteristic);
    }

    void onWrite(BLECharacteristic* characteristic, esp_ble_gatts_cb_param_t*) override {
        handleWrite(characteristic);
    }

    static void handleWrite(BLECharacteristic* characteristic) {
        if (active_service == nullptr) {
            return;
        }
        const std::string value = characteristic->getValue();
        if (!value.empty()) {
            Serial.printf("ble event: rx type=0x%02x len=%u\n",
                          static_cast<unsigned>(static_cast<uint8_t>(value[0])),
                          static_cast<unsigned>(value.size()));
            active_service->enqueueWrite(reinterpret_cast<const uint8_t*>(value.data()), value.size());
        }
    }
};
#endif

void append_u16(std::vector<uint8_t>& frame, uint16_t value) {
    frame.push_back(static_cast<uint8_t>(value & 0xffU));
    frame.push_back(static_cast<uint8_t>((value >> 8) & 0xffU));
}

void append_u32(std::vector<uint8_t>& frame, uint32_t value) {
    frame.push_back(static_cast<uint8_t>(value & 0xffU));
    frame.push_back(static_cast<uint8_t>((value >> 8) & 0xffU));
    frame.push_back(static_cast<uint8_t>((value >> 16) & 0xffU));
    frame.push_back(static_cast<uint8_t>((value >> 24) & 0xffU));
}

uint32_t read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

int32_t read_i32(const uint8_t* data) {
    return static_cast<int32_t>(read_u32(data));
}

String string_from_bytes(const uint8_t* data, size_t len) {
    String value;
    value.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == 0) {
            break;
        }
        value += static_cast<char>(data[i]);
    }
    return value;
}

void append_fixed_string(std::vector<uint8_t>& frame, const String& value, size_t width) {
    const size_t start = frame.size();
    for (size_t i = 0; i < width; ++i) {
        frame.push_back(0);
    }
    const size_t n = std::min(width, static_cast<size_t>(value.length()));
    std::memcpy(frame.data() + start, value.c_str(), n);
}

void append_contact_frame(std::vector<uint8_t>& frame, const meshcore::NodeInfo& contact) {
    for (const auto byte : contact.public_key) {
        frame.push_back(byte);
    }
    frame.push_back(contact.contact_type);
    frame.push_back(contact.contact_flags);
    frame.push_back(contact.out_path_len);
    for (const auto byte : contact.out_path) {
        frame.push_back(byte);
    }
    append_fixed_string(frame, contact.name.c_str(), 32);
    append_u32(frame, 0);
    append_u32(frame, static_cast<uint32_t>(static_cast<int32_t>(contact.latitude * 1000000.0)));
    append_u32(frame, static_cast<uint32_t>(static_cast<int32_t>(contact.longitude * 1000000.0)));
    append_u32(frame, contact.lastmod);
}

bool parse_contact_frame(const uint8_t* data, size_t len, meshcore::NodeInfo& contact) {
    constexpr size_t min_len = 1 + 32 + 1 + 1 + 1 + 64 + 32 + 4 + 4 + 4 + 4;
    if (len < min_len) {
        return false;
    }

    size_t offset = 1;
    for (auto& byte : contact.public_key) {
        byte = data[offset++];
    }
    contact.contact_type = data[offset++];
    contact.contact_flags = data[offset++];
    contact.out_path_len = data[offset++];
    for (auto& byte : contact.out_path) {
        byte = data[offset++];
    }
    contact.name = string_from_bytes(data + offset, 32).c_str();
    offset += 32;
    offset += 4;
    contact.latitude = static_cast<double>(read_i32(data + offset)) / 1000000.0;
    offset += 4;
    contact.longitude = static_cast<double>(read_i32(data + offset)) / 1000000.0;
    offset += 4;
    contact.lastmod = read_u32(data + offset);
    contact.has_position = contact.latitude != 0.0 && contact.longitude != 0.0;

    char id[12];
    std::snprintf(id, sizeof(id), "0x%02X%02X%02X%02X",
                  contact.public_key[0],
                  contact.public_key[1],
                  contact.public_key[2],
                  contact.public_key[3]);
    contact.short_id = id;
    if (contact.name.empty()) {
        contact.name = contact.short_id;
    }
    contact.rssi = -90;
    contact.snr = 0.0f;
    contact.last_seen_seconds = 0;
    return true;
}

uint8_t clamp_channel_index(uint8_t index, size_t size) {
    if (size == 0) {
        return 0;
    }
    return index < size ? index : static_cast<uint8_t>(size - 1);
}

bool has_public_key(const std::array<unsigned char, 32>& public_key) {
    for (const auto byte : public_key) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

std::size_t path_byte_len(uint8_t encoded_path_len) {
    if (encoded_path_len == meshcore::NodeInfo::out_path_unknown) {
        return 0;
    }
    const uint8_t hash_size = (encoded_path_len >> 6) + 1;
    const uint8_t hash_count = encoded_path_len & 63;
    return static_cast<std::size_t>(hash_size) * hash_count;
}

}  // namespace

bool BleCompanionService::begin(const meshcore::AppSnapshot& snapshot) {
    snapshot_ = snapshot;
#if APP_ENABLE_BLE
    if (status_.started) {
        return true;
    }
    active_service = this;
    String name = "MeshCore " + String(snapshot_.state.device_name.c_str());
    if (name.length() > 24) {
        name = "MeshCore T-Deck";
    }

    BLEDevice::init(name.c_str());
    BLEDevice::setSecurityCallbacks(new CompanionSecurityCallbacks());
    BLEDevice::setMTU(companion_ble_mtu);
    BLESecurity security;
    security.setStaticPIN(companion_ble_pin);
    security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);

    BLEServer* server = BLEDevice::createServer();
    ble_server = server;
    advertising_restart_ms = 0;
    server->setCallbacks(new CompanionServerCallbacks());

    BLEService* service = server->createService(meshcore_service_uuid);
    tx_characteristic = service->createCharacteristic(
        meshcore_tx_uuid,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
    tx_characteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
    tx_characteristic->addDescriptor(new BLE2902());

    BLECharacteristic* rx = service->createCharacteristic(
        meshcore_rx_uuid,
        BLECharacteristic::PROPERTY_WRITE);
    rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
    rx->setCallbacks(new CompanionRxCallbacks());

    service->start();
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(meshcore_service_uuid);
    advertising->setScanResponse(true);
    advertising->start();

    status_.enabled = true;
    status_.started = true;
    status_.connected = false;
    status_.state = "advertising";
    Serial.printf("ble companion: advertising name=%s pin=%06lu mtu=%u\n",
                  name.c_str(),
                  static_cast<unsigned long>(companion_ble_pin),
                  companion_ble_mtu);
    return true;
#else
    status_.enabled = false;
    status_.started = false;
    status_.connected = false;
    status_.state = "disabled";
    return true;
#endif
}

void BleCompanionService::end() {
#if APP_ENABLE_BLE
    if (status_.started) {
        BLEDevice::deinit(true);
    }
    if (active_service == this) {
        active_service = nullptr;
    }
    tx_characteristic = nullptr;
#endif
    pending_.clear();
    tx_queue_.clear();
    next_tx_ms_ = 0;
    status_.tx_queued = 0;
    status_.enabled = false;
    status_.started = false;
    status_.connected = false;
    status_.state = "off";
}

void BleCompanionService::updateSnapshot(const meshcore::AppSnapshot& snapshot) {
    if (!status_.started) {
        return;
    }
    snapshot_ = snapshot;
}

BleCompanionStatus BleCompanionService::status() const {
    return status_;
}

void BleCompanionService::loop() {
#if APP_ENABLE_BLE
    if (!status_.started || ble_server == nullptr) {
        return;
    }
    if (ble_server->getConnectedCount() == 0 && status_.connected) {
        setConnected(false);
        advertising_restart_ms = millis() + 1000U;
    }
    if (advertising_restart_ms != 0 && static_cast<int32_t>(millis() - advertising_restart_ms) >= 0) {
        if (ble_server->getConnectedCount() == 0) {
            ble_server->getAdvertising()->start();
            status_.state = "advertising";
        }
        advertising_restart_ms = 0;
    }
    flushTxQueue();
#endif
}

bool BleCompanionService::readCommand(BleCompanionCommand& command) {
    if (pending_.empty()) {
        return false;
    }
    command = pending_.front();
    pending_.erase(pending_.begin());
    return true;
}

void BleCompanionService::sendOk() {
    notify({resp_ok});
}

void BleCompanionService::sendError(uint8_t code) {
    notify({resp_err, code});
}

void BleCompanionService::sendSelfInfo() {
    std::vector<uint8_t> frame;
    frame.reserve(58 + snapshot_.state.device_name.size());
    frame.push_back(resp_self_info);
    frame.push_back(adv_type_chat);
    frame.push_back(static_cast<uint8_t>(snapshot_.state.tx_power_dbm));
    frame.push_back(22);
    if (has_public_key(snapshot_.state.public_key)) {
        for (const auto byte : snapshot_.state.public_key) {
            frame.push_back(byte);
        }
    } else {
        for (size_t i = 0; i < 32; ++i) {
            frame.push_back(i < snapshot_.state.local_node_id.size()
                                ? static_cast<uint8_t>(snapshot_.state.local_node_id[i])
                                : 0);
        }
    }
    append_u32(frame, static_cast<uint32_t>(static_cast<int32_t>(snapshot_.state.latitude * 1000000.0)));
    append_u32(frame, static_cast<uint32_t>(static_cast<int32_t>(snapshot_.state.longitude * 1000000.0)));
    frame.push_back(static_cast<uint8_t>(snapshot_.state.multi_acks));
    frame.push_back(static_cast<uint8_t>(snapshot_.state.advert_location_policy));
    frame.push_back(0);
    frame.push_back(snapshot_.state.manual_add_contacts ? 1 : 0);
    append_u32(frame, snapshot_.state.radio_frequency_khz);
    append_u32(frame, snapshot_.state.radio_bandwidth_hz);
    frame.push_back(static_cast<uint8_t>(snapshot_.state.radio_spreading_factor));
    frame.push_back(static_cast<uint8_t>(snapshot_.state.radio_coding_rate));
    for (char ch : snapshot_.state.device_name) {
        frame.push_back(static_cast<uint8_t>(ch));
    }
    notify(frame);
}

void BleCompanionService::sendDeviceInfo() {
    std::vector<uint8_t> frame;
    frame.reserve(82);
    frame.push_back(resp_device_info);
    frame.push_back(11);
    frame.push_back(companion_max_contacts_for_app);
    frame.push_back(companion_max_group_channels);
    append_u32(frame, companion_ble_pin);
    append_fixed_string(frame, "27 May 2026", 12);
    append_fixed_string(frame, "LILYGO T-Deck Plus", 40);
    append_fixed_string(frame, snapshot_.state.firmware_version.c_str(), 20);
    frame.push_back(snapshot_.state.client_repeat ? 1 : 0);
    frame.push_back(static_cast<uint8_t>(snapshot_.state.path_hash_mode));
    notify(frame);
}

void BleCompanionService::sendChannelInfo(uint8_t channel_index) {
    if (snapshot_.channels.empty() || channel_index >= snapshot_.channels.size()) {
        sendError(err_not_found);
        return;
    }
    const auto& channel = snapshot_.channels[channel_index];
    std::vector<uint8_t> frame;
    frame.reserve(50);
    frame.push_back(resp_channel_info);
    frame.push_back(channel_index);
    append_fixed_string(frame, channel.name.c_str(), 32);
    for (size_t i = 0; i < 16; ++i) {
        frame.push_back(channel.secret[i]);
    }
    notify(frame);
}

void BleCompanionService::sendContactsStart(std::size_t total_count) {
    const uint32_t count = static_cast<uint32_t>(
        std::min<std::size_t>(total_count, static_cast<std::size_t>(0xffffffffULL)));
    status_.last_contacts_total = static_cast<unsigned>(count);
    std::vector<uint8_t> frame{resp_contacts_start};
    append_u32(frame, count);
    notify(frame);
}

void BleCompanionService::sendContact(const meshcore::NodeInfo& contact) {
    std::vector<uint8_t> frame;
    frame.reserve(1 + 143);
    frame.push_back(resp_contact);
    append_contact_frame(frame, contact);
    notify(frame);
}

void BleCompanionService::sendEndOfContacts() {
    notify({resp_end_of_contacts});
}

void BleCompanionService::sendBattery() {
    std::vector<uint8_t> frame{resp_batt_and_storage};
    append_u16(frame, static_cast<uint16_t>(std::max(0, snapshot_.state.battery_mv)));
    append_u32(frame, snapshot_.state.persisted_message_count + snapshot_.state.persisted_node_count);
    append_u32(frame, 1024);
    notify(frame);
}

void BleCompanionService::sendSent(bool flood, uint32_t expected_ack, uint32_t timeout_ms) {
    std::vector<uint8_t> frame{resp_sent, static_cast<uint8_t>(flood ? 1 : 0)};
    append_u32(frame, expected_ack);
    append_u32(frame, timeout_ms);
    notify(frame);
}

void BleCompanionService::sendCurrentTime(uint32_t epoch_seconds) {
    std::vector<uint8_t> frame{resp_current_time};
    append_u32(frame, epoch_seconds);
    notify(frame);
}

void BleCompanionService::sendTuningParams(uint32_t rx_delay_base_ms, uint32_t airtime_factor_ms) {
    std::vector<uint8_t> frame{resp_tuning_params};
    append_u32(frame, rx_delay_base_ms);
    append_u32(frame, airtime_factor_ms);
    notify(frame);
}

void BleCompanionService::sendPrivateKey(const std::array<uint8_t, 64>& private_key) {
    std::vector<uint8_t> frame{resp_private_key};
    for (const auto byte : private_key) {
        frame.push_back(byte);
    }
    notify(frame);
}

void BleCompanionService::sendStats(uint8_t stats_type,
                                    uint16_t battery_mv,
                                    uint32_t uptime_seconds,
                                    uint16_t error_flags,
                                    uint8_t queue_len,
                                    int16_t noise_floor,
                                    int8_t last_rssi,
                                    int8_t last_snr_quarters) {
    std::vector<uint8_t> frame{resp_stats, stats_type};
    if (stats_type == 0) {
        append_u16(frame, battery_mv);
        append_u32(frame, uptime_seconds);
        append_u16(frame, error_flags);
        frame.push_back(queue_len);
    } else if (stats_type == 1) {
        append_u16(frame, static_cast<uint16_t>(noise_floor));
        frame.push_back(static_cast<uint8_t>(last_rssi));
        frame.push_back(static_cast<uint8_t>(last_snr_quarters));
        append_u32(frame, 0);
        append_u32(frame, 0);
    } else if (stats_type == 2) {
        for (int i = 0; i < 7; ++i) {
            append_u32(frame, 0);
        }
    }
    notify(frame);
}

void BleCompanionService::sendAutoAddConfig(uint8_t config, uint8_t max_hops) {
    notify({resp_autoadd_config, config, max_hops});
}

void BleCompanionService::sendAllowedRepeatFreq() {
    std::vector<uint8_t> frame{resp_allowed_repeat_freq};
    constexpr uint32_t ranges[][2] = {
        {433000, 433000},
        {869000, 869000},
        {918000, 918000},
    };
    for (const auto& range : ranges) {
        append_u32(frame, range[0]);
        append_u32(frame, range[1]);
    }
    notify(frame);
}

void BleCompanionService::sendAdvertPath(const meshcore::NodeInfo& contact, uint32_t recv_timestamp) {
    if (contact.out_path_len == meshcore::NodeInfo::out_path_unknown) {
        sendError(err_not_found);
        return;
    }
    const std::size_t path_len = std::min(path_byte_len(contact.out_path_len), contact.out_path.size());
    std::vector<uint8_t> frame{resp_advert_path};
    append_u32(frame, recv_timestamp);
    frame.push_back(contact.out_path_len);
    for (std::size_t i = 0; i < path_len; ++i) {
        frame.push_back(contact.out_path[i]);
    }
    notify(frame);
}

void BleCompanionService::sendDefaultFloodScope(const std::string& name,
                                                const std::array<unsigned char, 16>& secret) {
    std::vector<uint8_t> frame{resp_default_flood_scope};
    if (!name.empty()) {
        append_fixed_string(frame, name.c_str(), 31);
        for (const auto byte : secret) {
            frame.push_back(byte);
        }
    }
    notify(frame);
}

void BleCompanionService::sendNoMoreMessages() {
    notify({resp_no_more_messages});
}

void BleCompanionService::sendContactMessage(const meshcore::NodeInfo& contact, const String& text, uint32_t timestamp) {
    std::vector<uint8_t> frame;
    frame.reserve(18 + text.length());
    frame.push_back(resp_contact_msg_recv_v3);
    frame.push_back(0);
    frame.push_back(0);
    frame.push_back(0);
    for (size_t i = 0; i < 6; ++i) {
        frame.push_back(contact.public_key[i]);
    }
    frame.push_back(contact.out_path_len);
    frame.push_back(txt_type_plain);
    append_u32(frame, timestamp);
    for (size_t i = 0; i < text.length(); ++i) {
        frame.push_back(static_cast<uint8_t>(text[i]));
    }
    notify(frame);
}

void BleCompanionService::sendChannelMessage(uint8_t channel_index, const String& text, uint32_t timestamp) {
    std::vector<uint8_t> frame;
    frame.reserve(11 + text.length());
    frame.push_back(resp_channel_msg_recv_v3);
    frame.push_back(0);
    frame.push_back(0);
    frame.push_back(0);
    frame.push_back(clamp_channel_index(channel_index, snapshot_.channels.size()));
    frame.push_back(0xff);
    frame.push_back(txt_type_plain);
    append_u32(frame, timestamp);
    for (size_t i = 0; i < text.length(); ++i) {
        frame.push_back(static_cast<uint8_t>(text[i]));
    }
    notify(frame);
}

void BleCompanionService::notifyMessagesWaiting() {
    notify({push_msg_waiting});
}

void BleCompanionService::enqueueWrite(const uint8_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return;
    }
    status_.last_rx_type = data[0];
    status_.last_rx_len = static_cast<unsigned>(len);
    pending_.push_back(parseFrame(data, len));
    ++status_.rx_frames;
}

void BleCompanionService::setConnecting(uint16_t mtu) {
    tx_queue_.clear();
    next_tx_ms_ = 0;
    status_.tx_queued = 0;
    ++status_.connect_events;
    if (mtu != 0) {
        status_.mtu = mtu;
    }
    status_.connected = false;
    status_.state = "connecting";
}

void BleCompanionService::setMtu(uint16_t mtu) {
    status_.mtu = mtu;
}

void BleCompanionService::recordAuthentication(bool success) {
    if (success) {
        ++status_.auth_success;
        setConnected(true);
    } else {
        ++status_.auth_fail;
        setConnected(false);
        status_.state = "auth failed";
    }
}

void BleCompanionService::setConnected(bool connected) {
    if (!connected) {
        tx_queue_.clear();
        next_tx_ms_ = 0;
        status_.tx_queued = 0;
        ++status_.disconnect_events;
    }
    status_.connected = connected;
    status_.state = connected ? "connected" : "advertising";
}

void BleCompanionService::notify(const std::vector<uint8_t>& frame) {
#if APP_ENABLE_BLE
    if (tx_characteristic == nullptr || frame.empty()) {
        return;
    }
    status_.last_tx_type = frame[0];
    status_.last_tx_len = static_cast<unsigned>(frame.size());
    tx_queue_.push_back(frame);
    status_.tx_queued = tx_queue_.size();
#else
    (void)frame;
#endif
}

void BleCompanionService::flushTxQueue() {
#if APP_ENABLE_BLE
    if (tx_characteristic == nullptr || tx_queue_.empty()) {
        return;
    }
    if (status_.started && !status_.connected && ble_server != nullptr && ble_server->getConnectedCount() == 0) {
        tx_queue_.clear();
        status_.tx_queued = 0;
        return;
    }
    const uint32_t now = millis();
    if (next_tx_ms_ != 0 && static_cast<int32_t>(now - next_tx_ms_) < 0) {
        return;
    }

    const auto frame = tx_queue_.front();
    tx_queue_.erase(tx_queue_.begin());
    tx_characteristic->setValue(const_cast<uint8_t*>(frame.data()), frame.size());
    tx_characteristic->notify();
    status_.last_tx_type = frame[0];
    status_.last_tx_len = static_cast<unsigned>(frame.size());
    ++status_.tx_frames;
    status_.tx_queued = tx_queue_.size();
    next_tx_ms_ = now + companion_ble_tx_interval_ms;
#endif
}

BleCompanionCommand BleCompanionService::parseFrame(const uint8_t* data, size_t len) const {
    BleCompanionCommand command;
    command.raw_type = data[0];
    switch (data[0]) {
        case cmd_app_start:
            command.type = BleCompanionCommand::Type::AppStart;
            break;
        case cmd_send_txt_msg:
            if (len >= 14) {
                command.type = BleCompanionCommand::Type::SendContactMessage;
                command.text_type = data[1];
                command.attempt = data[2];
                command.timestamp = read_u32(data + 3);
                for (size_t i = 0; i < 6 && i < command.contact.public_key.size(); ++i) {
                    command.contact.public_key[i] = data[7 + i];
                }
                command.text = string_from_bytes(data + 13, len - 13);
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_send_channel_txt_msg:
            if (len >= 7 && data[1] == 0x00) {
                command.type = BleCompanionCommand::Type::SendChannelMessage;
                command.channel_index = data[2];
                command.timestamp = read_u32(data + 3);
                command.text = string_from_bytes(data + 7, len - 7);
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_get_contacts:
            command.type = BleCompanionCommand::Type::GetContacts;
            break;
        case cmd_get_device_time:
            command.type = BleCompanionCommand::Type::GetDeviceTime;
            break;
        case cmd_set_device_time:
            if (len >= 5) {
                command.type = BleCompanionCommand::Type::SetDeviceTime;
                command.timestamp = read_u32(data + 1);
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_send_self_advert:
            command.type = BleCompanionCommand::Type::SendSelfAdvert;
            command.flood = len >= 2 && data[1] == 1;
            break;
        case cmd_set_advert_name:
            if (len >= 2) {
                command.type = BleCompanionCommand::Type::SetAdvertName;
                command.text = string_from_bytes(data + 1, len - 1);
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_add_update_contact:
            command.type = parse_contact_frame(data, len, command.contact)
                               ? BleCompanionCommand::Type::AddUpdateContact
                               : BleCompanionCommand::Type::Unknown;
            break;
        case cmd_sync_next_message:
            command.type = BleCompanionCommand::Type::GetMessage;
            break;
        case cmd_set_radio_params:
            if (len >= 11) {
                command.type = BleCompanionCommand::Type::SetRadioParams;
                command.frequency_khz = read_u32(data + 1);
                command.bandwidth_hz = read_u32(data + 5);
                command.spreading_factor = data[9];
                command.coding_rate = data[10];
                command.client_repeat = len >= 12 && data[11] != 0;
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_set_radio_tx_power:
            if (len >= 2) {
                command.type = BleCompanionCommand::Type::SetRadioTxPower;
                command.tx_power_dbm = static_cast<int8_t>(data[1]);
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_reset_path:
            if (len >= 33) {
                command.type = BleCompanionCommand::Type::ResetPath;
                for (size_t i = 0; i < command.contact.public_key.size(); ++i) {
                    command.contact.public_key[i] = data[1 + i];
                }
            }
            break;
        case cmd_set_advert_latlon:
            if (len >= 9) {
                command.type = BleCompanionCommand::Type::SetAdvertLatLon;
                command.latitude_i = read_i32(data + 1);
                command.longitude_i = read_i32(data + 5);
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_remove_contact:
            if (len >= 33) {
                command.type = BleCompanionCommand::Type::RemoveContact;
                for (size_t i = 0; i < command.contact.public_key.size(); ++i) {
                    command.contact.public_key[i] = data[1 + i];
                }
            }
            break;
        case cmd_get_batt_and_storage:
            command.type = BleCompanionCommand::Type::GetBattery;
            break;
        case cmd_set_tuning_params:
            if (len >= 9) {
                command.type = BleCompanionCommand::Type::SetTuningParams;
                command.rx_delay_base_ms = read_u32(data + 1);
                command.airtime_factor_ms = read_u32(data + 5);
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_device_query:
            if (len >= 2 && data[1] == 0x03) {
                command.type = BleCompanionCommand::Type::DeviceQuery;
            } else {
                command.type = BleCompanionCommand::Type::DeviceQuery;
            }
            break;
        case cmd_export_private_key:
            command.type = BleCompanionCommand::Type::ExportPrivateKey;
            break;
        case cmd_import_private_key:
            if (len >= 65) {
                command.type = BleCompanionCommand::Type::ImportPrivateKey;
                for (size_t i = 0; i < command.private_key.size(); ++i) {
                    command.private_key[i] = data[1 + i];
                }
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_has_connection:
            if (len >= 33) {
                command.type = BleCompanionCommand::Type::HasConnection;
                for (size_t i = 0; i < command.contact.public_key.size(); ++i) {
                    command.contact.public_key[i] = data[1 + i];
                }
            }
            break;
        case cmd_logout:
            if (len >= 33) {
                command.type = BleCompanionCommand::Type::Logout;
                for (size_t i = 0; i < command.contact.public_key.size(); ++i) {
                    command.contact.public_key[i] = data[1 + i];
                }
            }
            break;
        case cmd_set_device_pin:
            if (len >= 5) {
                command.type = BleCompanionCommand::Type::SetDevicePin;
                command.device_pin = read_u32(data + 1);
            }
            break;
        case cmd_set_other_params:
            if (len >= 2) {
                command.type = BleCompanionCommand::Type::SetOtherParams;
                command.manual_add_contacts = data[1] != 0;
                command.telemetry_mode = len >= 3 ? data[2] : 0;
                command.advert_location_policy = len >= 4 ? data[3] : 0;
                command.multi_acks = len >= 5 ? data[4] : 0;
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_get_contact_by_key:
            if (len >= 33) {
                command.type = BleCompanionCommand::Type::GetContactByKey;
                for (size_t i = 0; i < command.contact.public_key.size(); ++i) {
                    command.contact.public_key[i] = data[1 + i];
                }
            }
            break;
        case cmd_get_tuning_params:
            command.type = BleCompanionCommand::Type::GetTuningParams;
            break;
        case cmd_get_stats:
            if (len >= 2) {
                command.type = BleCompanionCommand::Type::GetStats;
                command.stats_type = data[1];
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_set_autoadd_config:
            command.type = BleCompanionCommand::Type::SetAutoAddConfig;
            command.autoadd_config = len >= 2 ? data[1] : 0;
            command.autoadd_max_hops = len >= 3 ? data[2] : 64;
            break;
        case cmd_get_autoadd_config:
            command.type = BleCompanionCommand::Type::GetAutoAddConfig;
            break;
        case cmd_get_allowed_repeat_freq:
            command.type = BleCompanionCommand::Type::GetAllowedRepeatFreq;
            break;
        case cmd_set_path_hash_mode:
            if (len >= 3 && data[1] == 0) {
                command.type = BleCompanionCommand::Type::SetPathHashMode;
                command.path_hash_mode = data[2];
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        case cmd_get_advert_path:
            if (len >= 34) {
                command.type = BleCompanionCommand::Type::GetAdvertPath;
                for (size_t i = 0; i < command.contact.public_key.size(); ++i) {
                    command.contact.public_key[i] = data[2 + i];
                }
            }
            break;
        case cmd_set_default_flood_scope:
            command.type = BleCompanionCommand::Type::SetDefaultFloodScope;
            if (len >= 48) {
                command.text = string_from_bytes(data + 1, 31);
                for (size_t i = 0; i < command.secret.size(); ++i) {
                    command.secret[i] = data[32 + i];
                }
            }
            break;
        case cmd_get_default_flood_scope:
            command.type = BleCompanionCommand::Type::GetDefaultFloodScope;
            break;
        case cmd_get_channel:
            if (len >= 2) {
                command.type = BleCompanionCommand::Type::GetChannel;
                command.channel_index = data[1];
            }
            break;
        case cmd_set_channel:
            if (len >= 50) {
                command.type = BleCompanionCommand::Type::SetChannel;
                command.channel_index = data[1];
                command.channel_name = string_from_bytes(data + 2, 32);
                for (size_t i = 0; i < command.secret.size(); ++i) {
                    command.secret[i] = data[34 + i];
                }
            } else {
                command.type = BleCompanionCommand::Type::Unknown;
            }
            break;
        default:
            command.type = BleCompanionCommand::Type::Unknown;
            break;
    }
    return command;
}
