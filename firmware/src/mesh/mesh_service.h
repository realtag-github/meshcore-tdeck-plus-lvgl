#pragma once

#include <Arduino.h>
#include <vector>
#include "app/app_controller.h"
#include "app/app_types.h"
#include "drivers/ble_companion.h"
#include "drivers/hardware_services.h"
#include "meshcore_core_facade.h"

using NodeInfo = meshcore::NodeInfo;
using MeshMessage = meshcore::MeshMessage;

class MeshService {
public:
    bool begin();
    void loop();

    const meshcore::AppSnapshot& snapshot() const;
    bool handleUiCommand(meshcore::ActionCommand command,
                         const meshcore::AppSnapshot& before,
                         meshcore::AppSnapshot& after);
    bool sendDirectMessage(const String& node_id, const String& text);
    std::vector<NodeInfo> getKnownNodes() const;
    std::vector<MeshMessage> getRecentMessages() const;

private:
    meshcore::AppSnapshot snapshot_;
    BoardPowerService board_power_;
    BatteryService battery_;
    GpsService gps_;
    StorageService storage_;
    AudioService audio_;
    InputService input_;
    RadioService radio_;
    BleCompanionService ble_;
    meshcore_firmware::MeshCoreCoreFacade core_;
    uint32_t last_status_ms_ = 0;
    uint32_t last_input_poll_ms_ = 0;
    uint32_t last_hardware_poll_ms_ = 0;
    uint32_t last_radio_poll_ms_ = 0;
    uint32_t last_advert_ms_ = 0;
    uint32_t radio_scan_next_ms_ = 0;
    uint32_t radio_scan_base_rx_raw_ = 0;
    uint32_t radio_scan_base_rx_ok_ = 0;
    uint32_t radio_scan_total_raw_ = 0;
    uint32_t radio_scan_total_ok_ = 0;
    uint32_t radio_scan_best_raw_ = 0;
    uint32_t radio_scan_best_ok_ = 0;
    uint32_t radio_scan_status_next_ms_ = 0;
    uint32_t radio_scan_original_frequency_khz_ = 910525;
    uint32_t radio_scan_original_bandwidth_hz_ = 62500;
    uint8_t radio_scan_original_spreading_factor_ = 7;
    uint8_t radio_scan_original_coding_rate_ = 5;
    uint32_t clock_epoch_base_ = 0;
    uint32_t clock_millis_base_ = 0;
    uint8_t radio_scan_index_ = 0;
    bool radio_scan_active_ = false;
    int radio_scan_current_peak_rssi_ = -127;
    int radio_scan_best_rssi_ = -127;
    String radio_scan_best_name_;
    String radio_scan_best_rssi_name_;
    String radio_scan_original_region_;
    uint8_t startup_stage_ = 0;
    bool storage_started_ = false;
    bool radio_started_ = false;
    bool core_started_ = false;
    unsigned next_incoming_id_ = 500;
    String serial_buffer_;
    std::vector<std::string> ble_synced_message_ids_;

    void loadSettings();
    void saveSettings() const;
    void appendLog(const String& value);
    void loadMessageHistory();
    void loadNodeHistory();
    void addMessage(const MeshMessage& message, bool persist);
    bool persistMessage(const MeshMessage& message);
    bool persistDelete(const String& message_id);
    void handleRadioPayload(const String& payload);
    void upsertNode(const String& short_id, const String& name, int rssi, float snr, bool persist);
    void upsertContact(const NodeInfo& contact, int rssi, float snr, bool persist);
    void updateNodePosition(const String& short_id, double latitude, double longitude, bool persist);
    bool persistNode(const NodeInfo& node);
    void pollSerialConsole();
    void handleSerialCommand(const String& command);
    void handleInputKey(char key);
    void processBleCommands();
    void handleBleCommand(const BleCompanionCommand& command);
    void syncNextBleMessage();
    void syncCoreState();
    void startRadioScan();
    void stopRadioScan(bool restore_original, const char* reason);
    void applyRadioScanPreset();
    void updateRadioScan(uint32_t now_ms);
    void refreshRadioScanStatus(uint32_t raw_delta, uint32_t ok_delta);
    void printRadioScanStatus();
    bool startupComplete() const;
    void continueStartup();
    uint32_t currentEpochSeconds() const;
    void pollRadio(uint32_t now_ms);
    void pollHardware(uint32_t now_ms);
};
