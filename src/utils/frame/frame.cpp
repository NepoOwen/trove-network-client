// File: utils/frame/frame.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "frame.hpp"

namespace frame {

static void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xff));
    out.push_back((uint8_t)((v >> 8) & 0xff));
    out.push_back((uint8_t)((v >> 16) & 0xff));
    out.push_back((uint8_t)((v >> 24) & 0xff));
}

static uint32_t get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static std::vector<uint8_t> segment(const uint8_t* ct, size_t len, uint8_t flag) {
    std::vector<uint8_t> out;
    out.reserve(5 + len);
    put_u32(out, (uint32_t)len);
    out.push_back(flag);
    out.insert(out.end(), ct, ct + len);
    return out;
}

std::vector<uint8_t> pad16(const std::vector<uint8_t>& data) {
    size_t n = 16 - (data.size() % 16);
    std::vector<uint8_t> out = data;
    out.insert(out.end(), n - 1, 0);
    out.push_back((uint8_t)n);
    return out;
}

std::vector<uint8_t> unpad16(const std::vector<uint8_t>& data) {
    if (data.empty()) return data;
    uint8_t n = data.back();
    if (n == 0 || n > 16 || n > data.size()) return data;
    for (size_t i = data.size() - n; i < data.size() - 1; i++)
        if (data[i] != 0) return data;
    return std::vector<uint8_t>(data.begin(), data.end() - n);
}

std::vector<uint8_t> build_game(Channel& ch, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> padded = pad16(std::vector<uint8_t>(payload, payload + len));
    std::vector<uint8_t> ct = ch.stream.encrypt(padded.data(), padded.size());
    return segment(ct.data(), ct.size(), 0);
}

std::vector<uint8_t> build_login(Channel& ch, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> comp = pad16(std::vector<uint8_t>(payload, payload + len));
    std::vector<uint8_t> plain(16, 0);
    plain[15] = 0x10;
    plain.insert(plain.end(), comp.begin(), comp.end());

    std::vector<uint8_t> ct = ch.stream.encrypt(plain.data(), plain.size());
    std::vector<uint8_t> out = segment(ct.data(), 16, 0);
    std::vector<uint8_t> rest = segment(ct.data() + 16, ct.size() - 16, 1);
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

std::vector<uint8_t> build_login_raw(Channel& ch, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> padded = pad16(std::vector<uint8_t>(payload, payload + len));
    std::vector<uint8_t> plain(16, 0);
    plain[15] = 0x10;
    plain.insert(plain.end(), padded.begin(), padded.end());

    std::vector<uint8_t> ct = ch.stream.encrypt(plain.data(), plain.size());
    std::vector<uint8_t> out = segment(ct.data(), 16, 1);
    std::vector<uint8_t> rest = segment(ct.data() + 16, ct.size() - 16, 0);
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

size_t parse_segment(Channel& ch, const uint8_t* buf, size_t len, Segment& out) {
    if (len < 5) return 0;
    uint32_t seg = get_u32(buf);
    uint8_t flag = buf[4];
    if (seg > 0x1000000 || (size_t)seg + 5 > len) return 0;

    std::vector<uint8_t> plain = ch.stream.decrypt(buf + 5, seg);
    if (flag == 1) {
        // Every flag==1 frame feeds the same continuous deflate stream, so a
        // one-shot inflate can't decode a frame whose back-references point
        // into an earlier frame's output.
        size_t before = ch.inflate.win.size();
        size_t added = zmin::inflate_stream(ch.inflate, plain.data(), plain.size());
        if (added == (size_t)-1) plain.clear();
        else plain.assign(ch.inflate.win.begin() + before, ch.inflate.win.end());
    }
    else plain = unpad16(plain);

    out.len = seg;
    out.flag = flag;
    out.raw.assign(buf + 5, buf + 5 + seg);
    out.payload = std::move(plain);
    return (size_t)seg + 5;
}

}
