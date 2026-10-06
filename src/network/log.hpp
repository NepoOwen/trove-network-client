// File: network/log.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "../utils/frame/frame.hpp"

namespace network {

void log_send(const char* who, const char* what, size_t bytes);
void log_recv(const char* who, uint32_t type, size_t bytes);
void log_info(const std::string& msg);
void log_error(const std::string& msg);

// Mirrors the real client's $LoadingStatus_* loading-screen text.
void log_status(const std::string& name, const std::string& extra = std::string());

}
