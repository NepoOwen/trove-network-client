// File: network/log.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "log.hpp"
#include "../utils/proto/proto.hpp"
#include <cstdio>

namespace network {

void log_send(const char* who, const char* what, size_t bytes) {
    return; // disabled for now
    printf("[%s] -> %s (%zu bytes)\n", who, what, bytes);
}

void log_recv(const char* who, uint32_t type, size_t bytes) {
    return; // disabled for now
    if (type == WORLD_MAP_DATA_TYPE || type == WORLD_SPAWN_ENTITY_TYPE) return;
    const char* name = proto::type_name(type);
    if (name) printf("[%s] <- %s (%zu bytes)\n", who, name, bytes);
    // unidentified type: stay quiet rather than spam raw hex for every one
}

void log_info(const std::string& msg) { printf("[*] %s\n", msg.c_str()); }
void log_error(const std::string& msg) { printf("[!] %s\n", msg.c_str()); }

void log_status(const std::string& name, const std::string& extra) {
    printf("[LOGIN] $LoadingStatus_%s%s%s\n", name.c_str(), extra.empty() ? "" : " : ", extra.c_str());
}

}
