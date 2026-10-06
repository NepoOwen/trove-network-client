// File: world/world_messages.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../utils/frame/frame.hpp"
#include "../utils/proto/type_names.hpp"

namespace world {

std::vector<uint8_t> build_xc_response_frame(frame::Channel& ch, uint32_t challenge_id, const std::string& response);

std::vector<uint8_t> build_handshake(const std::string& version, uint64_t account_id, const std::string& email);
std::vector<uint8_t> build_handshake_done_frame(frame::Channel& ch);

// Fields of the world login message. Fields 3 and 4 are never present.
struct LoginFields {
    uint64_t field0 = 0;      // account id
    uint64_t field1 = 0;      // echoes the auth socket's Resp1 token
    uint32_t field2 = 0;      // flag, always 1
    int32_t  field5 = 0;      // client-generated session nonce (random signed 32-bit)
    std::string field6;       // auth signature
    std::string field7;       // full auth ticket XML
};

std::vector<uint8_t> build_login_frame(frame::Channel& ch, const LoginFields& f);

std::vector<uint8_t> build_post_login_ack_frame(frame::Channel& ch, uint64_t account_id,
                                                 int32_t session_nonce, int64_t login_finished_token);

std::vector<uint8_t> build_channel_join_frame(frame::Channel& ch, uint64_t channel_id);
std::vector<uint8_t> build_channel_join2_frame(frame::Channel& ch);

std::vector<uint8_t> build_world_ready_frame(frame::Channel& ch);

}
