// File: utils/frame/frame.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "../crypto/crypto.hpp"
#include "../zlib/inflate_stream.hpp"

namespace frame {

// Wire format of every chunk on a connection:
//   [u32 len][u8 flag][len-byte ciphertext]
// Ciphertext is AES-128-CFB, chaining continuously across all chunks in each
// direction. flag == 1 means the decrypted payload is zlib-compressed
// (part of that direction's one continuous deflate stream).

struct Segment {
    uint32_t len = 0;              // ciphertext length
    uint8_t flag = 0;               // 1 = compressed
    std::vector<uint8_t> raw;       // raw ciphertext as received
    std::vector<uint8_t> payload;   // decrypted (+ inflated) plaintext
};

// Per-connection state: the CFB stream and the continuous inflate window
// both belong to one physical socket. Two connections open at once must
// each get their own Channel and never share one.
struct Channel {
    crypto::Stream stream;
    zmin::InflateStream inflate;
};

// Game channel message: one segment, flag=0.
std::vector<uint8_t> build_game(Channel& ch, const uint8_t* payload, size_t len);

// Login channel message: plaintext is [16-byte header][payload], sent as two
// segments -- the header (flag 0) followed by the compressed content (flag 1).
std::vector<uint8_t> build_login(Channel& ch, const uint8_t* payload, size_t len);

// Login channel message with a raw (uncompressed) payload; flags inverted
// (header=1, content=0).
std::vector<uint8_t> build_login_raw(Channel& ch, const uint8_t* payload, size_t len);

// Parses one segment from buf. Returns bytes consumed, or 0 if incomplete.
size_t parse_segment(Channel& ch, const uint8_t* buf, size_t len, Segment& out);

// Pads to a multiple of 16 with [zeros][N] where N is the padding length.
// Always adds padding, even when already block-aligned.
std::vector<uint8_t> pad16(const std::vector<uint8_t>& data);

// Removes the trailing [zeros][N] padding.
std::vector<uint8_t> unpad16(const std::vector<uint8_t>& data);

}
