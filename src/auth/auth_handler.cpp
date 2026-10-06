// File: auth/auth_handler.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "auth_handler.hpp"
#include "auth_messages.hpp"
#include "../network/log.hpp"
#include "../utils/net/net.hpp"
#include "../world/world_handler.hpp"
#include "../api/api_messages.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>

#define AUTO_SET_NAME_ENABLED 0                 // Set to 1 to automatically set the username to "pd<account_id>" if not already set
#define AUTO_JOIN_WORLD_ENABLED 0               // Set to 1 to automatically join the world with ID 8131749450383144715 if not already in that world
#define AUTO_JOIN_WORLD_ID 8131749450383144715  // The world ID to automatically join if AUTO_JOIN_WORLD_ENABLED is set to 1

namespace auth {
namespace {

std::string find_server(const std::vector<uint8_t>& p) {
    std::string s(p.begin(), p.end());
    static const char* suffix[] = { ".trovegame.com", ".triongames.com" };
    for (const char* suf : suffix) {
        size_t pos = s.find(suf);
        if (pos == std::string::npos) continue;
        size_t start = pos;
        while (start > 0 && (std::isalnum((unsigned char)s[start - 1]) || s[start - 1] == '-' || s[start - 1] == '.'))
            start--;
        size_t end = pos + std::strlen(suf);
        while (end < s.size() && (std::isdigit((unsigned char)s[end]) || s[end] == ':')) end++;
        return s.substr(start, end - start);
    }
    return {};
}

uint64_t find_channel_id(const std::vector<uint8_t>& p) {
    static const char* marker = "$Channel_World#";
    std::string s(p.begin(), p.end());
    size_t pos = s.find(marker);
    if (pos == std::string::npos) return 0;
    size_t start = pos + std::strlen(marker);
    size_t end = start;
    while (end < s.size() && std::isdigit((unsigned char)s[end])) end++;
    if (end == start) return 0;
    return std::strtoull(s.substr(start, end - start).c_str(), nullptr, 10);
}

} // namespace

AuthHandler::AuthHandler() = default;

AuthHandler::~AuthHandler() {
    if (thread_.joinable()) thread_.detach();
}

void AuthHandler::on_handshake_ack(const proto::Message& m, const frame::Segment&) {
    bool accepted = false;
    for (const auto& f : m.fields) if (f.number == 0) { accepted = true; break; }
    if (!accepted) { network::log_status("VersionRejected"); network::log_error("auth version rejected"); rejected_ = true; return; }
    network::log_status("LS_VersionCheck");

    std::vector<uint8_t> login_frame = build_login_frame(conn_.channel(), xml_, sig_, account_id_);
    if (conn_.send(login_frame)) {
        network::log_send("auth", "login", login_frame.size());
        login_sent_ = true;
        network::log_status("AS_Login");
        network::log_status("AS_RequestLogin");
    } else {
        rejected_ = true;
    }
}

void AuthHandler::on_resp1(const proto::Message& m, const frame::Segment&) {
    for (const auto& f : m.fields) if (f.number == 0) { resp1_token_ = f.ival; break; }
    network::log_status("LS_RecvLogin");
}

void AuthHandler::on_resp2(const proto::Message&, const frame::Segment& seg) {
    if (seg.payload.size() >= 18) {
        std::memcpy(token2_, seg.payload.data() + 8, 10);
        have_token2_ = true;
    }
    network::log_status("LS_UserAck");
    network::log_status("LS_LoginFinished");
}

void AuthHandler::on_nm_channel(const proto::Message& m, const frame::Segment&) {
    int64_t op = 0;
    const proto::Field* nested_field = nullptr;
    for (const auto& f : m.fields) {
        if (f.number == 2 && f.wire == 3) op = f.ival;
        if (f.number == 1 && f.wire == 6) nested_field = &f;
    }

    std::vector<proto::Field> nested;
    if (nested_field) proto::parse_fields((const uint8_t*)nested_field->sval.data(), nested_field->sval.size(), nested);

#if AUTO_SET_NAME_ENABLED
    // AUTO SET NAME
    static bool world_server_logged = false;
    if (op == 6021128 && nested.size()) world_server_logged = true;
    if (world_server_logged && !username_seen_ && !username_set_sent_) {
        // name
        if (op == 142529108 && nested.size() >= 2 && nested[0].wire == 6) {
            username_seen_ = true;
            return;
        }
        if (op == 17551750) {
            username_set_sent_ = true;
            std::string username = "pd" + std::to_string(account_id_);
            std::vector<uint8_t> msg = api::build_session_data_message({ api::sdf_str(0, username) }, 142529108);
            if (!send_game(msg)) network::log_error("failed to send username SessionData");
        }
    }
#endif

#if AUTO_JOIN_WORLD_ENABLED
    // AUTO JOIN WORLD
    if (op == 179901435 && nested.size() >= 2) {
        if (nested[2].ival != AUTO_JOIN_WORLD_ID) {
            std::vector<uint8_t> msg = api::build_session_data_message({ api::sdf_int(0, 4), api::sdf_int(2, AUTO_JOIN_WORLD_ID) }, 103867405);
            if (!send_game(msg)) network::log_error("failed to send joinworld");
        }
    }
#endif
}

void AuthHandler::on_any_message(const proto::Message&, const frame::Segment& seg) {
    segments_since_login_++;

    if (segments_since_login_ == 2) {
        if (have_token2_) {
            std::vector<uint8_t> fu = build_followup_frame(conn_.channel(), token2_, account_id_);
            if (conn_.send(fu)) network::log_send("auth", "followup", fu.size());
        } else {
            network::log_error("no token2 yet, skipping followup");
        }
    }

    uint64_t cid = find_channel_id(seg.payload);
    if (cid) channel_id_.store(cid);

    std::string addr = find_server(seg.payload);
    if (addr.empty() || addr == world_server_) return;
    world_found_ = true;
    world_server_ = addr;
    network::log_status("LS_WorldAssigned", addr);
    network::log_status("World_Connecting");

    std::string host; uint16_t port = 0;
    if (!net::parse_address(addr, host, port)) {
        network::log_error("bad world server address: " + addr);
        net::cleanup();
        std::exit(1);
    }
    worldh_ = std::make_unique<world::WorldHandler>();
    worldh_->start(host, port, account_id_, email_, resp1_token_, sig_, xml_,
                    [this] { return channel_id_.load(); },
                    [this](const std::vector<uint8_t>& plain) { return send_game(plain); });
}

bool AuthHandler::send_game(const std::vector<uint8_t>& plaintext) {
    std::lock_guard<std::mutex> lk(send_mtx_);
    std::vector<uint8_t> wire = frame::build_game(conn_.channel(), plaintext.data(), plaintext.size());
    return conn_.send(wire);
}

bool AuthHandler::try_server(const std::string& server, const std::string& xml, const std::string& sig, uint64_t account_id) {
    std::string host; uint16_t port = 0;
    if (!net::parse_address(server, host, port)) return false;
    if (!conn_.connect(host, port)) { network::log_error("connect failed: " + server); return false; }

    network::log_info("connected " + server);
    network::log_status("AS_Connect");
    conn_.reset_channel();
    xml_ = xml; sig_ = sig; account_id_ = account_id;
    have_token2_ = false; login_sent_ = false; segments_since_login_ = 0; rejected_ = false;
    resp1_token_ = 0; channel_id_.store(0); world_server_.clear(); world_found_ = false;
    username_seen_ = false; username_set_sent_ = false; region_seen_ = false;

    std::vector<uint8_t> hs = build_handshake("STABLE-103-451");
    if (!conn_.send(frame::build_game(conn_.channel(), hs.data(), hs.size()))) return false;
    network::log_send("auth", "handshake", hs.size());

    for (;;) {
        frame::Segment seg;
        if (!conn_.recv_segment(seg, 60000)) { network::log_error("auth: connection lost"); break; }
        if (seg.payload.empty()) continue;

        size_t off = 0;
        while (seg.payload.size() - off >= 8) {
            const uint8_t* mp = seg.payload.data() + off;
            size_t mn = seg.payload.size() - off;
            proto::Message m = proto::parse(mp, mn);
            if (!m.ok) break;
            size_t consumed = m.total != 0 ? (size_t)m.total + 4 : mn;
            if (consumed > mn) break;

            network::log_recv("auth", m.type, consumed);

            bool login_was_sent = login_sent_;

            switch (m.type) {
                case AUTH_HANDSHAKE_ACK_TYPE: on_handshake_ack(m, seg); break;
                case AUTH_RESP1_TYPE: on_resp1(m, seg); break;
                case AUTH_RESP2_TYPE: on_resp2(m, seg); break;
                case AUTH_USER_ACK_REQ_TYPE: network::log_status("LS_UserAck"); break;
                case AUTH_QUEUE_TYPE: network::log_status("LS_LoginQueue"); break;
                case AUTH_SESSION_DATA_TYPE: break;
                case NET_NM_TYPE: on_nm_channel(m, seg); break;
                case NET_KEEPALIVE_TYPE: break;
                default: break;
            }
            if (login_was_sent) on_any_message(m, seg);

            off += consumed;
            if (m.total == 0) break;
        }

        if (rejected_) break;
    }

    if (rejected_) { conn_.close(); return true; }
    return world_found_;
}

void AuthHandler::thread_main(std::vector<std::string> servers, std::string xml, std::string sig, uint64_t account_id) {
    for (const std::string& server : servers) {
        if (try_server(server, xml, sig, account_id)) {
            if (rejected_) { network::log_error("auth rejected"); net::cleanup(); std::exit(1); }
            network::log_info("auth complete via " + server);
            disconnected_.store(true);
            return;
        }
    }
    network::log_error("no auth server accepted the login");
    net::cleanup();
    std::exit(1);
}

void AuthHandler::start(const std::vector<std::string>& servers, const std::string& xml,
                          const std::string& sig, uint64_t account_id, const std::string& email) {
    email_ = email;
    thread_ = std::thread(&AuthHandler::thread_main, this, servers, xml, sig, account_id);
}

}
