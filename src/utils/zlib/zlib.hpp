// File: utils/zlib/zlib.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace zlib {

// zlib (RFC 1950) compress using dynamic Huffman deflate blocks, terminated
// with a Z_SYNC_FLUSH marker (matches the game's continuous per-connection
// stream instead of a one-shot finished stream).
std::vector<uint8_t> deflate(const uint8_t* data, size_t len);

// zlib (RFC 1950) one-shot inflate. Returns empty on failure. For the
// continuous per-connection stream, see zmin::InflateStream instead.
std::vector<uint8_t> inflate(const uint8_t* data, size_t len);

}
