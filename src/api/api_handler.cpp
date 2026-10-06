// File: api/api_handler.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "api_handler.hpp"
#include "../network/log.hpp"
#include <chrono>
#include <cstdio>

namespace api {
namespace {

void log_net(const char* dir, uintptr_t sock, const std::vector<uint8_t>& plain) {
    printf("[NET] %s sock=%zu plain=%zu\n", dir, (size_t)sock, plain.size());
    if (plain.size() < 8) return;
    proto::Message m = proto::parse(plain.data(), plain.size());
    proto::dump(m, "[NET]   ", plain.data());
}

} // namespace

ApiHandler::ApiHandler(world::WorldHandler& world) : world_(world) {
    world_.set_api_callback([this](const proto::Message& m, const frame::Segment& seg) { on_message(m, seg); });
}

bool ApiHandler::send_net_event(uint64_t entity_id, const std::string& event_name,
                                 const std::vector<EventParam>& params, int64_t field4) {
    std::vector<uint8_t> plain = build_net_event_message(entity_id, event_name, params, field4);
    log_net("SEND", world_.connection().native_handle(), plain);
    return world_.send_game(plain);
}

bool ApiHandler::send_session_data(const std::vector<SessionField>& params, int64_t field2, bool emit_field0) {
    std::vector<uint8_t> plain = build_session_data_message(params, field2, emit_field0);
    log_net("SEND(auth)", 0, plain);
    bool ok = world_.send_auth(plain);
    if (!ok) network::log_error("send_session_data: send_auth failed (no auth connection yet?)");
    return ok;
}

void ApiHandler::on_message(const proto::Message& m, const frame::Segment& seg) {
    //log_net("RECV", world_.connection().native_handle(), seg.payload);

    if (m.type == WORLD_LEADERBOARD_ENTRIES_TYPE || m.type == WORLD_MARKET_LISTINGS_TYPE) {
        proto::dump(m, "[NET]   ", seg.payload.data());
        return;
    }

    if (m.type == WORLD_LOCAL_PLAYER_LOADED_TYPE) {
        uint64_t id = 0;
        for (const proto::Field& f : m.fields) {
            if (f.number == 0 && f.wire == 3) id = (uint64_t)f.ival;
        }
        if (id) {
            self_id_.store(id);
            data_cv_.notify_all(); // wake wait_self_id() -- and send any queued events here, once we have one
        }
        return;
    }

    if (m.type != WORLD_NET_EVENT_TYPE) return;
    uint64_t id = 0;
    std::string name;
    for (const proto::Field& f : m.fields) {
        if (f.number == 0 && f.wire == 3) id = (uint64_t)f.ival;
        if (f.number == 2 && f.wire == 6) name = f.sval;
    }
}

uint64_t ApiHandler::wait_self_id(int timeout_ms) {
    std::unique_lock<std::mutex> lk(data_mtx_);
    data_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return self_id_.load() != 0; });
    return self_id_.load();
}

}
