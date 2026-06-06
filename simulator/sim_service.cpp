#include "sim_service.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "app/app_controller.h"
#include "app/mock_data.h"

namespace {

SimService* active_service = nullptr;

bool sim_action_sink(meshcore::ActionCommand command,
                     const meshcore::AppSnapshot& before,
                     meshcore::AppSnapshot& after) {
    (void)before;
    if (active_service == nullptr) {
        return false;
    }
    if (command == meshcore::ActionCommand::SendMessage) {
        after.state.radio_state = "sim tx queued";
        after.state.last_event = "sim: tx queued";
    }
    return true;
}

}  // namespace

void SimService::begin() {
    snapshot_ = meshcore::make_mock_snapshot();
    active_service = this;
    meshcore::app_set_action_sink(sim_action_sink);
    meshcore::app_set_snapshot(snapshot_);
}

void SimService::loop(uint32_t now_ms) {
    if (now_ms - last_status_ms_ >= 1000) {
        last_status_ms_ = now_ms;
        snapshot_ = meshcore::app_snapshot();
        if (snapshot_.state.radio_state.empty()) {
            snapshot_.state.radio_state = "sim idle";
        }
        snapshot_.state.uptime_seconds = now_ms / 1000;
        snapshot_.state.current_epoch_seconds = static_cast<unsigned>(std::time(nullptr));
        snapshot_.state.heap_free_bytes = 196608;
        snapshot_.state.psram_total_bytes = 8388608;
        snapshot_.state.psram_free_bytes = 7340032;
        snapshot_.state.storage_writable = true;
        snapshot_.state.storage_state = "sim storage";
        snapshot_.state.persisted_message_count = static_cast<unsigned>(snapshot_.messages.size());
        snapshot_.state.persisted_node_count = static_cast<unsigned>(snapshot_.nodes.size());
        meshcore::app_ingest_service_snapshot(snapshot_);
    }
    uint32_t rx_interval_ms = 15000;
    if (const char* value = std::getenv("MESHCORE_SIM_RX_INTERVAL_MS")) {
        const auto parsed = std::atoi(value);
        if (parsed > 0) {
            rx_interval_ms = static_cast<uint32_t>(parsed);
        }
    }
    if (now_ms - last_rx_ms_ >= rx_interval_ms) {
        last_rx_ms_ = now_ms;
        if (std::getenv("MESHCORE_SIM_RX_ONCE") != nullptr && next_message_id_ > 900) {
            return;
        }
        injectMessage();
    }
}

void SimService::injectMessage() {
    snapshot_ = meshcore::app_snapshot();
    char id[16];
    std::snprintf(id, sizeof(id), "%u", next_message_id_++);
    snapshot_.messages.insert(snapshot_.messages.begin(),
                              {id, "Sim RX", "Packet", "Background simulator packet.", 0, false});
    if (snapshot_.messages.size() > 8) {
        snapshot_.messages.pop_back();
    }
    if (!snapshot_.nodes.empty()) {
        auto& node = snapshot_.nodes[static_cast<std::size_t>(snapshot_.state.selected_node) % snapshot_.nodes.size()];
        node.last_seen_seconds = 0;
        node.rssi = std::min(-45, node.rssi + 1);
    }
    snapshot_.state.selected_message = 0;
    snapshot_.state.last_event = "sim: rx packet";
    snapshot_.logs.insert(snapshot_.logs.begin(), snapshot_.state.last_event);
    if (snapshot_.logs.size() > 8) {
        snapshot_.logs.pop_back();
    }
    if (std::getenv("MESHCORE_SIM_INPUT_TRACE") != nullptr) {
        std::fprintf(stderr, "sim inject message id=%s\n", id);
    }
    meshcore::app_ingest_service_snapshot(snapshot_);
}
