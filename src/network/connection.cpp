// File: network/connection.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "connection.hpp"

namespace network {

bool Connection::connect(const std::string& host, uint16_t port) {
    return sock_.connect(host, port);
}

void Connection::close() {
    sock_.close();
}

void Connection::reset_channel() {
    ch_ = frame::Channel();
    buf_.clear();
}

bool Connection::send(const std::vector<uint8_t>& wire) {
    return sock_.send_all(wire.data(), wire.size());
}

bool Connection::recv_segment(frame::Segment& out, int timeout_ms) {
    for (;;) {
        size_t consumed = frame::parse_segment(ch_, buf_.data(), buf_.size(), out);
        if (consumed) {
            buf_.erase(buf_.begin(), buf_.begin() + consumed);
            return true;
        }
        uint8_t rbuf[65536];
        int n = sock_.recv(rbuf, sizeof(rbuf), timeout_ms);
        if (n <= 0) return false;
        buf_.insert(buf_.end(), rbuf, rbuf + n);
    }
}

}
