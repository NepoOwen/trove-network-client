// File: app/session.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "session.hpp"
#include "selftest.hpp"
#include "../login/login.hpp"
#include "../auth/auth_handler.hpp"
#include "../network/log.hpp"
#include "../utils/net/net.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace app {
namespace {

std::vector<std::string> split_servers(const std::string& list) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t e = list.find('|', start);
        if (e == std::string::npos) { if (start < list.size()) out.push_back(list.substr(start)); break; }
        out.push_back(list.substr(start, e - start));
        start = e + 1;
    }
    return out;
}

// Authenticates with Glyph and caches the resulting ticket. On success,
// *out_account_id is set and this returns true; on failure an error is
// logged (including the 2FA-required case) and this returns false.
bool authenticate_and_save(const std::string& email, const std::string& password,
                            const std::string& code, uint64_t* out_account_id) {
    login::Result r = login::authenticate(email, password, code);
    if (r.token_required) {
        network::log_error("2FA required (" + r.token_required_type + "). Rerun with the code.");
        return false;
    }
    if (!r.ok) { network::log_error(r.error); return false; }
    if (r.ticket.signature.size() != 64) { network::log_error("signature must be 64 bytes"); return false; }
    if (!login::save_ticket(r.ticket)) { network::log_error("failed to save ticket"); return false; }
    *out_account_id = r.ticket.account_id;
    return true;
}

int do_login(int argc, char** argv) {
    std::string email = argv[2], password = argv[3];
    std::string code = argc >= 5 ? argv[4] : "";

    uint64_t account_id = 0;
    if (!authenticate_and_save(email, password, code, &account_id)) return 1;
    network::log_info("saved ticket, account=" + std::to_string(account_id));
    return 0;
}

// Prompts on stdin for credentials and logs in, for the case where no
// cached ticket exists yet and none was provided via --login.
bool interactive_login(uint64_t* out_account_id) {
    std::string email, password, code;
    printf("No cached accounts found -- log in now.\n");
    printf("Email: ");
    if (!std::getline(std::cin, email)) return false;
    printf("Password: ");
    if (!std::getline(std::cin, password)) return false;
    printf("2FA code (leave blank if none): ");
    if (!std::getline(std::cin, code)) return false;

    return authenticate_and_save(email, password, code, out_account_id);
}

int run_game(const std::string& region, uint64_t account_id) {
    login::Ticket ticket;
    if (!login::load_cached_ticket(account_id, ticket)) {
        network::log_error("missing/invalid cached ticket for account " + std::to_string(account_id) + " -- run with --login first");
        return 1;
    }

    std::vector<std::string> servers = region.find(':') != std::string::npos
        ? std::vector<std::string>{ region }
        : split_servers(login::default_auth_servers(region));

    if (!net::init()) { network::log_error("socket init failed"); return 1; }

    auth::AuthHandler authh;
    authh.start(servers, ticket.xml, ticket.signature, ticket.account_id, ticket.email);

    network::log_info("staying connected (Ctrl+C to stop)");
    for (;;) {
        if (authh.disconnected()) {
            network::log_error("lost connection -- exiting so the master can respawn this account");
            net::cleanup();
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace

int run(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--selftest")
        return run_selftests() ? 0 : 1;

    if (argc >= 4 && std::string(argv[1]) == "--login")
        return do_login(argc, argv);

    std::string region = argc > 1 ? argv[1] : "EU";
    uint64_t account_id = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 0;
    if (!account_id) {
        std::vector<uint64_t> cached = login::list_cached_accounts();
        if (cached.size() == 1) {
            account_id = cached.front();
        } else if (cached.empty()) {
            if (!interactive_login(&account_id)) return 1;
        } else {
            network::log_error("no account id given, and auth/ has " + std::to_string(cached.size()) +
                                " cached accounts (need exactly 1 to auto-select) -- pass one explicitly");
            return 1;
        }
    }

    return run_game(region, account_id);
}

}
