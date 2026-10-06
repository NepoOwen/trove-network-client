// File: api/api_messages.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "api_messages.hpp"
#include "../utils/proto/type_names.hpp"
#include <cstring>

namespace api {
namespace {

void put_varint(std::vector<uint8_t>& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back((uint8_t)((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back((uint8_t)v);
}

void put_zigzag(std::vector<uint8_t>& out, int64_t v) {
    put_varint(out, (uint64_t)((v << 1) ^ (v >> 63)));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i)));
}

// Same custom int field encoding every other module uses: 0/1 are tagged
// with no payload, negative values are zigzag varints, else a plain varint.
void put_int_field(std::vector<uint8_t>& out, uint32_t field, int64_t v) {
    if (v == 0) { put_varint(out, (uint64_t)field << 3); return; }
    if (v == 1) { put_varint(out, 1u | ((uint64_t)field << 3)); return; }
    if (v < 0) {
        put_varint(out, 2u | ((uint64_t)field << 3));
        put_zigzag(out, v);
        return;
    }
    put_varint(out, 3u | ((uint64_t)field << 3));
    put_varint(out, (uint64_t)v);
}

void put_raw_varint_field(std::vector<uint8_t>& out, uint32_t field, uint32_t wire, uint64_t v) {
    uint64_t tag = wire <= 6 ? (((uint64_t)field << 3) | wire) : (((uint64_t)field << 6) | wire);
    put_varint(out, tag);
    put_varint(out, v);
}

void put_bytes_field(std::vector<uint8_t>& out, uint32_t field, const std::vector<uint8_t>& bytes) {
    put_varint(out, 6u | ((uint64_t)field << 3));
    put_varint(out, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

void put_string_field(std::vector<uint8_t>& out, uint32_t field, const std::string& s) {
    put_varint(out, 6u | ((uint64_t)field << 3));
    put_varint(out, s.size());
    out.insert(out.end(), s.begin(), s.end());
}

// Inverse of proto::parse_event_params().
void encode_param(std::vector<uint8_t>& out, const EventParam& p) {
    switch (p.kind) {
        case EventParam::Kind::Int:
            put_zigzag(out, 0);
            put_zigzag(out, p.i);
            break;
        case EventParam::Kind::UInt:
            put_zigzag(out, 1);
            put_varint(out, p.u);
            break;
        case EventParam::Kind::Int64: {
            put_zigzag(out, 3);
            for (int i = 0; i < 8; i++) out.push_back((uint8_t)(p.i >> (8 * i)));
            break;
        }
        case EventParam::Kind::Str:
            put_zigzag(out, 4);
            put_varint(out, p.s.size());
            out.insert(out.end(), p.s.begin(), p.s.end());
            break;
        case EventParam::Kind::Float: {
            put_zigzag(out, 5);
            uint32_t bits; memcpy(&bits, &p.f, 4);
            put_u32(out, bits);
            break;
        }
        case EventParam::Kind::Double: {
            put_zigzag(out, 6);
            uint64_t bits; memcpy(&bits, &p.d, 8);
            put_u32(out, (uint32_t)bits);
            put_u32(out, (uint32_t)(bits >> 32));
            break;
        }
    }
}

void encode_session_field(std::vector<uint8_t>& out, const SessionField& f) {
    switch (f.kind) {
        case SessionField::Kind::Int: put_int_field(out, f.field, f.ival); break;
        case SessionField::Kind::Str: put_string_field(out, f.field, f.sval); break;
        case SessionField::Kind::Raw: put_raw_varint_field(out, f.field, f.wire, (uint64_t)f.ival); break;
    }
}

} // namespace

std::vector<uint8_t> build_net_event_message(uint64_t entity_id, const std::string& event_name,
                                              const std::vector<EventParam>& params, int64_t field4) {
    std::vector<uint8_t> param_blob;
    for (const EventParam& p : params) encode_param(param_blob, p);

    std::vector<uint8_t> body;
    put_int_field(body, 0, (int64_t)entity_id);
    put_bytes_field(body, 1, param_blob);
    put_string_field(body, 2, event_name);
    if (field4) put_int_field(body, 4, field4);
    body.push_back(0x0F);

    std::vector<uint8_t> msg;
    put_u32(msg, (uint32_t)(body.size() + 4));
    put_u32(msg, WORLD_NET_EVENT_TYPE);
    msg.insert(msg.end(), body.begin(), body.end());
    return msg;
}

std::vector<uint8_t> build_session_data_message(const std::vector<SessionField>& params, int64_t field2, bool emit_field0) {
    std::vector<uint8_t> nested;
    for (const SessionField& f : params) encode_session_field(nested, f);
    nested.push_back(0x0F);

    std::vector<uint8_t> body;
    if (emit_field0) put_int_field(body, 0, 0);
    put_bytes_field(body, 1, nested);
    if (field2) put_int_field(body, 2, field2);
    body.push_back(0x0F);

    std::vector<uint8_t> msg;
    put_u32(msg, (uint32_t)(body.size() + 4));
    put_u32(msg, AUTH_SESSION_DATA_TYPE);
    msg.insert(msg.end(), body.begin(), body.end());
    return msg;
}

}
