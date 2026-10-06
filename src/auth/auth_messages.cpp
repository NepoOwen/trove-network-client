// File: auth/auth_messages.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "auth_messages.hpp"
#include "../utils/zlib/zlib.hpp"

namespace auth {

static void put_varint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back((uint8_t)((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back((uint8_t)v);
}

static void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xff));
    out.push_back((uint8_t)((v >> 8) & 0xff));
    out.push_back((uint8_t)((v >> 16) & 0xff));
    out.push_back((uint8_t)((v >> 24) & 0xff));
}

static void put_string(std::vector<uint8_t>& out, uint32_t field, const std::string& s) {
    put_varint(out, 6u | (field << 3));
    put_varint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

std::vector<uint8_t> build_handshake(const std::string& version) {
    std::vector<uint8_t> out;
    uint32_t total = 4u + 2u + (uint32_t)version.size() + 3u + 1u + 1u;
    put_u32(out, total);
    put_u32(out, AUTH_HANDSHAKE_TYPE);
    put_string(out, 0, version);
    const uint8_t tail[5] = { 0x0e, 0x01, 0x31, 0x11, 0x0f }; // field1="1", field2=1, end marker
    out.insert(out.end(), tail, tail + 5);
    return out;
}

std::vector<uint8_t> build_login(const std::string& xml, const std::string& sig, uint64_t account_id) {
    std::vector<uint8_t> body;
    put_string(body, 0, xml);
    put_string(body, 1, sig);
    put_varint(body, 3u | (2u << 3)); // field 2: accountId
    put_varint(body, account_id);
    body.push_back((uint8_t)(1u | (4u << 3))); // field 4: = 1
    body.push_back(0x0F);

    std::vector<uint8_t> msg;
    put_u32(msg, (uint32_t)(body.size() + 4));
    put_u32(msg, AUTH_LOGIN_TYPE);
    msg.insert(msg.end(), body.begin(), body.end());
    return msg;
}

std::vector<uint8_t> build_login_frame(frame::Channel& ch, const std::string& xml, const std::string& sig, uint64_t account_id) {
    std::vector<uint8_t> msg = build_login(xml, sig, account_id);
    std::vector<uint8_t> comp = zlib::deflate(msg.data(), msg.size());
    return frame::build_login(ch, comp.data(), comp.size());
}

std::vector<uint8_t> build_followup_frame(frame::Channel& ch, const uint8_t token2[10], uint64_t account_id) {
    // Body layout recovered from a real capture:
    //   0E 11 [nested 17 bytes] 13 <varint 0x0B2BFF4C> 0F
    //   nested = 03 <accountId> <token2(10)> 21 0F
    std::vector<uint8_t> body;
    put_varint(body, 6u | (1u << 3));
    put_varint(body, 17);
    body.push_back(0x03);
    put_varint(body, account_id);
    body.insert(body.end(), token2, token2 + 10);
    body.push_back(0x21);
    body.push_back(0x0F);
    put_varint(body, 3u | (2u << 3));
    put_varint(body, 0x0B2BFF4Cu); // constant
    body.push_back(0x0F);

    std::vector<uint8_t> msg;
    put_u32(msg, (uint32_t)(body.size() + 4));
    put_u32(msg, AUTH_SESSION_DATA_TYPE);
    msg.insert(msg.end(), body.begin(), body.end());

    return frame::build_login_raw(ch, msg.data(), msg.size());
}

}
