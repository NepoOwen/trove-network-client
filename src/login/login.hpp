// File: login/login.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace login {

struct Ticket {
    std::string xml;
    std::string signature;
    std::string raw; // full raw multiauth response body; xml/signature are derived from this
    uint64_t account_id = 0;
    std::string email;
    std::string channel_id;
};

struct Result {
    bool ok = false;
    std::string error;
    bool token_required = false;
    std::string token_required_type; // "email" | "mobile"
    Ticket ticket;
};

// Glyph HTTP login (email + password, optionally a 2FA code).
Result authenticate(const std::string& email, const std::string& password, const std::string& auth_code);

// Default auth servers for a region ("EU" | "NA" | "PTS"), "|"-joined host:port list.
std::string default_auth_servers(const std::string& region);

// Cached ticket on disk (auth/<account_id>_raw.bin, one per account), so the
// game run doesn't need to re-authenticate with Glyph every time. xml and
// signature are derived from the raw response on load. save_ticket keys the
// filename off t.account_id itself.
bool load_cached_ticket(uint64_t account_id, Ticket& out);
bool save_ticket(const Ticket& t);

// Scans auth/ for *_raw.bin files and returns their account ids.
std::vector<uint64_t> list_cached_accounts();

}
