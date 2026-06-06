#pragma once

#include <Arduino.h>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "app/app_types.h"

struct BleCompanionStatus {
    bool enabled = false;
    bool started = false;
    bool connected = false;
    String state = "off";
    unsigned rx_frames = 0;
    unsigned tx_frames = 0;
    unsigned tx_queued = 0;
    unsigned connect_events = 0;
    unsigned disconnect_events = 0;
    unsigned auth_success = 0;
    unsigned auth_fail = 0;
    uint16_t mtu = 0;
    uint8_t last_rx_type = 0;
    uint8_t last_tx_type = 0;
    unsigned last_rx_len = 0;
    unsigned last_tx_len = 0;
    unsigned last_contacts_total = 0;
};

struct BleCompanionCommand {
    enum class Type {
        AppStart,
        DeviceQuery,
        GetDeviceTime,
        SetDeviceTime,
        SetAdvertName,
        SetAdvertLatLon,
        SendSelfAdvert,
        SetRadioParams,
        SetRadioTxPower,
        SetTuningParams,
        GetTuningParams,
        SetOtherParams,
        ExportPrivateKey,
        ImportPrivateKey,
        GetStats,
        SetAutoAddConfig,
        GetAutoAddConfig,
        GetAllowedRepeatFreq,
        SetPathHashMode,
        HasConnection,
        Logout,
        SetDevicePin,
        GetAdvertPath,
        SetDefaultFloodScope,
        GetDefaultFloodScope,
        GetChannel,
        SetChannel,
        SendContactMessage,
        SendChannelMessage,
        GetContacts,
        AddUpdateContact,
        RemoveContact,
        ResetPath,
        GetContactByKey,
        GetMessage,
        GetBattery,
        Unknown,
    };

    Type type = Type::Unknown;
    uint8_t channel_index = 0;
    uint8_t text_type = 0;
    uint8_t attempt = 0;
    int tx_power_dbm = 0;
    int path_hash_mode = 0;
    uint32_t frequency_khz = 0;
    uint32_t bandwidth_hz = 0;
    uint32_t rx_delay_base_ms = 0;
    uint32_t airtime_factor_ms = 0;
    int32_t latitude_i = 0;
    int32_t longitude_i = 0;
    uint8_t spreading_factor = 0;
    uint8_t coding_rate = 0;
    uint8_t advert_location_policy = 0;
    uint8_t telemetry_mode = 0;
    uint8_t multi_acks = 0;
    uint8_t stats_type = 0;
    uint8_t autoadd_config = 0;
    uint8_t autoadd_max_hops = 0;
    uint32_t device_pin = 0;
    bool client_repeat = false;
    bool manual_add_contacts = false;
    bool flood = false;
    uint32_t timestamp = 0;
    String text;
    String channel_name;
    meshcore::NodeInfo contact;
    std::array<uint8_t, 16> secret{};
    std::array<uint8_t, 64> private_key{};
    uint8_t raw_type = 0;
};

class BleCompanionService {
public:
    bool begin(const meshcore::AppSnapshot& snapshot);
    void end();
    void updateSnapshot(const meshcore::AppSnapshot& snapshot);
    void loop();
    BleCompanionStatus status() const;
    bool readCommand(BleCompanionCommand& command);

    void sendOk();
    void sendError(uint8_t code = 0);
    void sendSelfInfo();
    void sendDeviceInfo();
    void sendChannelInfo(uint8_t channel_index);
    void sendContactsStart(std::size_t total_count);
    void sendContact(const meshcore::NodeInfo& contact);
    void sendEndOfContacts();
    void sendBattery();
    void sendSent(bool flood, uint32_t expected_ack, uint32_t timeout_ms);
    void sendCurrentTime(uint32_t epoch_seconds);
    void sendTuningParams(uint32_t rx_delay_base_ms, uint32_t airtime_factor_ms);
    void sendPrivateKey(const std::array<uint8_t, 64>& private_key);
    void sendStats(uint8_t stats_type,
                   uint16_t battery_mv,
                   uint32_t uptime_seconds,
                   uint16_t error_flags,
                   uint8_t queue_len,
                   int16_t noise_floor,
                   int8_t last_rssi,
                   int8_t last_snr_quarters);
    void sendAutoAddConfig(uint8_t config, uint8_t max_hops);
    void sendAllowedRepeatFreq();
    void sendAdvertPath(const meshcore::NodeInfo& contact, uint32_t recv_timestamp);
    void sendDefaultFloodScope(const std::string& name, const std::array<unsigned char, 16>& secret);
    void sendNoMoreMessages();
    void sendContactMessage(const meshcore::NodeInfo& contact, const String& text, uint32_t timestamp);
    void sendChannelMessage(uint8_t channel_index, const String& text, uint32_t timestamp);
    void notifyMessagesWaiting();

    void enqueueWrite(const uint8_t* data, size_t len);
    void setConnecting(uint16_t mtu = 0);
    void setMtu(uint16_t mtu);
    void recordAuthentication(bool success);
    void setConnected(bool connected);

private:
    meshcore::AppSnapshot snapshot_;
    BleCompanionStatus status_;
    std::vector<BleCompanionCommand> pending_;
    std::vector<std::vector<uint8_t>> tx_queue_;
    uint32_t next_tx_ms_ = 0;

    void notify(const std::vector<uint8_t>& frame);
    void flushTxQueue();
    BleCompanionCommand parseFrame(const uint8_t* data, size_t len) const;
};
