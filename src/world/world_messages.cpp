// File: world/world_messages.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "world_messages.hpp"
#include "../utils/zlib/zlib.hpp"

namespace world {

static void put_varint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back((uint8_t)((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back((uint8_t)v);
}

static void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i)));
}

// Custom int64 field encoding: 0 and 1 are tagged with no payload, negative
// values are zigzag varints, everything else is a plain varint.
static void put_int_field(std::vector<uint8_t>& out, uint32_t field, int64_t v) {
    if (v == 0) { put_varint(out, (uint64_t)field << 3); return; }
    if (v == 1) { put_varint(out, 1u | ((uint64_t)field << 3)); return; }
    if (v < 0) {
        put_varint(out, 2u | ((uint64_t)field << 3));
        put_varint(out, (uint64_t)((v << 1) ^ (v >> 63)));
        return;
    }
    put_varint(out, 3u | ((uint64_t)field << 3));
    put_varint(out, (uint64_t)v);
}

static void put_string_field(std::vector<uint8_t>& out, uint32_t field, const std::string& s) {
    put_varint(out, 6u | ((uint64_t)field << 3));
    put_varint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

static std::vector<uint8_t> wrap(uint32_t type, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> msg;
    put_u32(msg, (uint32_t)(body.size() + 4));
    put_u32(msg, type);
    msg.insert(msg.end(), body.begin(), body.end());
    return msg;
}

std::vector<uint8_t> build_handshake(const std::string& version, uint64_t account_id, const std::string& email) {
    std::vector<uint8_t> body;
    put_string_field(body, 0, version);
    put_string_field(body, 1, "1");
    put_int_field(body, 2, (int64_t)account_id);
    put_string_field(body, 3, email);
    put_int_field(body, 4, 1);
    body.push_back(0x0F);
    return wrap(WORLD_HANDSHAKE_TYPE, body);
}

std::vector<uint8_t> build_handshake_done_frame(frame::Channel& ch) {
    std::vector<uint8_t> body{ 0x0F };
    std::vector<uint8_t> msg = wrap(WORLD_HANDSHAKE_DONE_TYPE, body);
    return frame::build_game(ch, msg.data(), msg.size());
}

std::vector<uint8_t> build_xc_response_frame(frame::Channel& ch, uint32_t challenge_id, const std::string& response) {
    // The real XCResponseEvent totals 1184 bytes: field[3] is the 76-char A3
    // string + NUL + a ~1107-byte binary tail. The server only appears to
    // verify length + the A3 string, so the tail is zero-padded to match.
    // field[0] echoes the challenge's own field[0] (a per-challenge counter).
    std::string payload = response;
    payload.push_back('\0');
    if (payload.size() < 1175) payload.append(1175 - payload.size(), '\0');

    std::vector<uint8_t> body;
    put_int_field(body, 0, (int64_t)challenge_id);
    put_string_field(body, 3, payload);
    body.push_back(0x0F);

    std::vector<uint8_t> msg = wrap(WORLD_XC_RESPONSE_TYPE, body);
    return frame::build_login_raw(ch, msg.data(), msg.size());
}

std::vector<uint8_t> build_login_frame(frame::Channel& ch, const LoginFields& f) {
    std::vector<uint8_t> body;
    put_int_field(body, 0, (int64_t)f.field0);
    put_int_field(body, 1, (int64_t)f.field1);
    put_int_field(body, 2, (int64_t)f.field2);
    put_int_field(body, 5, (int64_t)f.field5);
    put_string_field(body, 6, f.field6);
    put_string_field(body, 7, f.field7);
    body.push_back(0x0F);

    std::vector<uint8_t> msg = wrap(WORLD_LOGIN_TYPE, body);
    std::vector<uint8_t> comp = zlib::deflate(msg.data(), msg.size());
    return frame::build_login(ch, comp.data(), comp.size());
}

std::vector<uint8_t> build_post_login_ack_frame(frame::Channel& ch, uint64_t account_id,
                                                 int32_t session_nonce, int64_t login_finished_token) {
    std::vector<uint8_t> body;
    put_int_field(body, 0, (int64_t)account_id);
    put_int_field(body, 1, (int64_t)session_nonce);
    put_int_field(body, 2, login_finished_token);
    body.push_back(0x0F);

    std::vector<uint8_t> msg = wrap(WORLD_POST_LOGIN_ACK_TYPE, body);
    return frame::build_game(ch, msg.data(), msg.size());
}

std::vector<uint8_t> build_channel_join_frame(frame::Channel& ch, uint64_t channel_id) {
    std::vector<uint8_t> body;
    put_int_field(body, 0, (int64_t)channel_id);
    body.push_back(0x0F);
    std::vector<uint8_t> msg = wrap(WORLD_CHANNEL_JOIN_TYPE, body);
    return frame::build_game(ch, msg.data(), msg.size());
}

std::vector<uint8_t> build_channel_join2_frame(frame::Channel& ch) {
    std::vector<uint8_t> body;
    put_int_field(body, 0, 210);
    body.push_back(0x0F);
    std::vector<uint8_t> msg = wrap(WORLD_CHANNEL_JOIN2_TYPE, body);
    return frame::build_game(ch, msg.data(), msg.size());
}

std::vector<uint8_t> build_world_ready_frame(frame::Channel& ch) {
    std::vector<uint8_t> body{ 0x0F };
    std::vector<uint8_t> msg = wrap(WORLD_READY_TYPE, body);
    return frame::build_game(ch, msg.data(), msg.size());
}

}
