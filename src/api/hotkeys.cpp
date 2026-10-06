// File: api/hotkeys.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
//
// GetAsyncKeyState/VK_* are Windows-only; this whole polling mechanism is a
// no-op on other platforms.
#include "hotkeys.hpp"
#include "../network/log.hpp"
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace api {

#ifdef _WIN32
namespace {

struct Hotkey {
    int vk;
    const char* label;
    std::function<void(ApiHandler&, uint64_t self_id)> fire;
    bool needs_self_id = true;
};

// field4 is the per-event-name constant seen in real captures -- it does not
// default to anything, so each entry here states the one that goes with it.
const Hotkey kHotkeys[] = {
    { VK_F1, "SetClass(11)", [](ApiHandler& a, uint64_t id) {
        a.send_net_event(id, "SetClass", { ep_int(11), ep_uint(0) }, 10);
    } },
    { VK_F2, "SetMode(1)", [](ApiHandler& a, uint64_t id) {
        a.send_net_event(id, "SetMode", { ep_int(1) }, 47);
    } },
    { VK_F3, "InvitePlayer", [](ApiHandler& a, uint64_t id) {
        a.send_net_event(id, "InvitePlayer", { ep_str("123testzz"), ep_int(0) }, 376);
    } },
    { VK_F4, "JoinPlayer", [](ApiHandler& a, uint64_t id) {
        a.send_net_event(id, "JoinPlayer", { ep_str("segesyhgseyghesgyhseg") }, 376);
    } },

    // request leaderboard data - it will arrive to `void ApiHandler::on_message(const proto::Message& m, const frame::Segment& seg) {}`
    { VK_F5, "GetLeaderboardInternal", [](ApiHandler& a, uint64_t id) {
        a.send_net_event(id, "GetLeaderboardInternal", { ep_int64(999), ep_int(1), ep_int(1000), ep_int64(3) }, 362);
    } },

    // join world id (via world server)
    { VK_F8, "RequestSpecificWorld2", [](ApiHandler& a, uint64_t id) {
        a.send_net_event(id, "RequestSpecificWorld", { ep_int64(7164068446503133809) }, 376);
    } },

    // atlas example -- join "geode_vug" (geode) with difficulty 15
    { VK_F11, "SessionData", [](ApiHandler& a, uint64_t) {
        a.send_session_data({ sdf_int(0, 72057594037927935), sdf_int(1, 0), sdf_int(3, 15), sdf_int(4, 15),
                               sdf_str(5, "geode_vug"), sdf_int(10, 1), sdf_int(17, 1) }, 55894304);
    }, false },

    // join world id (via auth server, not world server)
    { VK_F12, "SessionData", [](ApiHandler& a, uint64_t) {
        a.send_session_data({ sdf_int(0, 4), sdf_int(2, 8131749450383144715) }, 103867405);
    }, false },

};

} // namespace

void start_test_hotkeys(ApiHandler& api) {
    std::string msg = "hotkeys:";
    for (const Hotkey& k : kHotkeys) msg += std::string(" F") + std::to_string(k.vk - VK_F1 + 1) + "=" + k.label;
    network::log_info(msg);

    std::thread([&api] {
        bool down[std::size(kHotkeys)] = {};
        for (;;) {
            for (size_t i = 0; i < std::size(kHotkeys); i++) {
                bool pressed = (GetAsyncKeyState(kHotkeys[i].vk) & 0x8000) != 0;
                if (pressed && !down[i]) {
                    uint64_t id = api.self_id();
                    if (kHotkeys[i].needs_self_id && !id) {
                        network::log_error("hotkey: self id not seen yet (waiting for LocalPlayerJoined event)");
                    } else {
                        network::log_info(std::string("hotkey: ") + kHotkeys[i].label);
                        kHotkeys[i].fire(api, id);
                    }
                }
                down[i] = pressed;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }).detach();
}

#else

void start_test_hotkeys(ApiHandler&) {
    network::log_info("hotkeys: unavailable on this platform (Windows-only)");
}

#endif

}
