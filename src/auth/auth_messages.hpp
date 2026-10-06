// File: auth/auth_messages.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../utils/frame/frame.hpp"
#include "../utils/proto/type_names.hpp"

namespace auth {

std::vector<uint8_t> build_handshake(const std::string& version);
std::vector<uint8_t> build_login(const std::string& xml, const std::string& sig, uint64_t account_id);
std::vector<uint8_t> build_login_frame(frame::Channel& ch, const std::string& xml, const std::string& sig, uint64_t account_id);

// Echoes the 10-byte session token from AUTH_RESP2_TYPE back to the server.
std::vector<uint8_t> build_followup_frame(frame::Channel& ch, const uint8_t token2[10], uint64_t account_id);

}
