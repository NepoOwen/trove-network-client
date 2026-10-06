// File: network/connection.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../utils/net/net.hpp"
#include "../utils/frame/frame.hpp"

namespace network {

// One physical TCP connection plus its framing/encryption state. Every
// subhandler (auth, world, post-login) talks to its socket through one of
// these instead of juggling a net::Socket and a frame::Channel separately.
class Connection {
public:
    bool connect(const std::string& host, uint16_t port);
    void close();

    // Fresh CFB + inflate state, e.g. before retrying a handshake on a new
    // underlying socket.
    void reset_channel();

    bool send(const std::vector<uint8_t>& wire);

    // Reads and decodes exactly one segment, blocking up to timeout_ms.
    bool recv_segment(frame::Segment& out, int timeout_ms);

    std::string peer_ip() const { return sock_.peer_ip(); }
    uintptr_t native_handle() const { return sock_.native_handle(); }
    frame::Channel& channel() { return ch_; }

private:
    net::Socket sock_;
    frame::Channel ch_;
    std::vector<uint8_t> buf_;
};

}
