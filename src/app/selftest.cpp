// File: app/selftest.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "selftest.hpp"
#include "../utils/crypto/crypto.hpp"
#include "../utils/zlib/zlib.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

namespace app {
namespace {

bool crypto_selftest() {
    const uint8_t nonce[16] = { 0x3d,0xf2,0x32,0x53,0x6b,0xcb,0x15,0x18,0x16,0x4c,0x46,0x85,0x39,0x25,0x72,0xb8 };
    const uint8_t plain[16] = { 0x2d,0x31,0x30,0x33,0x2d,0x34,0x34,0x39,0x0e,0x01,0x31,0x11,0x0f,0x00,0x00,0x03 };
    const uint8_t expect[16] = { 0x43,0xd0,0xbc,0xbb,0x71,0xbe,0x7a,0x0b,0xbb,0x09,0x86,0x26,0xe2,0x3a,0xcc,0xfb };
    uint8_t out[16];
    crypto::cfb_encrypt(nonce, plain, out, 16);
    return std::memcmp(out, expect, 16) == 0;
}

bool deflate_selftest() {
    std::vector<uint8_t> data(1000);
    for (int i = 0; i < 1000; i++) data[i] = (uint8_t)(i * 7 + (i >> 3) + (i % 5) * 31);
    std::vector<uint8_t> comp = zlib::deflate(data.data(), data.size());
    std::vector<uint8_t> back = zlib::inflate(comp.data(), comp.size());
    return back == data;
}

} // namespace

bool run_selftests() {
    bool c = crypto_selftest();
    bool d = deflate_selftest();
    printf(c ? "[+] crypto OK\n" : "[!] crypto FAILED\n");
    printf(d ? "[+] deflate round-trip OK\n" : "[!] deflate round-trip FAILED\n");
    return c && d;
}

}
