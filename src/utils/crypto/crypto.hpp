// File: utils/crypto/crypto.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace crypto {

// Fixed game-channel AES key (recovered from the binary).
inline constexpr uint8_t KEY[16] = {
    0xcb, 0xf2, 0xfc, 0x05, 0x34, 0x48, 0x4d, 0x93,
    0xac, 0x55, 0xd4, 0x8b, 0xed, 0x1c, 0x1f, 0x00
};

// Fixed base IV (g_CipherIV) used to seed every game-channel CFB stream.
inline constexpr uint8_t FIXED_IV[16] = {
    0x9a, 0xa9, 0xef, 0xf5, 0x4c, 0x94, 0x43, 0xe5,
    0x9e, 0xed, 0x55, 0x3f, 0x12, 0x00, 0xc1, 0x00
};

// AES-128-CFB128 with an explicit IV, one-shot.
void cfb_encrypt(const uint8_t* iv, const uint8_t* plain, uint8_t* cipher, size_t len);
void cfb_decrypt(const uint8_t* iv, const uint8_t* cipher, uint8_t* plain, size_t len);

// Continuous per-direction CFB stream, seeded with FIXED_IV. The feedback
// register carries across every packet sent/received on the connection.
class Stream {
public:
    Stream() { reset(); }
    void reset();
    std::vector<uint8_t> encrypt(const uint8_t* plain, size_t len);
    std::vector<uint8_t> decrypt(const uint8_t* cipher, size_t len);
private:
    uint8_t send_fb_[16];
    uint8_t recv_fb_[16];
};

}
