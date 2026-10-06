// File: utils/proto/proto.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
//
// Clean-room decoder for Trove's wire message format.
//
// Every message (after optional zlib decompression) is:
//   [u32 total][u32 type][fields...]
// where total = 4 (type) + size-of-fields. Fields use a custom
// protobuf-style encoding:
//   tag (varint) -> wire = tag & 7, field = tag >> 3   (wire 0..6)
//   tag 0x0F (wire 15) = end-of-message marker
//   wire 0 = int value 0         (no payload)
//   wire 1 = int value 1         (no payload)
//   wire 2 = zigzag varint       (signed int)
//   wire 3 = varint              (unsigned int)
//   wire 6 = length-delimited    (string / bytes / nested message)
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "type_names.hpp"

namespace proto {

struct Field {
    uint32_t number = 0;
    uint32_t wire = 0;
    int64_t  ival = 0;
    std::string sval;
    size_t   value_off = 0; // byte offset of a wire=6 value within its message buffer, for dump()
};

struct Message {
    bool ok = false;
    uint32_t total = 0;
    uint32_t type = 0;
    std::vector<Field> fields;
};

inline uint32_t get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline uint64_t read_varint(const uint8_t* p, size_t n, size_t& off) {
    uint64_t v = 0;
    int shift = 0;
    while (off < n && shift < 64) {
        uint8_t b = p[off++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return v;
}

inline int64_t zigzag_decode(uint64_t z) {
    return (int64_t)(z >> 1) ^ -(int64_t)(z & 1);
}

// Tag decoding matches the game: when tag & 7 == 7 the field sits in the
// high bits (tag >> 6) and the wire is 6 bits (tag & 0x3F); otherwise
// field = tag >> 3 and wire = tag & 7. Wire 15 (tag 0x0F) = end marker.
inline void decode_tag(uint64_t tag, uint32_t& wire, uint32_t& field) {
    if ((tag & 7) == 7) {
        wire = (uint32_t)(tag & 0x3F);
        field = (uint32_t)(tag >> 6);
    } else {
        wire = (uint32_t)(tag & 7);
        field = (uint32_t)(tag >> 3);
    }
}

inline Message parse(const uint8_t* p, size_t n) {
    Message m;
    if (n < 8) return m;
    m.total = get_u32(p);
    m.type = get_u32(p + 4);
    size_t off = 8;
    while (off < n) {
        size_t before = off;
        uint64_t tag = read_varint(p, n, off);
        if (off == before) return m; // malformed tag
        uint32_t wire, field;
        decode_tag(tag, wire, field);
        if (wire == 15) { m.ok = true; return m; } // end marker
        Field f;
        f.number = field;
        f.wire = wire;
        switch (wire) {
            case 0: f.ival = 0; break;
            case 1: f.ival = 1; break;
            case 2: f.ival = zigzag_decode(read_varint(p, n, off)); break;
            case 3: f.ival = (int64_t)read_varint(p, n, off); break;
            case 6: {
                uint64_t len = read_varint(p, n, off);
                f.value_off = off;
                if (off + len > n) return m; // truncated
                f.sval.assign((const char*)(p + off), (size_t)len);
                off += (size_t)len;
                break;
            }
            default: f.ival = (int64_t)read_varint(p, n, off); break; // extended wire: unsigned varint
        }
        m.fields.push_back(f);
    }
    return m; // ran off the end without an end marker
}

// Parse a bare field sequence (no [total][type] header), e.g. a nested
// wire=6 blob. Only returns true if it terminates with the 0x0F end marker.
inline bool parse_fields(const uint8_t* p, size_t n, std::vector<Field>& out) {
    size_t off = 0;
    while (off < n) {
        size_t before = off;
        uint64_t tag = read_varint(p, n, off);
        if (off == before) return false;
        uint32_t wire, field;
        decode_tag(tag, wire, field);
        if (wire == 15) return true;
        Field f;
        f.number = field;
        f.wire = wire;
        switch (wire) {
            case 0: f.ival = 0; break;
            case 1: f.ival = 1; break;
            case 2: f.ival = zigzag_decode(read_varint(p, n, off)); break;
            case 3: f.ival = (int64_t)read_varint(p, n, off); break;
            case 6: {
                uint64_t len = read_varint(p, n, off);
                f.value_off = off;
                if (off + len > n) return false;
                f.sval.assign((const char*)(p + off), (size_t)len);
                off += (size_t)len;
                break;
            }
            default: f.ival = (int64_t)read_varint(p, n, off); break;
        }
        out.push_back(f);
    }
    return false;
}

inline const char* type_name(uint32_t t) {
    switch (t) {
        case AUTH_LOGIN_TYPE: return "Login(auth)";
        case AUTH_HANDSHAKE_TYPE: return "Handshake";
        case AUTH_HANDSHAKE_ACK_TYPE: return "HandshakeAck";
        case AUTH_RESP1_TYPE: return "Resp1";
        case AUTH_USER_ACK_REQ_TYPE: return "UserAckReq";
        case AUTH_RESP2_TYPE: return "Resp2(token)";
        case AUTH_QUEUE_TYPE: return "Queue";
        case NET_NM_TYPE: return "NM(channel)";
        case NET_KEEPALIVE_TYPE: return "Keepalive";
        case AUTH_SESSION_DATA_TYPE: return "SessionData";
        case WORLD_LOGIN_TYPE: return "WorldLogin";
        case WORLD_LOGIN_FINISHED_TYPE: return "LoginFinished";
        case WORLD_HANDSHAKE_TYPE: return "WorldHandshake";
        case WORLD_HANDSHAKE_ACK_TYPE: return "WorldHandshakeAck";
        case WORLD_HANDSHAKE_DONE_TYPE: return "WorldHandshakeDone";
        case WORLD_PRE_CHALLENGE_TYPE: return "PreChallenge";
        case WORLD_XC_CHALLENGE_TYPE: return "XCChallenge";
        case WORLD_XC_RESPONSE_TYPE: return "XCResponse";
        case WORLD_POST_LOGIN_ACK_TYPE: return "PostLoginAck";
        case WORLD_POST_LOGIN_ACK_ACK_TYPE: return "PostLoginAckAck";
        case WORLD_CHANNEL_JOIN_TYPE: return "ChannelJoin";
        case WORLD_CHANNEL_JOIN2_TYPE: return "ChannelJoin2";
        case WORLD_DATA_TYPE: return "WorldData";
        case WORLD_READY_TYPE: return "WorldReady";
        case WORLD_MAP_DATA_TYPE: return "MapData";
        case AUTH_VERSION_ERROR_TYPE: return "VersionError";
        case AUTH_RECV_LOGIN_TYPE: return "RecvLogin";
        case AUTH_VERSION_REJECTED_TYPE: return "VersionRejected";
        case WORLD_PACKET_DISPATCH_TYPE: return "WorldPacketDispatch";
        case WORLD_NET_EVENT_TYPE: return "NetEvent";
        case WORLD_LEADERBOARD_ENTRIES_TYPE: return "LeaderboardEntries";
        case WORLD_MARKET_LISTINGS_TYPE: return "MarketListings";
        case WORLD_SPAWN_ENTITY_TYPE: return "SpawnEntity";
        case WORLD_ENTITY_UPDATE_TYPE: return "EntityUpdate";
        case WORLD_LOCALIZED_NOTICE_TYPE: return "LocalizedNotice";
        case WORLD_POSITION_UPDATE_TYPE: return "PositionUpdate";
        case WORLD_ROTATION_UPDATE_TYPE: return "RotationUpdate";
        case WORLD_PLAYER_FULL_DATA_TYPE: return "PlayerFullData";
        case WORLD_ENTITY_POSITION_TYPE: return "EntityPosition";
        default: return nullptr;
    }
}

inline bool is_printable(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c > 0x7E) return false;
    return true;
}

inline bool is_plausible_field(uint32_t wire, uint32_t field) {
    return (wire == 0 || wire == 1 || wire == 2 || wire == 3 || wire == 6) && field <= 255;
}

inline bool is_plausible_fields(const std::vector<Field>& fields) {
    if (fields.empty()) return false;
    for (const Field& f : fields)
        if (!is_plausible_field(f.wire, f.number)) return false;
    return true;
}

// ===== typed event-param list (NetEvent payload = field[1] of 0x06A9613B) =====
// Each param is [zigzag varint: type][value]:
//   0=int(zigzag varint)  1=uint/bool(varint)  3=int64(8B LE)
//   4=string(varint len + bytes)  5=float(4B LE)  6=double(8B LE)
//   7=vec3f(3x float LE)  8=vec3(3x zigzag varint)  11=vec3[](varint count + count x vec3)
inline bool read_varint_b(const uint8_t* p, size_t n, size_t& off, uint64_t& out) {
    uint64_t v = 0;
    int shift = 0;
    while (off < n && shift < 64) {
        uint8_t b = p[off++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { out = v; return true; }
        shift += 7;
    }
    return false;
}

inline bool parse_event_params(const uint8_t* p, size_t n, const std::string& prefix, bool print) {
    if (n == 0) return false;
    size_t off = 0;
    int idx = 0;
    while (off < n) {
        uint64_t tz;
        if (!read_varint_b(p, n, off, tz)) return false;
        int64_t type = zigzag_decode(tz);
        switch (type) {
        case 0: {
            uint64_t z;
            if (!read_varint_b(p, n, off, z)) return false;
            if (print) printf("%sparam[%d] int = %lld\n", prefix.c_str(), idx, (long long)zigzag_decode(z));
            break;
        }
        case 1: {
            uint64_t v;
            if (!read_varint_b(p, n, off, v)) return false;
            if (print) printf("%sparam[%d] uint/bool = %llu\n", prefix.c_str(), idx, (unsigned long long)v);
            break;
        }
        case 3: {
            if (off + 8 > n) return false;
            int64_t v = 0;
            for (int i = 0; i < 8; i++) v |= (int64_t)p[off + i] << (8 * i);
            off += 8;
            if (print) printf("%sparam[%d] int64 = %lld\n", prefix.c_str(), idx, (long long)v);
            break;
        }
        case 4: {
            uint64_t len;
            if (!read_varint_b(p, n, off, len)) return false;
            if (off + len > n) return false;
            if (print) printf("%sparam[%d] string = \"%s\"\n", prefix.c_str(), idx, std::string((const char*)(p + off), (size_t)len).c_str());
            off += (size_t)len;
            break;
        }
        case 5: {
            if (off + 4 > n) return false;
            uint32_t bits = get_u32(p + off);
            float f; memcpy(&f, &bits, 4);
            off += 4;
            if (print) printf("%sparam[%d] float = %g\n", prefix.c_str(), idx, (double)f);
            break;
        }
        case 6: {
            if (off + 8 > n) return false;
            uint64_t bits = (uint64_t)get_u32(p + off) | ((uint64_t)get_u32(p + off + 4) << 32);
            double d; memcpy(&d, &bits, 8);
            off += 8;
            if (print) printf("%sparam[%d] double = %g\n", prefix.c_str(), idx, d);
            break;
        }
        case 7: {
            if (off + 12 > n) return false;
            float v[3]; memcpy(v, p + off, 12);
            off += 12;
            if (print) printf("%sparam[%d] vec3f = (%g, %g, %g)\n", prefix.c_str(), idx, v[0], v[1], v[2]);
            break;
        }
        case 8: {
            uint64_t a, b, c;
            if (!read_varint_b(p, n, off, a)) return false;
            if (!read_varint_b(p, n, off, b)) return false;
            if (!read_varint_b(p, n, off, c)) return false;
            if (print) printf("%sparam[%d] vec3 = (%lld, %lld, %lld)\n", prefix.c_str(), idx,
                              (long long)zigzag_decode(a), (long long)zigzag_decode(b), (long long)zigzag_decode(c));
            break;
        }
        case 11: {
            uint64_t cnt;
            if (!read_varint_b(p, n, off, cnt)) return false;
            if (cnt > n) return false;
            if (print) printf("%sparam[%d] vec3[%llu] =\n", prefix.c_str(), idx, (unsigned long long)cnt);
            std::string ind = prefix + "  ";
            for (uint64_t i = 0; i < cnt; i++) {
                uint64_t a, b, c;
                if (!read_varint_b(p, n, off, a)) return false;
                if (!read_varint_b(p, n, off, b)) return false;
                if (!read_varint_b(p, n, off, c)) return false;
                if (print) printf("%s(%lld, %lld, %lld)\n", ind.c_str(),
                                  (long long)zigzag_decode(a), (long long)zigzag_decode(b), (long long)zigzag_decode(c));
            }
            break;
        }
        default:
            return false;
        }
        idx++;
    }
    return true;
}

// ===== leaderboard-style record array (wire=6 "repeated struct" content) =====
// [id: zigzag varint][score: word-swapped double, immediately after id --
// no padding, no gap][rank: zigzag varint][lit_rank: zigzag varint]
// [u8 len][name]. id/rank/lit_rank each take however many bytes their value
// needs; the header fields are parsed directly (not guessed by scanning for
// the next plausible name) because the score field regularly ends in a byte
// pair that looks exactly like a valid one-character [len][name].
struct LeaderboardRecord {
    int64_t id = 0;
    double score = 0;
    int64_t rank = 0;
    int64_t lit_rank = 0;
    std::string name;
};

inline LeaderboardRecord decode_record_header(const uint8_t* h, int64_t header_size) {
    LeaderboardRecord r;
    size_t off = 0;
    r.id = zigzag_decode(read_varint(h, (size_t)header_size, off));
    if (header_size - (int64_t)off >= 8) {
        uint32_t lo = get_u32(h + off);
        uint32_t hi = get_u32(h + off + 4);
        uint64_t bits = ((uint64_t)hi << 32) | lo;
        memcpy(&r.score, &bits, 8);
        off += 8;
    }
    if ((int64_t)off < header_size) r.rank = zigzag_decode(read_varint(h, (size_t)header_size, off));
    if ((int64_t)off < header_size) r.lit_rank = zigzag_decode(read_varint(h, (size_t)header_size, off));
    return r;
}

// Walk one record's id/score/rank/lit_rank fields (see decode_record_header)
// without interpreting them, just to find where they end.
inline bool skip_leaderboard_header(const uint8_t* p, size_t n, size_t& pos) {
    if (pos >= n) return false;
    read_varint(p, n, pos); // id
    if (pos + 8 > n) return false;
    pos += 8; // score
    if (pos >= n) return false;
    read_varint(p, n, pos); // rank
    if (pos >= n) return false;
    read_varint(p, n, pos); // lit_rank
    return true;
}

// [u32 count] x { id/score/rank/lit_rank, [u8 len][name] } -- LeaderboardEntries.
inline bool decode_named_record_array(const uint8_t* p, size_t n, std::vector<LeaderboardRecord>& out) {
    if (n < 4) return false;
    uint32_t count = get_u32(p);
    if (count == 0 || count > 100000) return false;
    size_t pos = 4;
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        size_t start = pos;
        if (!skip_leaderboard_header(p, n, pos)) return false;
        if (pos >= n) return false;
        uint8_t len = p[pos];
        if (len == 0 || len > 64 || pos + 1 + len > n) return false;
        for (size_t k = 0; k < len; k++) {
            unsigned char c = p[pos + 1 + k];
            if (c < 0x20 || c > 0x7E) return false;
        }
        LeaderboardRecord r = decode_record_header(p + start, (int64_t)(pos - start));
        r.name.assign((const char*)(p + pos + 1), len);
        out.push_back(std::move(r));
        pos = pos + 1 + len;
    }
    if (pos != n) return false;
    return true;
}

inline bool try_named_record_array(const uint8_t* p, size_t n, const std::string& prefix, bool print) {
    std::vector<LeaderboardRecord> recs;
    if (!decode_named_record_array(p, n, recs)) return false;
    if (print) {
        for (size_t i = 0; i < recs.size(); i++) {
            const LeaderboardRecord& r = recs[i];
            printf("%srecord[%zu] id=%lld score=%g rank=%lld lit_rank=%lld name=\"%s\"\n",
                   prefix.c_str(), i, (long long)r.id, r.score, (long long)r.rank, (long long)r.lit_rank, r.name.c_str());
        }
    }
    return true;
}

// [u32 count] x { qty varint, price varint, then a run of [u8 len]
// [printable string] } -- MarketListings. qty/price are plain zigzag
// values, not tag-encoded fields. count is a soft bound, not trusted
// exactly, so records are decoded until the buffer is exhausted rather than
// for a fixed number of iterations.
struct MarketRecord {
    int64_t qty = 0;
    int64_t price = 0;
    std::vector<std::string> strs;
};

inline bool decode_tagged_record_array(const uint8_t* p, size_t n, std::vector<MarketRecord>& out) {
    if (n < 4) return false;
    uint32_t count = get_u32(p);
    if (count == 0 || count > 100000) return false;
    size_t pos = 4;
    out.clear();
    while (pos < n) {
        size_t before = pos;
        int64_t qty = zigzag_decode(read_varint(p, n, pos));
        if (pos == before) return false;
        if (pos >= n) return false;
        int64_t price = zigzag_decode(read_varint(p, n, pos));
        std::vector<std::string> strs;
        while (pos < n) {
            uint8_t len = p[pos];
            if (len == 0 || len > 200 || pos + 1 + len > n) break;
            bool printable = true;
            for (size_t k = 0; k < len; k++) {
                unsigned char c = p[pos + 1 + k];
                if (c < 0x20 || c > 0x7E) { printable = false; break; }
            }
            if (!printable) break;
            strs.emplace_back((const char*)(p + pos + 1), len);
            pos += 1 + len;
        }
        if (strs.empty()) return false;
        MarketRecord r;
        r.qty = qty;
        r.price = price;
        r.strs = std::move(strs);
        out.push_back(std::move(r));
        if (out.size() > 100000) return false;
    }
    return true;
}

inline bool try_tagged_record_array(const uint8_t* p, size_t n, const std::string& prefix, bool print) {
    std::vector<MarketRecord> recs;
    if (!decode_tagged_record_array(p, n, recs)) return false;
    if (print) {
        for (size_t i = 0; i < recs.size(); i++) {
            printf("%srecord[%zu] qty=%lld price=%lld", prefix.c_str(), i, (long long)recs[i].qty, (long long)recs[i].price);
            for (const std::string& s : recs[i].strs) printf(" \"%s\"", s.c_str());
            printf("\n");
        }
    }
    return true;
}

// Print one field, recursing into nested field sequences or a typed
// event-param list. base, if given, is the start of the buffer f came from,
// used only to print each wire=6 value's address.
inline void dump_field(const Field& f, const std::string& prefix, int depth, const uint8_t* base = nullptr) {
    printf("%sfield[%u] wire=%u ", prefix.c_str(), f.number, f.wire);
    switch (f.wire) {
        case 0: printf("= 0\n"); break;
        case 1: printf("= 1\n"); break;
        case 2: printf("= %lld\n", (long long)f.ival); break;
        case 3: printf("= %llu\n", (unsigned long long)f.ival); break;
        case 6: {
            if (base) printf("@%p ", (const void*)(base + f.value_off));
            // Check printability first: a nested-field or event-param parse
            // can spuriously "succeed" on plain text that happens to contain
            // a few bytes that look like valid tag/value pairs -- that's a
            // false positive, not evidence it's actually structured data.
            if (is_printable(f.sval)) {
                printf("= \"%s\"\n", f.sval.c_str());
                break;
            }
            if (depth < 4) {
                const uint8_t* bp = (const uint8_t*)f.sval.data();
                size_t bn = f.sval.size();
                std::vector<Field> nested;
                if (parse_fields(bp, bn, nested) && is_plausible_fields(nested)) {
                    printf("= nested\n");
                    std::string ind = prefix + "  ";
                    for (const Field& nf : nested) dump_field(nf, ind, depth + 1, bp);
                    return;
                }
                if (parse_event_params(bp, bn, prefix + "  ", false)) {
                    printf("= params\n");
                    parse_event_params(bp, bn, prefix + "  ", true);
                    return;
                }
                if (try_named_record_array(bp, bn, prefix + "  ", false)) {
                    printf("= records\n");
                    try_named_record_array(bp, bn, prefix + "  ", true);
                    return;
                }
                if (try_tagged_record_array(bp, bn, prefix + "  ", false)) {
                    printf("= records\n");
                    try_tagged_record_array(bp, bn, prefix + "  ", true);
                    return;
                }
            }
            printf("= bytes[%zu]: ", f.sval.size());
            size_t cap = f.sval.size() < 48 ? f.sval.size() : 48;
            for (size_t i = 0; i < cap; i++) printf("%02X", (unsigned char)f.sval[i]);
            if (f.sval.size() > cap) printf("...");
            printf("\n");
            break;
        }
        default: printf("= %llu\n", (unsigned long long)f.ival); break;
    }
}

// Print one decoded message with typed fields. base, if given, is the start
// of the buffer m was parsed from, used to print each field's address.
inline void dump(const Message& m, const char* prefix = "  ", const uint8_t* base = nullptr) {
    const char* nm = type_name(m.type);
    printf("%sMESSAGE type=0x%08X%s%s total=%u fields=%zu%s%p\n",
           prefix, m.type, nm ? " (" : "", nm ? nm : "", m.total, m.fields.size(),
           base ? " @" : "", (const void*)base);
    for (const Field& f : m.fields) dump_field(f, prefix, 0, base);
}

// ===== PlayerFullData (0x0E3E4612) reflection decoder =====
// Decompressed field[1] = [u8 version][zigzag varint entity_id][varint body_len][body].
// The body is a list of reflection structs, each field = [varint tag] where
// zigzag(tag) = field<<3 | type, terminated by 0x1E (zigzag 15).
//   type 0 = zigzag varint, 1 = plain varint, 2 = float(4B), 3 = int64(8B),
//   4 = string(varint len + bytes), 6 = length-prefixed blob, 7 = nested struct.
inline void dump_hex(const uint8_t* p, size_t n, const char* prefix, size_t cap) {
    if (n > cap) n = cap;
    for (size_t row = 0; row < n; row += 16) {
        printf("%s  %06zX  ", prefix, row);
        for (size_t k = 0; k < 16; k++) {
            if (row + k < n) printf("%02X ", p[row + k]); else printf("   ");
        }
        printf(" ");
        for (size_t k = 0; k < 16; k++) {
            if (row + k < n) { unsigned char c = p[row + k]; putchar((c >= 0x20 && c <= 0x7E) ? c : '.'); }
            else putchar(' ');
        }
        printf("\n");
    }
}

inline bool dump_reflection(const uint8_t* p, size_t n, const std::string& prefix, int depth, size_t* consumed) {
    if (depth > 10) return false;
    size_t off = 0;
    int count = 0;
    while (off < n) {
        size_t before = off;
        uint64_t raw = read_varint(p, n, off);
        if (off == before) return false;
        int64_t tv = zigzag_decode(raw);
        if (tv == 15) { if (consumed) *consumed = off; return true; }
        if (tv < 0) return false;
        uint32_t field = (uint32_t)(tv >> 3);
        uint32_t type  = (uint32_t)(tv & 7);
        printf("%s[%d] +%zu f%u/%u", prefix.c_str(), count, before, field, type);
        switch (type) {
            case 0: { uint64_t v = read_varint(p, n, off); printf(" = %lld (raw %llu)\n", (long long)zigzag_decode(v), (unsigned long long)v); break; }
            case 1: { uint64_t v = read_varint(p, n, off); printf(" = %llu\n", (unsigned long long)v); break; }
            case 2: { if (off + 4 > n) return false; uint32_t bits = get_u32(p + off); float f; memcpy(&f, &bits, 4); off += 4; printf(" = %g\n", (double)f); break; }
            case 3: { uint64_t v = read_varint(p, n, off); printf(" = %llu\n", (unsigned long long)v); break; }
            case 4: {
                uint64_t len = read_varint(p, n, off);
                if (off + len > n) return false;
                bool printable = len > 0;
                for (uint64_t i = 0; i < len; i++) { unsigned char c = p[off + i]; if (c < 0x20 || c > 0x7E) { printable = false; break; } }
                if (printable) {
                    printf(" = \"%.*s\"\n", (int)len, (const char*)(p + off));
                    off += (size_t)len;
                } else {
                    printf(" = nested[%llu] {\n", (unsigned long long)len);
                    size_t sub = 0;
                    if (!dump_reflection(p + off, (size_t)len, prefix + "    ", depth + 1, &sub))
                        printf("%s    <nested parse failed>\n", prefix.c_str());
                    printf("%s}\n", prefix.c_str());
                    off += (size_t)len;
                }
                break;
            }
            case 6: { uint64_t len = read_varint(p, n, off); if (off + len > n) return false; printf(" = bytes[%llu]\n", (unsigned long long)len); size_t cap = len < 96 ? (size_t)len : 96; dump_hex(p + off, cap, prefix.c_str(), cap); off += (size_t)len; break; }
            case 7: { if (off + 8 > n) return false; uint64_t bits = 0; for (int i = 0; i < 8; i++) bits |= (uint64_t)p[off + i] << (8 * i); off += 8; printf(" = %llu (%g)\n", (unsigned long long)bits, (double)bits); break; }
            default: printf(" = <unknown type %u>\n", type); return false;
        }
        count++;
        if (count > 100000) return false;
    }
    if (consumed) *consumed = off;
    return true;
}

// Decode a decompressed PlayerFullData body (no zlib here - caller decompresses).
inline void dump_player_full_body(const uint8_t* p, size_t n, const char* prefix) {
    size_t h = 0;
    uint8_t version = (h < n) ? p[h++] : 0;
    uint64_t eid_raw = read_varint(p, n, h);
    int64_t eid = zigzag_decode(eid_raw);
    uint64_t body_len = read_varint(p, n, h);
    printf("%sheader: version=%u entity_id=%lld body_len=%llu\n", prefix, version, (long long)eid, (unsigned long long)body_len);
    size_t avail = n > h ? n - h : 0;
    size_t blen = (body_len < avail) ? (size_t)body_len : avail;
    if (blen < body_len) printf("%s  (truncated sample: only %zu of %llu body bytes available)\n", prefix, blen, (unsigned long long)body_len);
    size_t off = 0;
    int s = 0;
    while (off < blen) {
        printf("%sstruct[%d] @ +%zu:\n", prefix, s, off);
        size_t used = 0;
        if (!dump_reflection(p + h + off, blen - off, std::string(prefix) + "  ", 0, &used)) {
            printf("%s  (parse failed at +%zu)\n", prefix, off);
            dump_hex(p + h + off, blen - off, prefix, 256);
            break;
        }
        if (used == 0) break;
        off += used;
        s++;
    }
}

}
