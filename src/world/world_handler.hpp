// File: world/world_handler.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "world_messages.hpp"
#include "post_login_handler.hpp"
#include "../network/connection.hpp"
#include "../utils/proto/proto.hpp"

namespace api { class ApiHandler; }

namespace world {

class WorldHandler {
public:
    WorldHandler();
    ~WorldHandler();

    void start(const std::string& host, uint16_t port, uint64_t account_id,
                const std::string& email, int64_t resp1_token,
                const std::string& sig, const std::string& xml,
                std::function<uint64_t()> get_channel_id,
                std::function<bool(const std::vector<uint8_t>&)> send_auth);

    bool disconnected() const { return disconnected_.load(); }

    network::Connection& connection() { return conn_; }

    void send_world_ready();

    bool send_game(const std::vector<uint8_t>& plaintext);
    bool send_auth(const std::vector<uint8_t>& plaintext) { return send_auth_ ? send_auth_(plaintext) : false; }

    void set_api_callback(std::function<void(const proto::Message&, const frame::Segment&)> cb) {
        api_cb_ = std::move(cb);
    }

private:
    void thread_main(std::string host, uint16_t port, uint64_t account_id, std::string email,
                      int64_t resp1_token, std::string sig, std::string xml);

    void on_handshake_ack(const proto::Message& m, const frame::Segment& seg);
    void on_pre_challenge(const proto::Message& m, const frame::Segment& seg);
    void on_login_finished(const proto::Message& m, const frame::Segment& seg);
    void on_xc_challenge(const proto::Message& m, const frame::Segment& seg);
    void on_world_data();
    void on_post_login_ack(bool ok);
    bool join_channel(uint64_t channel_id);

    network::Connection conn_;
    std::mutex send_mtx_;
    std::thread thread_;
    PostLoginHandler posth_;
    std::unique_ptr<api::ApiHandler> apih_;
    std::function<uint64_t()> get_channel_id_;
    std::function<bool(const std::vector<uint8_t>&)> send_auth_;

    std::string host_;
    uint16_t port_ = 0;
    uint64_t account_id_ = 0;

    LoginFields pending_login_;
    bool login_sent_ = false;
    bool rejected_ = false;

    bool login_done_ = false;
    bool login_ok_ = false;
    int64_t login_finished_token_ = 0;

    std::atomic<bool> ready_sent_{false};
    std::atomic<bool> disconnected_{false};

    std::function<void(const proto::Message&, const frame::Segment&)> api_cb_;
};

}
