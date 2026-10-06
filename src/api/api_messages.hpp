// File: api/api_messages.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace api {

// One entry of a NetEvent's field[1] param list. Mirrors the typed encoding
// proto::parse_event_params() reads back: a [zigzag varint: kind] tag
// followed by the value.
struct EventParam {
    enum class Kind { Int, UInt, Int64, Str, Float, Double } kind;
    int64_t i = 0;
    uint64_t u = 0;
    std::string s;
    float f = 0;
    double d = 0;
};

inline EventParam ep_int(int64_t v)        { EventParam p; p.kind = EventParam::Kind::Int;    p.i = v; return p; }
inline EventParam ep_uint(uint64_t v)      { EventParam p; p.kind = EventParam::Kind::UInt;   p.u = v; return p; }
inline EventParam ep_int64(int64_t v)      { EventParam p; p.kind = EventParam::Kind::Int64;  p.i = v; return p; }
inline EventParam ep_str(std::string v)    { EventParam p; p.kind = EventParam::Kind::Str;    p.s = std::move(v); return p; }
inline EventParam ep_float(float v)        { EventParam p; p.kind = EventParam::Kind::Float;  p.f = v; return p; }
inline EventParam ep_double(double v)      { EventParam p; p.kind = EventParam::Kind::Double; p.d = v; return p; }

// Builds the plaintext [total][type][fields] NetEvent message (unencrypted --
// the caller sends it through WorldHandler::send_game()). field4 is omitted
// when 0, matching the handful of real captures seen so far.
std::vector<uint8_t> build_net_event_message(uint64_t entity_id, const std::string& event_name,
                                              const std::vector<EventParam>& params, int64_t field4);

// One entry of a SessionData's field[1] nested blob. Unlike EventParam
// these are real field-numbered tags, not kind-tagged -- Raw exists for
// wire types outside 0/1/2/3/6 (e.g. wire=23) seen in real captures.
struct SessionField {
    enum class Kind { Int, Str, Raw } kind;
    uint32_t field = 0;
    int64_t ival = 0;
    std::string sval;
    uint32_t wire = 0;
};
inline SessionField sdf_int(uint32_t field, int64_t v)             { SessionField f; f.kind = SessionField::Kind::Int; f.field = field; f.ival = v; return f; }
inline SessionField sdf_str(uint32_t field, std::string v)         { SessionField f; f.kind = SessionField::Kind::Str; f.field = field; f.sval = std::move(v); return f; }
inline SessionField sdf_raw(uint32_t field, uint32_t wire, uint64_t v) { SessionField f; f.kind = SessionField::Kind::Raw; f.field = field; f.wire = wire; f.ival = (int64_t)v; return f; }

// Builds the plaintext [total][type][fields] SessionData message (sent
// through AuthHandler::send_game(), not the world connection). field[1] is
// the nested blob built from `params`; field2 is omitted when 0, same as
// field4 in build_net_event_message.
std::vector<uint8_t> build_session_data_message(const std::vector<SessionField>& params, int64_t field2, bool emit_field0 = false);

}
