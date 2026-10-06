// File: world/post_login_handler.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include "../network/connection.hpp"
#include "../utils/proto/proto.hpp"

namespace world {

class PostLoginHandler {
public:
    ~PostLoginHandler();

    void start(const std::string& host, uint16_t port, uint64_t account_id,
                int32_t session_nonce, int64_t login_finished_token,
                std::function<void()> on_map_data,
                std::function<void(bool ok)> on_ack);

    bool disconnected() const { return disconnected_.load(); }

private:
    void thread_main(std::string host, uint16_t port, uint64_t account_id,
                      int32_t session_nonce, int64_t login_finished_token);
    void on_xc_challenge(const proto::Message& m, const frame::Segment& seg);

    network::Connection conn_;
    std::thread thread_;
    std::function<void()> on_map_data_;
    std::function<void(bool)> on_ack_;
    std::atomic<bool> disconnected_{false};
};

}
