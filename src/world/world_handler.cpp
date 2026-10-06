// File: world/world_handler.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "world_handler.hpp"
#include "../network/log.hpp"
#include "../utils/xigncode/challenge.hpp"
#include "../utils/net/net.hpp"
#include "../api/api_handler.hpp"
#include "../api/hotkeys.hpp"
#include <cstdlib>
#include <random>

namespace world {

WorldHandler::WorldHandler() = default;

WorldHandler::~WorldHandler() {
    if (thread_.joinable()) thread_.detach();
}

void WorldHandler::on_handshake_ack(const proto::Message& m, const frame::Segment&) {
    bool accepted = false;
    for (const auto& f : m.fields) if (f.number == 0) { accepted = true; break; }
    if (!accepted) { network::log_error("world version rejected"); rejected_ = true; return; }

    std::lock_guard<std::mutex> lk(send_mtx_);
    std::vector<uint8_t> done = build_handshake_done_frame(conn_.channel());
    if (conn_.send(done)) { network::log_send("world", "handshake-done", done.size()); network::log_status("ServerConfigData"); }
    else rejected_ = true;
}

void WorldHandler::on_pre_challenge(const proto::Message&, const frame::Segment&) {
    std::lock_guard<std::mutex> lk(send_mtx_);

    std::vector<uint8_t> ack = frame::build_game(conn_.channel(), nullptr, 0);
    if (!conn_.send(ack)) { rejected_ = true; return; }
    network::log_send("world", "pre-challenge-ack", ack.size());

    if (login_sent_) return;
    login_sent_ = true;
    std::vector<uint8_t> login_frame = build_login_frame(conn_.channel(), pending_login_);
    if (conn_.send(login_frame)) {
        network::log_send("world", "worldlogin", login_frame.size());
        network::log_status("World_Login");
        network::log_status("World_LoginFinished");
    } else {
        rejected_ = true;
    }
}

void WorldHandler::on_login_finished(const proto::Message& m, const frame::Segment&) {
    login_ok_ = true;
    for (const auto& f : m.fields) {
        if (f.number == 0 && f.wire == 6 && f.sval.size() == 2 &&
            (unsigned char)f.sval[0] == 0 && (unsigned char)f.sval[1] == 0) login_ok_ = false;
        if (f.number == 1) login_finished_token_ = f.ival;
    }
    login_done_ = true;
    if (!login_ok_) { rejected_ = true; return; }

    std::string ip = conn_.peer_ip();
    posth_.start(ip.empty() ? host_ : ip, port_, account_id_,
                 pending_login_.field5, login_finished_token_,
                 [this] { send_world_ready(); },
                 [this](bool ok) { on_post_login_ack(ok); });
}

void WorldHandler::on_post_login_ack(bool ok) {
    if (!ok) { network::log_error("post-login ack failed"); net::cleanup(); std::exit(1); }
    if (!get_channel_id_ || !join_channel(get_channel_id_())) { net::cleanup(); std::exit(1); }
}

void WorldHandler::on_world_data() {
    apih_ = std::make_unique<api::ApiHandler>(*this);

    std::thread([this] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        send_world_ready();
    }).detach();
}

void WorldHandler::on_xc_challenge(const proto::Message& m, const frame::Segment&) {
    std::string challenge;
    uint32_t challenge_id = 0;
    for (const auto& f : m.fields) {
        if (f.number == 1 && f.wire == 6) challenge = f.sval;
        if (f.number == 0) challenge_id = (uint32_t)f.ival;
    }
    if (challenge.empty()) { network::log_error("xc challenge missing field[1]"); return; }

    std::string resp = xem::solve(challenge);
    if (resp.empty()) { network::log_error("xem::solve() failed"); return; }

    std::lock_guard<std::mutex> lk(send_mtx_);
    std::vector<uint8_t> wire = build_xc_response_frame(conn_.channel(), challenge_id, resp);
    if (conn_.send(wire)) { network::log_send("world", "xc-response", wire.size()); }
    else network::log_error("xc response send failed");
}

void WorldHandler::thread_main(std::string host, uint16_t port, uint64_t account_id, std::string email,
                                 int64_t resp1_token, std::string sig, std::string xml) {
    host_ = host; port_ = port; account_id_ = account_id;

    if (!conn_.connect(host, port)) { network::log_error("world connect failed"); net::cleanup(); std::exit(1); }
    network::log_info("connected world " + host + ":" + std::to_string(port));
    network::log_status("VersionCheck");

    pending_login_.field0 = account_id;
    pending_login_.field1 = (uint64_t)resp1_token;
    pending_login_.field2 = 1;
    pending_login_.field5 = (int32_t)std::mt19937(std::random_device{}())();
    pending_login_.field6 = sig;
    pending_login_.field7 = xml;

    std::vector<uint8_t> hs = build_handshake("STABLE-103-451", account_id, email);
    if (!conn_.send(frame::build_game(conn_.channel(), hs.data(), hs.size()))) { net::cleanup(); std::exit(1); }
    network::log_send("world", "handshake", hs.size());

    for (;;) {
        frame::Segment seg;
        if (!conn_.recv_segment(seg, 60000)) { network::log_error("world: connection lost"); disconnected_.store(true); break; }
        if (seg.payload.empty()) continue;

        size_t off = 0;
        while (seg.payload.size() - off >= 8) {
            const uint8_t* mp = seg.payload.data() + off;
            size_t mn = seg.payload.size() - off;
            proto::Message m = proto::parse(mp, mn);
            if (!m.ok) break;
            size_t consumed = m.total != 0 ? (size_t)m.total + 4 : mn;
            if (consumed > mn) break;

            network::log_recv("world", m.type, consumed);

            switch (m.type) {
                case WORLD_HANDSHAKE_ACK_TYPE: on_handshake_ack(m, seg); break;
                case WORLD_PRE_CHALLENGE_TYPE: on_pre_challenge(m, seg); break;
                case WORLD_LOGIN_FINISHED_TYPE: on_login_finished(m, seg); break;
                case WORLD_XC_CHALLENGE_TYPE: on_xc_challenge(m, seg); break;
                case WORLD_DATA_TYPE:
                    network::log_info("received world data (" + std::to_string(consumed) + " bytes)");
                    network::log_status("LoadWorld");
                    if (!apih_) on_world_data();
                    break;
                case NET_KEEPALIVE_TYPE: break;
                case NET_NM_TYPE: break;
                case WORLD_LOCAL_PLAYER_LOADED_TYPE:
                case WORLD_NET_EVENT_TYPE:
                case WORLD_LEADERBOARD_ENTRIES_TYPE:
                case WORLD_MARKET_LISTINGS_TYPE:
                case WORLD_SPAWN_ENTITY_TYPE:
                    if (api_cb_) api_cb_(m, seg);
                    break;
                default: break;
            }

            off += consumed;
            if (m.total == 0) break;
        }

        if (rejected_) break;
    }

    if (rejected_) { network::log_error("world login rejected"); net::cleanup(); std::exit(1); }
}

void WorldHandler::start(const std::string& host, uint16_t port, uint64_t account_id,
                           const std::string& email, int64_t resp1_token,
                           const std::string& sig, const std::string& xml,
                           std::function<uint64_t()> get_channel_id,
                           std::function<bool(const std::vector<uint8_t>&)> send_auth) {
    get_channel_id_ = std::move(get_channel_id);
    send_auth_ = std::move(send_auth);
    thread_ = std::thread(&WorldHandler::thread_main, this, host, port, account_id, email, resp1_token, sig, xml);
}

bool WorldHandler::join_channel(uint64_t channel_id) {
    network::log_status("World_Enter");
    std::lock_guard<std::mutex> lk(send_mtx_);
    std::vector<uint8_t> join1 = build_channel_join_frame(conn_.channel(), channel_id);
    std::vector<uint8_t> join2 = build_channel_join2_frame(conn_.channel());
    if (!conn_.send(join1) || !conn_.send(join2)) { network::log_error("channel join send failed"); return false; }
    network::log_send("world", "channel-join", join1.size());
    network::log_send("world", "channel-join2", join2.size());
    network::log_status("World_Ready");
    return true;
}

void WorldHandler::send_world_ready() {
    if (ready_sent_.exchange(true)) return;
    std::lock_guard<std::mutex> lk(send_mtx_);
    std::vector<uint8_t> ready = build_world_ready_frame(conn_.channel());
    if (conn_.send(ready)) { network::log_send("world", "world-ready", ready.size()); network::log_status("LoadPlayer"); }
    printf("[LOGIN] You have loaded into the world.\n");
    if (apih_) api::start_test_hotkeys(*apih_);
}

bool WorldHandler::send_game(const std::vector<uint8_t>& plaintext) {
    std::lock_guard<std::mutex> lk(send_mtx_);
    std::vector<uint8_t> wire = frame::build_game(conn_.channel(), plaintext.data(), plaintext.size());
    return conn_.send(wire);
}

}
