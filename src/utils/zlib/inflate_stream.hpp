// File: utils/zlib/inflate_stream.hpp
// Author: NepoOwen
// Last Modified: 2026-10-01
//
// Minimal clean-room DEFLATE (zlib header) decompressor with persistent
// cross-frame state. The game runs one continuous zlib stream per connection
// per direction (Z_SYNC_FLUSH after each message, never finalized), so the
// LZ77 window carries over across every flag==1 frame -- a one-shot inflate
// can't decode a frame whose back-references point into an earlier frame's
// output. This resumes decoding where the previous frame's flush left off.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace zmin {

// LSB-first bit reader, as DEFLATE requires.
struct reader {
    const unsigned char* p;
    size_t nbytes;
    size_t pos;
    unsigned int bits;
    int nbits;

    void init(const unsigned char* src, size_t len) { p = src; nbytes = len; pos = 0; bits = 0; nbits = 0; }

    unsigned int get(int n) {
        while (nbits < n) {
            if (pos >= nbytes) { nbits = 0; return 0; }
            bits |= (unsigned int)p[pos++] << nbits;
            nbits += 8;
        }
        unsigned int v = bits & ((1u << n) - 1u);
        bits >>= n;
        nbits -= n;
        return v;
    }
    void align() { bits >>= (nbits & 7); nbits &= ~7; }
    size_t byte_pos() const { return pos - (nbits >> 3); }
};

static const unsigned short LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const unsigned short LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const unsigned short DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const unsigned short DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const unsigned char CLEN_ORDER[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

// Canonical Huffman table (puff.c-style).
struct huff {
    int count[16];
    int symbol[288];
};

static bool build_huff(huff& h, const unsigned char* lens, int n) {
    memset(h.count, 0, sizeof(h.count));
    for (int i = 0; i < n; i++) {
        if (lens[i] > 15) return false;
        h.count[lens[i]]++;
    }
    h.count[0] = 0;

    int offs[16]; offs[0] = 0;
    for (int i = 1; i < 16; i++) offs[i] = offs[i - 1] + h.count[i - 1];
    for (int i = 0; i < n; i++)
        if (lens[i]) h.symbol[offs[lens[i]]++] = i;
    return true;
}

static int decode_sym(reader& r, const huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= (int)r.get(1);
        int cnt = h.count[len];
        if (code - first < cnt)
            return h.symbol[index + (code - first)];
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

// Persistent, cross-message DEFLATE stream for one connection direction.
struct InflateStream {
    bool started = false;             // zlib header already consumed?
    std::vector<unsigned char> win;   // full decompressed history (= the LZ77 window)
};

// Decodes one frame's plaintext against the persistent stream `st`,
// appending newly-decoded bytes to `st.win`. Returns the number of new bytes
// (0 is a valid "no output yet" result), or SIZE_MAX on a decode error.
static size_t inflate_stream(InflateStream& st, const unsigned char* in, size_t in_len) {
    constexpr size_t ERR = (size_t)-1;
    size_t p = 0;
    if (!st.started) {
        if (in_len < 2) return ERR;
        unsigned char cmf = in[0], flg = in[1];
        if ((cmf & 0x0F) != 8) return ERR;
        if ((((unsigned)cmf << 8) | flg) % 31 != 0) return ERR;
        p = 2;
        if (flg & 0x20) { // FDICT, unsupported
            if (in_len < p + 4) return ERR;
            p += 4;
        }
        st.started = true;
    }

    reader r; r.init(in + p, in_len - p);
    size_t start_len = st.win.size();

    for (;;) {
        unsigned bfinal = r.get(1);
        unsigned btype = r.get(2);

        if (btype == 0) {
            r.align();
            size_t by = r.byte_pos();
            if (by + 4 > r.nbytes) return ERR;
            unsigned len = r.p[by] | (r.p[by + 1] << 8);
            unsigned nlen = r.p[by + 2] | (r.p[by + 3] << 8);
            if (((len ^ nlen) & 0xFFFF) != 0xFFFF) return ERR;
            by += 4;
            if (by + len > r.nbytes) return ERR;
            size_t o = st.win.size();
            st.win.resize(o + len);
            memcpy(st.win.data() + o, r.p + by, len);
            r.pos = by + len; r.nbits = 0; r.bits = 0;

            // Z_SYNC_FLUSH: empty, non-final stored block -- this message is
            // done but the stream stays open for the next frame.
            if (len == 0 && !bfinal) break;
        }
        else if (btype == 1 || btype == 2) {
            unsigned char lit_lens[320];
            unsigned char dist_lens[32];
            memset(lit_lens, 0, sizeof(lit_lens));
            memset(dist_lens, 0, sizeof(dist_lens));

            if (btype == 1) {
                for (int i = 0; i < 144; i++) lit_lens[i] = 8;
                for (int i = 144; i < 256; i++) lit_lens[i] = 9;
                for (int i = 256; i < 280; i++) lit_lens[i] = 7;
                for (int i = 280; i < 288; i++) lit_lens[i] = 8;
                memset(dist_lens, 5, 32);
            }
            else {
                int hlit = r.get(5) + 257;
                int hdist = r.get(5) + 1;
                int hclen = r.get(4) + 4;
                unsigned char cl_lens[19] = { 0 };
                for (int i = 0; i < hclen; i++) cl_lens[CLEN_ORDER[i]] = (unsigned char)r.get(3);

                huff clh;
                if (!build_huff(clh, cl_lens, 19)) return ERR;

                int total = hlit + hdist;
                int idx = 0;
                while (idx < total) {
                    int s = decode_sym(r, clh);
                    if (s < 0) return ERR;
                    if (s < 16) {
                        unsigned char v = (unsigned char)s;
                        if (idx < hlit) lit_lens[idx] = v; else dist_lens[idx - hlit] = v;
                        idx++;
                    }
                    else if (s == 16) {
                        if (idx == 0) return ERR;
                        unsigned char prev = (idx <= hlit) ? lit_lens[idx - 1] : dist_lens[idx - hlit - 1];
                        unsigned long rep = 3 + r.get(2);
                        for (unsigned long k = 0; k < rep && idx < total; k++, idx++) {
                            if (idx < hlit) lit_lens[idx] = prev; else dist_lens[idx - hlit] = prev;
                        }
                    }
                    else if (s == 17) {
                        unsigned long rep = 3 + r.get(3);
                        for (unsigned long k = 0; k < rep && idx < total; k++, idx++) {
                            if (idx < hlit) lit_lens[idx] = 0; else dist_lens[idx - hlit] = 0;
                        }
                    }
                    else { // s == 18
                        unsigned long rep = 11 + r.get(7);
                        for (unsigned long k = 0; k < rep && idx < total; k++, idx++) {
                            if (idx < hlit) lit_lens[idx] = 0; else dist_lens[idx - hlit] = 0;
                        }
                    }
                }
                if (idx != total) return ERR;
            }

            huff lh, dh;
            if (!build_huff(lh, lit_lens, 288)) return ERR;
            if (!build_huff(dh, dist_lens, 32)) return ERR;

            for (;;) {
                int sym = decode_sym(r, lh);
                if (sym < 0) return ERR;
                if (sym < 256) {
                    st.win.push_back((unsigned char)sym);
                }
                else if (sym == 256) {
                    break;
                }
                else {
                    int lc = sym - 257;
                    if (lc >= 29) return ERR;
                    unsigned length = LEN_BASE[lc] + r.get(LEN_EXTRA[lc]);
                    int dc = decode_sym(r, dh);
                    if (dc < 0) return ERR;
                    unsigned dist = DIST_BASE[dc] + r.get(DIST_EXTRA[dc]);
                    size_t o = st.win.size();
                    if (dist > o) return ERR;
                    st.win.resize(o + length);
                    unsigned char* buf = st.win.data();
                    for (unsigned k = 0; k < length; k++) buf[o + k] = buf[o + k - dist];
                }
            }
        }
        else {
            return ERR; // reserved btype
        }

        if (bfinal) { st.started = false; break; } // stream fully finished
    }

    return st.win.size() - start_len;
}

} // namespace zmin
