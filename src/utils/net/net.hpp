// File: utils/net/net.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;
inline int closesocket(SOCKET s) { return ::close(s); }
#endif
#include <cstdint>
#include <cstddef>
#include <string>

namespace net {

// Raw blocking TCP socket. No protocol knowledge -- see network::Connection
// for the framed/encrypted layer built on top of this.
class Socket {
public:
    Socket() = default;
    ~Socket() { close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : sock_(other.sock_) { other.sock_ = INVALID_SOCKET; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) { close(); sock_ = other.sock_; other.sock_ = INVALID_SOCKET; }
        return *this;
    }

    bool connect(const std::string& host, uint16_t port);
    void close();
    bool send_all(const void* data, size_t len);
    int recv(uint8_t* buf, size_t max_len, int timeout_ms); // >0 bytes, 0 timeout, -1 error, -2 closed

    // Connected peer's numeric IP, so a second connection can reuse it
    // without another DNS lookup.
    std::string peer_ip() const;

    // Raw handle value, for log display only (e.g. "[NET] SEND sock=...").
    uintptr_t native_handle() const { return (uintptr_t)sock_; }

private:
    SOCKET sock_ = INVALID_SOCKET;
};

// Splits "host:port". Returns false if malformed.
bool parse_address(const std::string& s, std::string& host, uint16_t& port);

// One-time socket subsystem init/teardown (WSAStartup/WSACleanup on Windows,
// no-ops on POSIX). Returns false on failure.
bool init();
void cleanup();

}
