// File: utils/net/net.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "net.hpp"
#include <cstdio>
#include <cstdlib>

namespace net {

bool init() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool Socket::connect(const std::string& host, uint16_t port) {
    sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) return false;

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u", port);
    if (::getaddrinfo(host.c_str(), port_str, &hints, &result) != 0) {
        close();
        return false;
    }

    bool ok = false;
    for (addrinfo* p = result; p; p = p->ai_next) {
        if (::connect(sock_, p->ai_addr, (int)p->ai_addrlen) == 0) { ok = true; break; }
    }
    ::freeaddrinfo(result);
    if (!ok) { close(); return false; }

    // A dead/stalled peer can otherwise block send_all() forever.
#ifdef _WIN32
    DWORD send_timeout = 15000;
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&send_timeout, sizeof(send_timeout));
#else
    struct timeval tv;
    tv.tv_sec = 15; tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, (const void*)&tv, sizeof(tv));
#endif
    return true;
}

std::string Socket::peer_ip() const {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (getpeername(sock_, (sockaddr*)&addr, &len) != 0) return "";
    char buf[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf))) return "";
    return buf;
}

void Socket::close() {
    if (sock_ != INVALID_SOCKET) {
        ::closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
}

bool Socket::send_all(const void* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        int n = ::send(sock_, (const char*)data + off, (int)(len - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

int Socket::recv(uint8_t* buf, size_t max_len, int timeout_ms) {
#ifdef _WIN32
    DWORD tv = (DWORD)timeout_ms;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const void*)&tv, sizeof(tv));
#endif
    int n = ::recv(sock_, (char*)buf, (int)max_len, 0);
    if (n == 0) return -2;
    if (n == SOCKET_ERROR) {
#ifdef _WIN32
        return WSAGetLastError() == WSAETIMEDOUT ? 0 : -1;
#else
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
#endif
    }
    return n;
}

bool parse_address(const std::string& s, std::string& host, uint16_t& port) {
    size_t colon = s.rfind(':');
    if (colon == std::string::npos) return false;
    host = s.substr(0, colon);
    port = (uint16_t)std::strtoul(s.c_str() + colon + 1, nullptr, 10);
    return !host.empty() && port != 0;
}

}
