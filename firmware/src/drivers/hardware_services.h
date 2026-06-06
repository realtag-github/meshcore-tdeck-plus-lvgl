#pragma once

#include <Arduino.h>
#include <array>
#include <vector>

#include "app/app_types.h"

struct BatteryStatus {
    int percent = 0;
    float volts = 0.0f;
    bool charging = false;
    bool valid = false;
};

struct GpsStatus {
    bool enabled = false;
    bool has_fix = false;
    double latitude = 0.0;
    double longitude = 0.0;
    unsigned satellites = 0;
    String state = "off";
};

struct StorageStatus {
    bool mounted = false;
    bool writable = false;
    String state = "not mounted";
};

struct RadioStatus {
    bool ready = false;
    bool tx_busy = false;
    bool rx_active = false;
    int dio1_level = -1;
    int busy_level = -1;
    int begin_result = 0;
    int rx_start_result = 0;
    int read_result = 0;
    unsigned irq_flags = 0;
    bool dio2_as_rf_switch = false;
    int tcxo_mv = 1800;
    int current_rssi = -127;
    int rssi = -127;
    float snr = 0.0f;
    unsigned rx_raw_count = 0;
    unsigned rx_decoded_count = 0;
    unsigned rx_decode_fail_count = 0;
    unsigned tx_count = 0;
    unsigned tx_fail_count = 0;
    unsigned last_packet_len = 0;
    unsigned last_packet_type = 255;
    String last_decode = "none";
    String last_packet_hex;
    String state = "not started";
};

struct RadioTxResult {
    RadioTxResult() = default;
    RadioTxResult(bool accepted_value, const String& status_value)
        : accepted(accepted_value), status(status_value) {}

    bool accepted = false;
    String status = "not sent";
};

struct RadioCadResult {
    int result = 0;
    bool detected = false;
    bool channel_free = false;
    bool error = false;
    int rssi = -127;
    unsigned irq_flags = 0;
};

struct RadioListenResult {
    uint32_t duration_ms = 0;
    uint32_t samples = 0;
    uint32_t rx_done_flags = 0;
    uint32_t preamble_flags = 0;
    uint32_t header_flags = 0;
    uint32_t dio1_high_samples = 0;
    uint32_t busy_high_samples = 0;
    unsigned raw_delta = 0;
    unsigned decoded_delta = 0;
    unsigned fail_delta = 0;
    int min_rssi = 127;
    int max_rssi = -127;
    unsigned last_irq_flags = 0;
    int last_read_result = 0;
    String last_decode = "none";
};

constexpr char INPUT_TRACKBALL_UP = 0x11;
constexpr char INPUT_TRACKBALL_DOWN = 0x12;
constexpr char INPUT_TRACKBALL_LEFT = 0x13;
constexpr char INPUT_TRACKBALL_RIGHT = 0x14;

class BatteryService {
public:
    bool begin();
    BatteryStatus poll();

private:
    BatteryStatus status_;
};

class BoardPowerService {
public:
    bool begin();
};

class GpsService {
public:
    bool begin();
    GpsStatus poll(bool enabled);

private:
    GpsStatus status_;
    String line_;

    void parseLine(const String& line);
    void parseGga(const String& line);
    void parseRmc(const String& line);
};

class StorageService {
public:
    bool begin();
    StorageStatus poll();
    bool appendLog(const String& line);
    bool appendMessageRecord(const String& line);
    std::vector<String> readMessageRecords(std::size_t max_records);
    bool clearMessageRecords();
    bool appendNodeRecord(const String& line);
    std::vector<String> readNodeRecords(std::size_t max_records);
    bool clearNodeRecords();

private:
    StorageStatus status_;
};

class AudioService {
public:
    bool begin();
    void beep(unsigned frequency_hz, unsigned duration_ms, bool enabled);
};

class InputService {
public:
    bool begin();
    void poll();
    bool readKey(char& key);

private:
    static constexpr uint8_t queue_size = 16;
    char queue_[queue_size] = {};
    uint8_t head_ = 0;
    uint8_t tail_ = 0;
    bool last_up_ = true;
    bool last_down_ = true;
    bool last_left_ = true;
    bool last_right_ = true;
    bool keyboard_present_ = false;

    void pushKey(char key);
    void pollTrackball();
};

class RadioService {
public:
    bool begin(const meshcore::AppState& state);
    RadioStatus poll();
    RadioTxResult sendDirect(const String& node_id, const String& text);
    RadioTxResult sendContactMessage(const meshcore::NodeInfo& contact, const String& text, uint32_t timestamp);
    RadioTxResult sendChannelMessage(uint8_t channel_index, const String& sender_name, const String& text, uint32_t timestamp);
    RadioTxResult sendSelfAdvert(const String& name, bool has_position, double latitude, double longitude, uint32_t timestamp);
    RadioTxResult sendPosition(const String& node_id, double latitude, double longitude);
    bool receivePacket(String& payload);
    RadioCadResult scanChannelActivity();
    RadioListenResult listenWindow(uint32_t duration_ms);
    bool setDio2AsRfSwitch(bool enabled);
    bool resetReceiver();
    void configure(const meshcore::AppState& state);
    void setLocalNodeId(const String& node_id);
    void setLocalIdentity(const std::array<uint8_t, 96>& identity);
    void setChannels(const std::vector<meshcore::ChannelInfo>& channels);
    void setNodes(const std::vector<meshcore::NodeInfo>& nodes);

private:
    RadioStatus status_;
    std::vector<meshcore::ChannelInfo> channels_;
    std::vector<meshcore::NodeInfo> nodes_;
    std::array<uint8_t, 96> local_identity_{};
    String region_ = "915 MHz";
    unsigned frequency_khz_ = 910525;
    unsigned bandwidth_hz_ = 250000;
    unsigned spreading_factor_ = 10;
    unsigned coding_rate_ = 5;
    String local_node_id_ = "me";
    int tx_power_dbm_ = 20;
    int path_hash_mode_ = 1;
    bool hardware_ready_ = false;
    bool rx_active_ = false;
    bool tx_in_progress_ = false;
    uint32_t tx_deadline_ms_ = 0;
    String tx_done_state_;
    String tx_timeout_state_;

    RadioTxResult startPacketTransmit(const uint8_t* data,
                                      size_t len,
                                      const char* queued_state,
                                      const char* done_state,
                                      const char* error_prefix);
    void pollTransmitComplete();
    void startReceive();
};
