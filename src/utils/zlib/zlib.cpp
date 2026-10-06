// File: utils/zlib/zlib.cpp
// Author: NepoOwen
// Last Modified: 2026-10-01
#include "zlib.hpp"
#include <cstring>

namespace zlib {
namespace {

// LSB-first bit writer (DEFLATE packs bits into bytes starting at bit 0).
struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t acc = 0;
    int nbits = 0;

    void put(uint32_t bits, int n) {
        acc |= (bits & ((1u << n) - 1)) << nbits;
        nbits += n;
        while (nbits >= 8) {
            out.push_back((uint8_t)(acc & 0xff));
            acc >>= 8;
            nbits -= 8;
        }
    }
    void align() {
        if (nbits) { out.push_back((uint8_t)(acc & 0xff)); acc = 0; nbits = 0; }
    }
};

constexpr uint16_t DF_LEN_BASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
constexpr uint8_t  DF_LEN_EXTRA[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
constexpr uint16_t DF_DIST_BASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
constexpr uint8_t  DF_DIST_EXTRA[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
constexpr uint8_t  DF_CLEN_ORDER[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

// Huffman codes are transmitted MSB-first; the writer is LSB-first, so code
// bits are reversed before being emitted.
uint32_t rev(uint32_t v, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

void write_code(BitWriter& w, uint16_t code, int len) {
    if (len > 0) w.put(rev(code, len), len);
}

uint16_t len_sym(uint32_t len, uint8_t& extra) {
    for (int i = 0; i < 29; i++)
        if (len <= (uint32_t)DF_LEN_BASE[i] + ((1u << DF_LEN_EXTRA[i]) - 1)) { extra = DF_LEN_EXTRA[i]; return 257 + i; }
    extra = 0; return 285;
}

uint16_t dist_sym(uint32_t dist, uint8_t& extra) {
    for (int i = 0; i < 30; i++)
        if (dist <= (uint32_t)DF_DIST_BASE[i] + ((1u << DF_DIST_EXTRA[i]) - 1)) { extra = DF_DIST_EXTRA[i]; return i; }
    extra = 0; return 29;
}

// Build Huffman code lengths from symbol frequencies (greedy tree, O(n^2)).
void huff_lengths(const uint32_t* freq, int n, int maxbits, uint8_t* lens) {
    memset(lens, 0, n);
    std::vector<int> parent(2 * n, -1);
    std::vector<uint32_t> w(2 * n, 0);
    std::vector<int> active;
    for (int i = 0; i < n; i++)
        if (freq[i] > 0) { w[i] = freq[i]; active.push_back(i); }

    if (active.size() == 1) { lens[active[0]] = 1; return; }
    if (active.empty()) return;

    int nnodes = n;
    while (active.size() > 1) {
        int a = 0, b = 1;
        if (w[active[a]] > w[active[b]]) { int t = a; a = b; b = t; }
        for (size_t i = 2; i < active.size(); i++) {
            if (w[active[i]] < w[active[a]]) { b = a; a = (int)i; }
            else if (w[active[i]] < w[active[b]]) b = (int)i;
        }
        int na = active[a], nb = active[b];
        if (a > b) { active.erase(active.begin() + a); active.erase(active.begin() + b); }
        else { active.erase(active.begin() + b); active.erase(active.begin() + a); }
        int p = nnodes++;
        parent[na] = p; parent[nb] = p;
        w[p] = w[na] + w[nb];
        active.push_back(p);
    }
    for (int i = 0; i < n; i++) {
        if (freq[i] == 0) continue;
        int d = 0;
        for (int p = parent[i]; p != -1; p = parent[p]) d++;
        lens[i] = (uint8_t)(d > maxbits ? maxbits : d);
    }
}

// Assign canonical DEFLATE codes for the given lengths.
void canon_codes(const uint8_t* lens, int n, uint16_t* codes) {
    memset(codes, 0, n * sizeof(uint16_t));
    int bl[16] = {0};
    for (int i = 0; i < n; i++) if (lens[i]) bl[lens[i]]++;
    uint16_t next[16] = {0};
    uint16_t code = 0;
    for (int bits = 1; bits <= 15; bits++) {
        code = (uint16_t)((code + bl[bits - 1]) << 1);
        next[bits] = code;
    }
    for (int i = 0; i < n; i++)
        if (lens[i]) codes[i] = next[lens[i]]++;
}

// Emit code lengths using the code-length code (with 16/17/18 RLE).
void write_lengths(BitWriter& w, const uint8_t* lens, int n,
                   const uint16_t* cl_codes, const uint8_t* cl_lens) {
    int i = 0;
    while (i < n) {
        uint8_t v = lens[i];
        int run = 1;
        while (i + run < n && lens[i + run] == v) run++;
        i += run;

        if (v == 0) {
            while (run >= 11) {
                int take = run > 138 ? 138 : run;
                write_code(w, cl_codes[18], cl_lens[18]);
                w.put((uint32_t)(take - 11), 7);
                run -= take;
            }
            if (run >= 3) {
                write_code(w, cl_codes[17], cl_lens[17]);
                w.put((uint32_t)(run - 3), 3);
                run = 0;
            }
            for (int k = 0; k < run; k++) write_code(w, cl_codes[0], cl_lens[0]);
        } else {
            write_code(w, cl_codes[v], cl_lens[v]);
            run--;
            while (run >= 3) {
                int take = run > 6 ? 6 : run;
                write_code(w, cl_codes[16], cl_lens[16]);
                w.put((uint32_t)(take - 3), 2);
                run -= take;
            }
            for (int k = 0; k < run; k++) write_code(w, cl_codes[v], cl_lens[v]);
        }
    }
}

// Counts how many times each code-length symbol (0..18) is actually emitted
// by write_lengths after 16/17/18 run-length encoding (mirrors zlib's
// scan_tree; counting raw length values directly would skew the frequencies).
void count_cl_freq(const uint8_t* lens, int n, uint32_t clfreq[19]) {
    int i = 0;
    while (i < n) {
        uint8_t v = lens[i];
        int run = 1;
        while (i + run < n && lens[i + run] == v) run++;
        i += run;

        if (v == 0) {
            while (run >= 11) {
                int take = run > 138 ? 138 : run;
                clfreq[18]++;
                run -= take;
            }
            if (run >= 3) { clfreq[17]++; run = 0; }
            clfreq[0] += run;
        } else {
            clfreq[v]++;
            run--;
            while (run >= 3) {
                int take = run > 6 ? 6 : run;
                clfreq[16]++;
                run -= take;
            }
            clfreq[v] += run;
        }
    }
}

} // namespace

std::vector<uint8_t> deflate(const uint8_t* data, size_t len) {
    // ---- LZ77 tokenization ----
    struct Token { uint16_t sym, dist, leval, deval; uint8_t le, de; bool match; };
    std::vector<Token> toks;
    toks.reserve(len);

    {
        const size_t HSIZE = 1 << 15;
        std::vector<int32_t> head(HSIZE, -1);
        std::vector<int32_t> prev(len, -1);

        auto hash3 = [&](size_t i) -> uint32_t {
            return ((uint32_t)data[i] << 10) ^ ((uint32_t)data[i + 1] << 5) ^ (uint32_t)data[i + 2];
        };
        auto insert = [&](size_t i) {
            uint32_t h = hash3(i) & (HSIZE - 1);
            prev[i] = head[h];
            head[h] = (int32_t)i;
        };

        size_t pos = 0;
        while (pos < len) {
            size_t best_len = 0, best_dist = 0;
            if (pos + 2 < len) {
                uint32_t h = hash3(pos) & (HSIZE - 1);
                int32_t cand = head[h];
                size_t chain = 0;
                while (cand >= 0 && chain < 64 && (size_t)(pos - (size_t)cand) <= 32768) {
                    size_t maxl = len - pos;
                    if (maxl > 258) maxl = 258;
                    size_t l = 0;
                    while (l < maxl && data[cand + l] == data[pos + l]) l++;
                    if (l > best_len) { best_len = l; best_dist = (size_t)(pos - (size_t)cand); }
                    if (l >= maxl) break;
                    cand = prev[cand];
                    chain++;
                }
            }

            if (best_len >= 3) {
                uint8_t le, de;
                uint16_t lc = len_sym((uint32_t)best_len, le);
                uint16_t dc = dist_sym((uint32_t)best_dist, de);
                Token t;
                t.match = true;
                t.sym = lc; t.le = le; t.leval = (uint16_t)(best_len - DF_LEN_BASE[lc - 257]);
                t.dist = dc; t.de = de; t.deval = (uint16_t)(best_dist - DF_DIST_BASE[dc]);
                toks.push_back(t);
                for (size_t i = pos; i < pos + best_len && i + 2 < len; i++) insert(i);
                pos += best_len;
            } else {
                Token t;
                t.match = false;
                t.sym = data[pos]; t.le = t.de = 0; t.leval = t.deval = 0; t.dist = 0;
                toks.push_back(t);
                if (pos + 2 < len) insert(pos);
                pos++;
            }
        }
    }

    // ---- frequencies ----
    uint32_t lfreq[286] = {0};
    uint32_t dfreq[30] = {0};
    for (const Token& t : toks) {
        if (t.match) { lfreq[t.sym]++; dfreq[t.dist]++; }
        else lfreq[t.sym]++;
    }
    lfreq[256] = 1; // EOB

    // ---- Huffman lengths + canonical codes ----
    uint8_t llens[286] = {0}, dlens[30] = {0};
    huff_lengths(lfreq, 286, 15, llens);
    huff_lengths(dfreq, 30, 15, dlens);

    int nlit = 257;
    for (int i = 285; i >= 257; i--) if (llens[i]) { nlit = i + 1; break; }

    // A single-symbol distance code (length 1) is an incomplete Huffman
    // code; pad it with a dummy 1-bit code so the tree is valid.
    {
        int used = 0;
        for (int i = 0; i < 30; i++) if (dlens[i]) used++;
        if (used == 1) {
            for (int i = 0; i < 30; i++) if (dlens[i] == 0) { dlens[i] = 1; break; }
        }
    }
    int ndist = 1;
    for (int i = 29; i >= 1; i--) if (dlens[i]) { ndist = i + 1; break; }

    uint16_t lcodes[286] = {0}, dcodes[30] = {0};
    canon_codes(llens, 286, lcodes);
    canon_codes(dlens, 30, dcodes);

    // ---- code-length code ----
    uint32_t clfreq[19] = {0};
    count_cl_freq(llens, nlit, clfreq);
    count_cl_freq(dlens, ndist, clfreq);

    uint8_t cl_lens[19] = {0};
    huff_lengths(clfreq, 19, 7, cl_lens);
    int nclen = 4;
    for (int i = 18; i >= 4; i--) if (cl_lens[DF_CLEN_ORDER[i]]) { nclen = i + 1; break; }
    uint16_t cl_codes[19] = {0};
    canon_codes(cl_lens, 19, cl_codes);

    // ---- emit ----
    std::vector<uint8_t> out;
    out.reserve(len / 2 + 64);
    out.push_back(0x78);
    out.push_back(0x01);
    {
        BitWriter w{out};
        w.put(0, 1);  // BFINAL = 0
        w.put(2, 2);  // BTYPE = 10 (dynamic Huffman)
        w.put((uint32_t)(nlit - 257), 5);
        w.put((uint32_t)(ndist - 1), 5);
        w.put((uint32_t)(nclen - 4), 4);
        for (int i = 0; i < nclen; i++)
            w.put(cl_lens[DF_CLEN_ORDER[i]], 3);
        write_lengths(w, llens, nlit, cl_codes, cl_lens);
        write_lengths(w, dlens, ndist, cl_codes, cl_lens);

        for (const Token& t : toks) {
            write_code(w, lcodes[t.sym], llens[t.sym]);
            if (t.match) {
                if (t.le) w.put(t.leval, t.le);
                write_code(w, dcodes[t.dist], dlens[t.dist]);
                if (t.de) w.put(t.deval, t.de);
            }
        }
        write_code(w, lcodes[256], llens[256]); // EOB

        // Z_SYNC_FLUSH: empty stored block right after EOB, byte-aligned,
        // LEN=0 / NLEN=0xFFFF -- matches zlib's exact sync-flush format so
        // the stream stays open for the next message on this connection.
        w.put(0, 1); // BFINAL = 0
        w.put(0, 2); // BTYPE = 00 (stored)
        w.align();
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0xff);
        out.push_back(0xff);
    }

    return out;
}

// ---- clean-room one-shot inflate ----

namespace {

struct reader {
    const uint8_t* p;
    size_t len, pos;
    uint32_t acc;
    int nbits;

    int get(int n) {
        while (nbits < n) {
            if (pos >= len) return -1;
            acc |= (uint32_t)p[pos++] << nbits;
            nbits += 8;
        }
        int v = acc & ((1u << n) - 1);
        acc >>= n;
        nbits -= n;
        return v;
    }

    void align() {
        int r = nbits & 7;
        if (r) { acc >>= r; nbits -= r; }
    }

    size_t byte_pos() const { return pos - (nbits / 8); }
};

constexpr uint16_t LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
constexpr uint8_t LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
constexpr uint16_t DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
constexpr uint8_t DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
constexpr uint8_t CLEN_ORDER[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

struct huff {
    uint16_t count[16];
    uint16_t symbol[288];
};

int build_huff(huff& h, const uint8_t* lens, int n) {
    std::memset(&h, 0, sizeof(h));
    for (int i = 0; i < n; i++) {
        if (lens[i] > 15) return -1;
        h.count[lens[i]]++;
    }
    if (h.count[0] == n) return 0;
    uint16_t offs[16];
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h.count[i];
    for (int i = 0; i < n; i++)
        if (lens[i]) h.symbol[offs[lens[i]]++] = (uint16_t)i;
    return 0;
}

int decode_sym(reader& r, const huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int bit = r.get(1);
        if (bit < 0) return -1;
        code |= bit;
        int count = h.count[len];
        if (code - first < count) {
            if (index + (code - first) >= 288) return -2;
            return h.symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -3;
}

} // namespace

std::vector<uint8_t> inflate(const uint8_t* in, size_t in_len) {
    if (!in || in_len < 2) return {};
    if ((in[0] & 0x0f) != 8) return {};
    if ((uint32_t)((in[0] << 8) | in[1]) % 31 != 0) return {};
    int flg = in[1];
    if (flg & 0x20) return {}; // preset dictionary unsupported

    reader r{ in + 2, in_len - 2, 0, 0, 0 };
    std::vector<uint8_t> out;
    huff lenc, distc;

    for (;;) {
        int bfinal = r.get(1);
        int btype = r.get(2);
        if (bfinal < 0 || btype < 0) return {};

        if (btype == 0) {
            r.align();
            size_t bp = r.byte_pos();
            if (bp + 4 > r.len) return {};
            uint32_t len = r.p[bp] | (r.p[bp + 1] << 8);
            uint32_t nlen = r.p[bp + 2] | (r.p[bp + 3] << 8);
            if ((len ^ 0xffff) != nlen) return {};
            r.pos = bp + 4;
            r.acc = 0;
            r.nbits = 0;
            if (r.pos + len > r.len) return {};
            size_t old = out.size();
            out.resize(old + len);
            std::memcpy(out.data() + old, r.p + r.pos, len);
            r.pos += len;
            if (len == 0 && !bfinal) break; // Z_SYNC_FLUSH terminator
            if (bfinal) break;
            continue;
        } else if (btype == 1) {
            uint8_t lens[288];
            for (int i = 0; i < 144; i++) lens[i] = 8;
            for (int i = 144; i < 256; i++) lens[i] = 9;
            for (int i = 256; i < 280; i++) lens[i] = 7;
            for (int i = 280; i < 288; i++) lens[i] = 8;
            uint8_t dlens[30];
            for (int i = 0; i < 30; i++) dlens[i] = 5;
            build_huff(lenc, lens, 288);
            build_huff(distc, dlens, 30);
        } else if (btype == 2) {
            int hlit = r.get(5), hdist = r.get(5), hclen = r.get(4);
            if (hlit < 0 || hdist < 0 || hclen < 0) return {};
            hlit += 257; hdist += 1; hclen += 4;
            if (hclen > 19 || hlit > 286 || hdist > 30) return {};
            uint8_t clens[19] = {0};
            for (int i = 0; i < hclen; i++) {
                int v = r.get(3);
                if (v < 0) return {};
                clens[CLEN_ORDER[i]] = (uint8_t)v;
            }
            huff clh;
            if (build_huff(clh, clens, 19)) return {};
            uint8_t lens[286 + 30] = {0};
            int total = hlit + hdist;
            int i = 0;
            while (i < total) {
                int s = decode_sym(r, clh);
                if (s < 0) return {};
                if (s < 16) {
                    lens[i++] = (uint8_t)s;
                } else if (s == 16) {
                    if (i == 0) return {};
                    int rep = 3 + r.get(2);
                    uint8_t prev = lens[i - 1];
                    for (int k = 0; k < rep && i < total; k++) lens[i++] = prev;
                } else if (s == 17) {
                    int rep = 3 + r.get(3);
                    for (int k = 0; k < rep && i < total; k++) lens[i++] = 0;
                } else if (s == 18) {
                    int rep = 11 + r.get(7);
                    for (int k = 0; k < rep && i < total; k++) lens[i++] = 0;
                } else return {};
            }
            if (build_huff(lenc, lens, hlit)) return {};
            if (build_huff(distc, lens + hlit, hdist)) return {};
        } else {
            return {};
        }

        for (;;) {
            int sym = decode_sym(r, lenc);
            if (sym < 0) return {};
            if (sym < 256) {
                out.push_back((uint8_t)sym);
            } else if (sym == 256) {
                break;
            } else {
                if (sym > 285) return {};
                int li = sym - 257;
                if (li >= 29) return {};
                uint32_t len = LEN_BASE[li];
                if (LEN_EXTRA[li]) {
                    int e = r.get(LEN_EXTRA[li]);
                    if (e < 0) return {};
                    len += e;
                }
                int dsym = decode_sym(r, distc);
                if (dsym < 0 || dsym >= 30) return {};
                uint32_t dist = DIST_BASE[dsym];
                if (DIST_EXTRA[dsym]) {
                    int e = r.get(DIST_EXTRA[dsym]);
                    if (e < 0) return {};
                    dist += e;
                }
                if (dist > out.size()) return {};
                size_t old = out.size();
                out.resize(old + len);
                for (uint32_t k = 0; k < len; k++)
                    out[old + k] = out[old + k - dist];
            }
        }
        if (bfinal) break;
    }
    return out;
}

}
