// File: utils/proto/type_names.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
//
// Every message type id the client knows about, defined once. Used both as
// the constants auth/world code switches on and by proto::type_name() for
// logging, so a hex value is never duplicated between the two.
//
// Prefixed by socket (NET_ = any socket, AUTH_ = auth socket, WORLD_ = world
// + post-login sockets) because plain #define has no namespace -- the auth
// and world sockets each have their own distinct "Login"/"HandshakeAck"
// message with a different id, so the unqualified names would collide.
#pragma once

// Background chatter seen on every connection.
#define NET_KEEPALIVE_TYPE 0x024ECA24u
#define NET_NM_TYPE        0x0ADD4D4Eu

// Auth-socket message types.
#define AUTH_HANDSHAKE_TYPE     0x0609C1E0u
#define AUTH_LOGIN_TYPE         0x06BC3345u
#define AUTH_HANDSHAKE_ACK_TYPE 0x0B915893u
#define AUTH_RESP1_TYPE         0x0840F2E7u // carries a token WorldLogin must echo
#define AUTH_RESP2_TYPE         0x07C670F6u // carries the 10-byte followup token
#define AUTH_SESSION_DATA_TYPE  0x01FD6BCCu
#define AUTH_USER_ACK_REQ_TYPE  0x087ED254u // unused by this client, kept for log display parity
#define AUTH_QUEUE_TYPE         0x0A2F0061u // unused by this client, kept for log display parity
#define AUTH_VERSION_ERROR_TYPE    0x0DFA778Du // unused by this client, kept for log display parity
#define AUTH_RECV_LOGIN_TYPE       0x005BE008u // unused by this client, kept for log display parity
#define AUTH_VERSION_REJECTED_TYPE 0x0C66FA4Fu // unused by this client, kept for log display parity

// World-socket message types (also used by the post-login connection).
#define WORLD_LOGIN_TYPE              0x010C177Bu
#define WORLD_HANDSHAKE_TYPE          0x0644CC1Eu
#define WORLD_HANDSHAKE_ACK_TYPE      0x0C66AF4Fu
#define WORLD_HANDSHAKE_DONE_TYPE     0x0825831Au
#define WORLD_PRE_CHALLENGE_TYPE      0x096A7293u // answered with a single empty frame
#define WORLD_XC_CHALLENGE_TYPE       0x02A2CA22u // anti-cheat challenge, answered via xem::solve()
#define WORLD_XC_RESPONSE_TYPE        0x01AAF303u
#define WORLD_LOGIN_FINISHED_TYPE     0x03596F29u
#define WORLD_POST_LOGIN_ACK_TYPE     0x05DC5CBFu // sent on the second (post-login) connection
#define WORLD_POST_LOGIN_ACK_ACK_TYPE 0x054AC0C3u
#define WORLD_CHANNEL_JOIN_TYPE       0x0F99AE77u
#define WORLD_CHANNEL_JOIN2_TYPE      0x0E7F02ACu
#define WORLD_DATA_TYPE               0x019F2382u
#define WORLD_READY_TYPE              0x01FD6115u
#define WORLD_MAP_DATA_TYPE           0x0744D098u // arrives on the post-login connection
#define WORLD_PACKET_DISPATCH_TYPE     0x01AE6A33u // unused by this client, kept for log display parity
#define WORLD_NET_EVENT_TYPE           0x06A9613Bu // game event: field[0]=entity id, field[1]=params, field[2]=name
#define WORLD_LOCAL_PLAYER_LOADED_TYPE 0x08CB1808u // sent once on world join: field[0]=local player's own entity id
#define WORLD_LEADERBOARD_ENTRIES_TYPE 0x0BCDAC2Cu
#define WORLD_MARKET_LISTINGS_TYPE     0x0D001259u
#define WORLD_SPAWN_ENTITY_TYPE        0x0C31A170u // spawn entity/effect: field[2]=type name, field[3..5]=x/y/z
#define WORLD_ENTITY_UPDATE_TYPE       0x0C814638u // field[0]=entity/container id, field[1]=serialized state
#define WORLD_LOCALIZED_NOTICE_TYPE    0x0C0F1766u // locale + key + resolved text (UI toast)
#define WORLD_POSITION_UPDATE_TYPE     0x0D5E6671u // high-frequency position tick (SEND+RECV)
#define WORLD_ROTATION_UPDATE_TYPE     0x0E0CB65Eu // high-frequency rotation/velocity tick (SEND+RECV)
#define WORLD_PLAYER_FULL_DATA_TYPE    0x0E3E4612u // joining player's full serialized state (zlib)
#define WORLD_ENTITY_POSITION_TYPE     0x0460DFBCu // entity id + x/y/z
