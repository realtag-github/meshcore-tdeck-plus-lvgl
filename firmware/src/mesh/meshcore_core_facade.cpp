#include "meshcore_core_facade.h"

#include <algorithm>

#if __has_include(<MeshCore.h>)
#include <Preferences.h>
#include <esp_system.h>
#include <MeshCore.h>
#include <Identity.h>
#define ED25519_NO_SEED 1
#include <ed_25519.h>
#define APP_HAS_UPSTREAM_MESHCORE 1
#else
#define APP_HAS_UPSTREAM_MESHCORE 0
#endif

namespace meshcore_firmware {
namespace {

void derive_public_key(const meshcore::AppState& state, std::array<uint8_t, 32>& dest) {
    const String seed = String(state.local_node_id.c_str()) + ":" + String(state.device_name.c_str());
    for (std::size_t i = 0; i < dest.size(); ++i) {
        dest[i] = static_cast<uint8_t>(seed.length() == 0 ? i * 17U : seed[i % seed.length()] + (i * 17U));
    }
}

#if APP_HAS_UPSTREAM_MESHCORE
constexpr const char* kPrefsNamespace = "meshcore";
constexpr const char* kIdentityKey = "identity";
constexpr std::size_t kStoredIdentitySize = PRV_KEY_SIZE + PUB_KEY_SIZE;

bool has_valid_public_key(const uint8_t* public_key) {
    bool any_non_zero = false;
    bool any_not_ff = false;
    for (std::size_t i = 0; i < PUB_KEY_SIZE; ++i) {
        any_non_zero = any_non_zero || public_key[i] != 0;
        any_not_ff = any_not_ff || public_key[i] != 0xFF;
    }
    return any_non_zero && any_not_ff && public_key[0] != 0x00 && public_key[0] != 0xFF;
}

void create_identity(std::array<uint8_t, kStoredIdentitySize>& stored_identity) {
    uint8_t seed[SEED_SIZE]{};
    esp_fill_random(seed, sizeof(seed));
    ed25519_create_keypair(&stored_identity[PRV_KEY_SIZE], stored_identity.data(), seed);
}

bool load_or_create_identity(std::array<uint8_t, kStoredIdentitySize>& stored_identity) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }

    bool needs_create = prefs.getBytesLength(kIdentityKey) != stored_identity.size();
    if (!needs_create) {
        prefs.getBytes(kIdentityKey, stored_identity.data(), stored_identity.size());
        needs_create = !has_valid_public_key(&stored_identity[PRV_KEY_SIZE]);
    }

    if (needs_create) {
        create_identity(stored_identity);
        prefs.putBytes(kIdentityKey, stored_identity.data(), stored_identity.size());
    }

    prefs.end();
    return true;
}
#endif

}  // namespace

bool MeshCoreCoreFacade::begin(const meshcore::AppSnapshot& snapshot) {
    upstream_available_ = APP_HAS_UPSTREAM_MESHCORE != 0;
#if APP_HAS_UPSTREAM_MESHCORE
    upstream_available_ = load_or_create_identity(local_identity_);
#endif
    ingestSnapshot(snapshot);
    return true;
}

void MeshCoreCoreFacade::ingestSnapshot(const meshcore::AppSnapshot& snapshot) {
    refreshIdentity(snapshot);
    refreshRadioParams(snapshot);
}

const CoreIdentity& MeshCoreCoreFacade::identity() const {
    return identity_;
}

const CoreRadioParams& MeshCoreCoreFacade::radioParams() const {
    return radio_params_;
}

const std::array<uint8_t, 96>& MeshCoreCoreFacade::localIdentity() const {
    return local_identity_;
}

bool MeshCoreCoreFacade::exportPrivateKey(std::array<uint8_t, 64>& private_key) const {
#if APP_HAS_UPSTREAM_MESHCORE
    if (!upstream_available_) {
        return false;
    }
    mesh::LocalIdentity local;
    local.readFrom(local_identity_.data(), local_identity_.size());
    return local.writeTo(private_key.data(), private_key.size()) == private_key.size();
#else
    (void)private_key;
    return false;
#endif
}

bool MeshCoreCoreFacade::importPrivateKey(const std::array<uint8_t, 64>& private_key) {
#if APP_HAS_UPSTREAM_MESHCORE
    if (!mesh::LocalIdentity::validatePrivateKey(private_key.data())) {
        return false;
    }
    mesh::LocalIdentity local;
    local.readFrom(private_key.data(), private_key.size());
    local.writeTo(local_identity_.data(), local_identity_.size());

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }
    prefs.putBytes(kIdentityKey, local_identity_.data(), local_identity_.size());
    prefs.end();
    upstream_available_ = true;
    return true;
#else
    (void)private_key;
    return false;
#endif
}

bool MeshCoreCoreFacade::upstreamAvailable() const {
    return upstream_available_;
}

const char* MeshCoreCoreFacade::upstreamVersion() const {
#if APP_HAS_UPSTREAM_MESHCORE
    return "submodule";
#else
    return "unavailable";
#endif
}

void MeshCoreCoreFacade::refreshIdentity(const meshcore::AppSnapshot& snapshot) {
    identity_.display_name = snapshot.state.device_name.c_str();
#if APP_HAS_UPSTREAM_MESHCORE
    if (upstream_available_) {
        mesh::LocalIdentity local;
        local.readFrom(local_identity_.data(), local_identity_.size());
        std::copy(local.pub_key, local.pub_key + identity_.public_key.size(), identity_.public_key.begin());
        return;
    }
#endif
    derive_public_key(snapshot.state, identity_.public_key);
}

void MeshCoreCoreFacade::refreshRadioParams(const meshcore::AppSnapshot& snapshot) {
    radio_params_.frequency_hz = snapshot.state.radio_frequency_khz * 1000UL;
    radio_params_.bandwidth_hz = snapshot.state.radio_bandwidth_hz;
    radio_params_.spreading_factor = static_cast<uint8_t>(
        std::max<unsigned>(5, std::min<unsigned>(12, snapshot.state.radio_spreading_factor)));
    radio_params_.coding_rate = static_cast<uint8_t>(
        std::max<unsigned>(5, std::min<unsigned>(8, snapshot.state.radio_coding_rate)));
    radio_params_.tx_power_dbm = static_cast<int8_t>(
        std::max(-9, std::min(22, snapshot.state.tx_power_dbm)));
    radio_params_.path_hash_mode = static_cast<uint8_t>(
        std::max(0, std::min(2, snapshot.state.path_hash_mode)));
}

}  // namespace meshcore_firmware
