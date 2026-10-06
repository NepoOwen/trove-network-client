// File: api/api_handler.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "api_messages.hpp"
#include "../world/world_handler.hpp"
#include "../utils/proto/proto.hpp"

namespace api {

// Everything in auth/, world/ and the top-level session flow exists just to
// get a live, authenticated world connection. This is the playground built
// on top of that connection: send our own NetEvents and watch what the
// server does with them. Every message this handler sends or sees gets
// fully decoded and printed via proto::dump(), unlike the rest of the
// client which stays quiet.
class ApiHandler {
public:
    explicit ApiHandler(world::WorldHandler& world);

    // field4 is a per-event-name constant seen in real captures (e.g.
    // SetMode=47, InvitePlayer=376) -- there's no known default, so every
    // call must state it explicitly. Pass 0 only for events confirmed to
    // omit field4 entirely (e.g. BeginInteraction).
    bool send_net_event(uint64_t entity_id, const std::string& event_name,
                         const std::vector<EventParam>& params, int64_t field4);

    // Sent on the auth connection, not world -- see api_messages.hpp.
    bool send_session_data(const std::vector<SessionField>& params, int64_t field2, bool emit_field0 = false);

    // Our own world entity id, kept current from field[0] of every
    // SetCombatEnergyLocal NetEvent (the one that reliably carries it).
    // 0 until the first one has been seen.
    uint64_t self_id() const { return self_id_.load(); }

    // Blocks (up to timeout_ms) until self_id() becomes non-zero, so a
    // request that arrives right after "ready" (before
    // WORLD_LOCAL_PLAYER_LOADED_TYPE has actually come in over the wire)
    // waits for it instead of failing instantly. Returns 0 on timeout.
    uint64_t wait_self_id(int timeout_ms);

private:
    void on_message(const proto::Message& m, const frame::Segment& seg);

    world::WorldHandler& world_;
    std::atomic<uint64_t> self_id_{0};

    mutable std::mutex data_mtx_;
    std::condition_variable data_cv_;
};

}
