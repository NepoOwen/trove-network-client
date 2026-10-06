// File: world/post_login_handler.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "post_login_handler.hpp"
#include "world_messages.hpp"
#include "../network/log.hpp"
#include "../utils/xigncode/challenge.hpp"

namespace world {

PostLoginHandler::~PostLoginHandler() {
    if (thread_.joinable()) thread_.detach();
}

void PostLoginHandler::on_xc_challenge(const proto::Message& m, const frame::Segment&) {
    std::string challenge;
    uint32_t challenge_id = 0;
    for (const auto& f : m.fields) {
        if (f.number == 1 && f.wire == 6) challenge = f.sval;
        if (f.number == 0) challenge_id = (uint32_t)f.ival;
    }
    if (challenge.empty()) { network::log_error("xc challenge missing field[1]"); return; }

    std::string resp = xem::solve(challenge);
    if (resp.empty()) { network::log_error("xem::solve() failed"); return; }

    std::vector<uint8_t> wire = build_xc_response_frame(conn_.channel(), challenge_id, resp);
    if (conn_.send(wire)) network::log_send("postlogin", "xc-response", wire.size());
    else network::log_error("xc response send failed");
}

void PostLoginHandler::thread_main(std::string host, uint16_t port, uint64_t account_id,
                                     int32_t session_nonce, int64_t login_finished_token) {
    if (!conn_.connect(host, port)) { network::log_error("post-login connect failed"); if (on_ack_) on_ack_(false); return; }
    network::log_info("connected post-login " + host + ":" + std::to_string(port));

    std::vector<uint8_t> ack = build_post_login_ack_frame(conn_.channel(), account_id, session_nonce, login_finished_token);
    if (!conn_.send(ack)) { network::log_error("post-login ack send failed"); if (on_ack_) on_ack_(false); return; }
    network::log_send("postlogin", "post-login-ack", ack.size());

    bool ack_signaled = false;
    for (;;) {
        frame::Segment seg;
        if (!conn_.recv_segment(seg, 60000)) { network::log_error("post-login: connection lost"); disconnected_.store(true); break; }
        if (seg.payload.empty()) continue;

        size_t off = 0;
        while (seg.payload.size() - off >= 8) {
            const uint8_t* mp = seg.payload.data() + off;
            size_t mn = seg.payload.size() - off;
            proto::Message m = proto::parse(mp, mn);
            if (!m.ok) break;
            size_t consumed = m.total != 0 ? (size_t)m.total + 4 : mn;
            if (consumed > mn) break;

            network::log_recv("postlogin", m.type, consumed);

            switch (m.type) {
                case WORLD_POST_LOGIN_ACK_ACK_TYPE:
                    if (!ack_signaled) { ack_signaled = true; if (on_ack_) on_ack_(true); }
                    break;
                case WORLD_MAP_DATA_TYPE:
                    if (on_map_data_) on_map_data_();
                    break;
                case WORLD_XC_CHALLENGE_TYPE: on_xc_challenge(m, seg); break;
                case NET_KEEPALIVE_TYPE: break;
                case NET_NM_TYPE: break;
                default: break;
            }

            off += consumed;
            if (m.total == 0) break;
        }
    }

    if (!ack_signaled && on_ack_) on_ack_(false);
}

void PostLoginHandler::start(const std::string& host, uint16_t port, uint64_t account_id,
                               int32_t session_nonce, int64_t login_finished_token,
                               std::function<void()> on_map_data,
                               std::function<void(bool)> on_ack) {
    on_map_data_ = std::move(on_map_data);
    on_ack_ = std::move(on_ack);
    thread_ = std::thread(&PostLoginHandler::thread_main, this, host, port, account_id, session_nonce, login_finished_token);
}

}
