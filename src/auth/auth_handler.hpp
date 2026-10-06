// File: auth/auth_handler.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "../network/connection.hpp"
#include "../utils/proto/proto.hpp"

namespace world { class WorldHandler; }

namespace auth {

class AuthHandler {
public:
    AuthHandler();
    ~AuthHandler();

    void start(const std::vector<std::string>& servers, const std::string& xml,
                const std::string& sig, uint64_t account_id, const std::string& email);

    bool disconnected() const { return disconnected_.load(); }

    bool send_game(const std::vector<uint8_t>& plaintext);

private:
    void thread_main(std::vector<std::string> servers, std::string xml, std::string sig, uint64_t account_id);
    bool try_server(const std::string& server, const std::string& xml, const std::string& sig, uint64_t account_id);

    void on_handshake_ack(const proto::Message& m, const frame::Segment& seg);
    void on_resp1(const proto::Message& m, const frame::Segment& seg);
    void on_resp2(const proto::Message& m, const frame::Segment& seg);
    void on_nm_channel(const proto::Message& m, const frame::Segment& seg);
    void on_any_message(const proto::Message& m, const frame::Segment& seg);

    network::Connection conn_;
    std::mutex send_mtx_;
    std::thread thread_;
    std::unique_ptr<world::WorldHandler> worldh_;

    std::string xml_, sig_, email_;
    uint64_t account_id_ = 0;
    uint8_t token2_[10] = {0};
    bool have_token2_ = false;
    bool login_sent_ = false;
    int segments_since_login_ = 0;
    bool rejected_ = false;
    bool username_seen_ = false;
    bool username_set_sent_ = false;
    bool region_seen_ = false;

    int64_t resp1_token_ = 0;
    std::atomic<uint64_t> channel_id_{0};
    std::string world_server_;
    bool world_found_ = false;

    std::atomic<bool> disconnected_{false};
};

}
