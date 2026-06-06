#pragma once

#include <Arduino.h>
#include <array>
#include <cstddef>

#include "app/app_types.h"

namespace meshcore_firmware {

struct CoreRadioParams {
    uint32_t frequency_hz = 910525000;
    uint32_t bandwidth_hz = 250000;
    uint8_t spreading_factor = 11;
    uint8_t coding_rate = 5;
    int8_t tx_power_dbm = 20;
    uint8_t path_hash_mode = 0;
};

struct CoreIdentity {
    std::array<uint8_t, 32> public_key{};
    String display_name = "T-Deck Plus";
};

class MeshCoreCoreFacade {
public:
    bool begin(const meshcore::AppSnapshot& snapshot);
    void ingestSnapshot(const meshcore::AppSnapshot& snapshot);

    const CoreIdentity& identity() const;
    const CoreRadioParams& radioParams() const;
    const std::array<uint8_t, 96>& localIdentity() const;
    bool exportPrivateKey(std::array<uint8_t, 64>& private_key) const;
    bool importPrivateKey(const std::array<uint8_t, 64>& private_key);

    bool upstreamAvailable() const;
    const char* upstreamVersion() const;

private:
    CoreIdentity identity_;
    CoreRadioParams radio_params_;
    std::array<uint8_t, 96> local_identity_{};
    bool upstream_available_ = false;

    void refreshIdentity(const meshcore::AppSnapshot& snapshot);
    void refreshRadioParams(const meshcore::AppSnapshot& snapshot);
};

}  // namespace meshcore_firmware
