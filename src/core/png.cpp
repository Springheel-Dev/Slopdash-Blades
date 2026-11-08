#include "core/png.h"

#include <string.h>

namespace png {

namespace {

typedef unsigned char u8;

unsigned Be32(const u8* p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

// ---------------------------------------------------------------------------
// DEFLATE (RFC 1951)
// ---------------------------------------------------------------------------

struct BitReader
{
    const u8* d;
    size_t size, pos;
    unsigned buf;
    int n;
    bool overrun;

    BitReader(const u8* data, size_t len) : d(data), size(len), pos(0), buf(0), n(0), overrun(false) {}

    unsigned Bits(int k)                    // LSB first
    {
        while (n < k)
        {
            unsigned b = 0;
            if (pos < size)
                b = d[pos];
            else
                overrun = true;
            pos++;
            buf |= b << n;
            n += 8;
        }
        unsigned v = buf & ((1u << k) - 1);
        buf >>= k;
        n -= k;
        return v;
    }
    void AlignByte() { buf = 0; n = 0; }
};

struct Huffman
{
    // Canonical decoding by code length: counts per length, symbols in order.
    unsigned short count[16];
    unsigned short symbol[320];

    bool Build(const u8* lengths, int n)
    {
        unsigned short offs[16];
        memset(count, 0, sizeof(count));
        for (int i = 0; i < n; i++)
            count[lengths[i]]++;
        count[0] = 0;
        int left = 1;
        for (int len = 1; len < 16; len++)
        {
            left <<= 1;
            left -= count[len];
            if (left < 0)
                return false;               // over-subscribed
        }
        offs[1] = 0;
        for (int len = 1; len < 15; len++)
            offs[len + 1] = (unsigned short)(offs[len] + count[len]);
        for (int i = 0; i < n; i++)
            if (lengths[i])
                symbol[offs[lengths[i]]++] = (unsigned short)i;
        return true;
    }

    int Decode(BitReader& b) const
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++)
        {
            code |= (int)b.Bits(1);
            int c = count[len];
            if (code - c < first)
                return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

const unsigned short kLenBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                      35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const u8 kLenExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const unsigned short kDistBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const u8 kDistExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool Codes(BitReader& b, const Huffman& lit, const Huffman& dist, std::vector<u8>& out, std::string& error)
{
    for (;;)
    {
        int sym = lit.Decode(b);
        if (sym < 0 || b.overrun)
        {
            error = "bad deflate data";
            return false;
        }
        if (sym < 256)
            out.push_back((u8)sym);
        else if (sym == 256)
            return true;
        else
        {
            sym -= 257;
            if (sym >= 29)
            {
                error = "bad deflate length";
                return false;
            }
            size_t len = kLenBase[sym] + b.Bits(kLenExtra[sym]);
            int d = dist.Decode(b);
            if (d < 0 || d >= 30)
            {
                error = "bad deflate distance";
                return false;
            }
            size_t back = kDistBase[d] + b.Bits(kDistExtra[d]);
            if (back > out.size())
            {
                error = "deflate distance too far";
                return false;
            }
            size_t from = out.size() - back;
            for (size_t i = 0; i < len; i++)
                out.push_back(out[from + i]);
        }
    }
}

// ---------------------------------------------------------------------------
// PNG
// ---------------------------------------------------------------------------

u8 Paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (u8)a;
    return (u8)(pb <= pc ? b : c);
}

// Undoes the per-row filters in place. `rows` rows of `stride` bytes, each
// preceded by its filter byte; `bpp` = bytes per complete pixel (at least 1).
bool Unfilter(u8* data, unsigned rows, size_t stride, unsigned bpp, std::string& error)
{
    u8* prev = NULL;
    for (unsigned y = 0; y < rows; y++)
    {
        u8* row = data + y * (stride + 1);
        u8 filter = row[0];
        u8* p = row + 1;
        for (size_t x = 0; x < stride; x++)
        {
            int a = x >= bpp ? p[x - bpp] : 0;
            int b = prev ? prev[x] : 0;
            int c = prev && x >= bpp ? prev[x - bpp] : 0;
            switch (filter)
            {
            case 0: break;
            case 1: p[x] = (u8)(p[x] + a); break;
            case 2: p[x] = (u8)(p[x] + b); break;
            case 3: p[x] = (u8)(p[x] + ((a + b) >> 1)); break;
            case 4: p[x] = (u8)(p[x] + Paeth(a, b, c)); break;
            default:
                error = "bad PNG filter";
                return false;
            }
        }
        prev = p;
    }
    return true;
}

struct Header
{
    unsigned width, height, depth, type;
    const u8* palette;
    unsigned paletteCount;
    u8 alphaPal[256];
    bool hasKey;
    unsigned keyR, keyG, keyB;              // tRNS colour key for grey / RGB
};

unsigned Channels(unsigned type)
{
    switch (type)
    {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    case 6: return 4;
    }
    return 0;
}

// Sample i of a row at the image's bit depth.
unsigned Sample(const u8* row, size_t i, unsigned depth)
{
    switch (depth)
    {
    case 8: return row[i];
    case 16: return ((unsigned)row[2 * i] << 8) | row[2 * i + 1];
    default:
    {
        size_t bit = i * depth;
        unsigned shift = 8 - depth - (unsigned)(bit & 7);
        return (row[bit >> 3] >> shift) & ((1u << depth) - 1);
    }
    }
}

// Converts one unfiltered row of `w` pixels into RGBA at `dst`, every `step` pixels.
void RowToRgba(const Header& h, const u8* row, unsigned w, u8* dst, unsigned step)
{
    const unsigned ch = Channels(h.type);
    const unsigned maxv = (1u << h.depth) - 1;
    for (unsigned x = 0; x < w; x++)
    {
        u8* o = dst + (size_t)x * step * 4;
        if (h.type == 3)
        {
            unsigned idx = Sample(row, x, h.depth);
            if (idx < h.paletteCount)
            {
                o[0] = h.palette[3 * idx];
                o[1] = h.palette[3 * idx + 1];
                o[2] = h.palette[3 * idx + 2];
                o[3] = h.alphaPal[idx];
            }
            else
                o[0] = o[1] = o[2] = o[3] = 0;
            continue;
        }
        unsigned s[4];
        for (unsigned c = 0; c < ch; c++)
            s[c] = Sample(row, (size_t)x * ch + c, h.depth);
        unsigned r, g, b, a = maxv;
        if (ch <= 2)
        {
            r = g = b = s[0];
            if (ch == 2)
                a = s[1];
            else if (h.hasKey && s[0] == h.keyR)
                a = 0;
        }
        else
        {
            r = s[0]; g = s[1]; b = s[2];
            if (ch == 4)
                a = s[3];
            else if (h.hasKey && r == h.keyR && g == h.keyG && b == h.keyB)
                a = 0;
        }
        o[0] = (u8)(r * 255 / maxv);
        o[1] = (u8)(g * 255 / maxv);
        o[2] = (u8)(b * 255 / maxv);
        o[3] = (u8)(a * 255 / maxv);
    }
}

}  // namespace

bool Inflate(const u8* in, size_t inSize, std::vector<u8>& out, std::string& error)
{
    out.clear();
    if (inSize < 2 || (in[0] & 0x0F) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20))
    {
        error = "bad zlib header";
        return false;
    }
    BitReader b(in + 2, inSize - 2);
    for (;;)
    {
        unsigned final = b.Bits(1), type = b.Bits(2);
        if (type == 0)
        {
            b.AlignByte();
            if (b.pos + 4 > b.size)
            {
                error = "truncated deflate data";
                return false;
            }
            unsigned len = b.d[b.pos] | (b.d[b.pos + 1] << 8);
            b.pos += 4;
            if (b.pos + len > b.size)
            {
                error = "truncated deflate data";
                return false;
            }
            out.insert(out.end(), b.d + b.pos, b.d + b.pos + len);
            b.pos += len;
        }
        else if (type == 1 || type == 2)
        {
            Huffman lit, dist;
            u8 lengths[320];
            int nlit = 288, ndist = 30;
            if (type == 1)
            {
                int i = 0;
                for (; i < 144; i++) lengths[i] = 8;
                for (; i < 256; i++) lengths[i] = 9;
                for (; i < 280; i++) lengths[i] = 7;
                for (; i < 288; i++) lengths[i] = 8;
                for (i = 0; i < 30; i++) lengths[288 + i] = 5;
            }
            else
            {
                nlit = (int)b.Bits(5) + 257;
                ndist = (int)b.Bits(5) + 1;
                int nclen = (int)b.Bits(4) + 4;
                static const u8 kOrder[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
                u8 cl[19];
                memset(cl, 0, sizeof(cl));
                for (int i = 0; i < nclen; i++)
                    cl[kOrder[i]] = (u8)b.Bits(3);
                Huffman clh;
                if (!clh.Build(cl, 19))
                {
                    error = "bad deflate code lengths";
                    return false;
                }
                int i = 0;
                while (i < nlit + ndist)
                {
                    int sym = clh.Decode(b);
                    if (sym < 0 || b.overrun)
                    {
                        error = "bad deflate code lengths";
                        return false;
                    }
                    if (sym < 16)
                        lengths[i++] = (u8)sym;
                    else
                    {
                        int rep = 0;
                        u8 val = 0;
                        if (sym == 16)
                        {
                            if (i == 0)
                            {
                                error = "bad deflate repeat";
                                return false;
                            }
                            val = lengths[i - 1];
                            rep = 3 + (int)b.Bits(2);
                        }
                        else if (sym == 17)
                            rep = 3 + (int)b.Bits(3);
                        else
                            rep = 11 + (int)b.Bits(7);
                        if (i + rep > nlit + ndist)
                        {
                            error = "bad deflate repeat";
                            return false;
                        }
                        while (rep--)
                            lengths[i++] = val;
                    }
                }
                // distance lengths follow the literal lengths in `lengths`
                memmove(lengths + 288, lengths + nlit, (size_t)ndist);
                for (int k = nlit; k < 288; k++)
                    lengths[k] = 0;
            }
            if (!lit.Build(lengths, nlit) || !dist.Build(lengths + 288, ndist))
            {
                error = "bad deflate tables";
                return false;
            }
            if (!Codes(b, lit, dist, out, error))
                return false;
        }
        else
        {
            error = "bad deflate block type";
            return false;
        }
        if (final)
            return true;
    }
}

bool IsPng(const u8* data, size_t size)
{
    return size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0;
}

bool Decode(const u8* data, size_t size, Image& out, std::string& error)
{
    if (!IsPng(data, size))
    {
        error = "not a PNG";
        return false;
    }
    Header h;
    memset(&h, 0, sizeof(h));
    memset(h.alphaPal, 255, sizeof(h.alphaPal));
    unsigned interlace = 0;
    std::vector<u8> idat;
    bool haveHeader = false;
    for (size_t p = 8; p + 12 <= size;)
    {
        unsigned len = Be32(data + p);
        const u8* type = data + p + 4;
        const u8* body = data + p + 8;
        if (p + 12 + (size_t)len > size)
        {
            error = "truncated PNG chunk";
            return false;
        }
        if (!memcmp(type, "IHDR", 4) && len >= 13)
        {
            h.width = Be32(body);
            h.height = Be32(body + 4);
            h.depth = body[8];
            h.type = body[9];
            interlace = body[12];
            haveHeader = true;
        }
        else if (!memcmp(type, "PLTE", 4))
        {
            h.palette = body;
            h.paletteCount = len / 3;
        }
        else if (!memcmp(type, "tRNS", 4))
        {
            if (h.type == 3)
                for (unsigned i = 0; i < len && i < 256; i++)
                    h.alphaPal[i] = body[i];
            else if (h.type == 0 && len >= 2)
            {
                h.hasKey = true;
                h.keyR = ((unsigned)body[0] << 8) | body[1];
            }
            else if (h.type == 2 && len >= 6)
            {
                h.hasKey = true;
                h.keyR = ((unsigned)body[0] << 8) | body[1];
                h.keyG = ((unsigned)body[2] << 8) | body[3];
                h.keyB = ((unsigned)body[4] << 8) | body[5];
            }
        }
        else if (!memcmp(type, "IDAT", 4))
            idat.insert(idat.end(), body, body + len);
        else if (!memcmp(type, "IEND", 4))
            break;
        p += 12 + (size_t)len;
    }
    const unsigned ch = Channels(h.type);
    if (!haveHeader || !ch || !h.width || !h.height || h.width > 8192 || h.height > 8192 ||
        (h.depth != 1 && h.depth != 2 && h.depth != 4 && h.depth != 8 && h.depth != 16) ||
        (h.type == 3 && (!h.palette || h.depth == 16)) || interlace > 1)
    {
        error = "unsupported PNG";
        return false;
    }
    std::vector<u8> raw;
    if (!Inflate(&idat[0], idat.size(), raw, error))
        return false;

    const unsigned bitsPerPixel = ch * h.depth;
    const unsigned bpp = bitsPerPixel >= 8 ? bitsPerPixel / 8 : 1;
    out.width = h.width;
    out.height = h.height;
    out.rgba.assign((size_t)h.width * h.height * 4, 0);

    // Adam7 passes (or one pass covering everything when not interlaced).
    static const unsigned kX0[7] = { 0, 4, 0, 2, 0, 1, 0 }, kY0[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const unsigned kDX[7] = { 8, 8, 4, 4, 2, 2, 1 }, kDY[7] = { 8, 8, 8, 4, 4, 2, 2 };
    const int passes = interlace ? 7 : 1;
    size_t at = 0;
    for (int pass = 0; pass < passes; pass++)
    {
        unsigned x0 = interlace ? kX0[pass] : 0, y0 = interlace ? kY0[pass] : 0;
        unsigned dx = interlace ? kDX[pass] : 1, dy = interlace ? kDY[pass] : 1;
        if (x0 >= h.width || y0 >= h.height)
            continue;
        unsigned pw = (h.width - x0 + dx - 1) / dx, ph = (h.height - y0 + dy - 1) / dy;
        size_t stride = ((size_t)pw * bitsPerPixel + 7) / 8;
        size_t need = ph * (stride + 1);
        if (at + need > raw.size())
        {
            error = "PNG data too short";
            return false;
        }
        if (!Unfilter(&raw[at], ph, stride, bpp, error))
            return false;
        for (unsigned y = 0; y < ph; y++)
        {
            const u8* row = &raw[at + y * (stride + 1) + 1];
            u8* dst = &out.rgba[(((size_t)(y0 + y * dy)) * h.width + x0) * 4];
            RowToRgba(h, row, pw, dst, dx);
        }
        at += need;
    }
    return true;
}

namespace {

unsigned Crc(const u8* p, size_t n, unsigned crc = 0xFFFFFFFFu)
{
    static unsigned table[256];
    static bool made = false;
    if (!made)
    {
        for (unsigned i = 0; i < 256; i++)
        {
            unsigned c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    for (size_t i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void Be32Out(std::vector<u8>& out, unsigned v)
{
    out.push_back((u8)(v >> 24));
    out.push_back((u8)(v >> 16));
    out.push_back((u8)(v >> 8));
    out.push_back((u8)v);
}

void Chunk(std::vector<u8>& out, const char* type, const std::vector<u8>& data)
{
    Be32Out(out, (unsigned)data.size());
    size_t at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    Be32Out(out, Crc(&out[at], out.size() - at) ^ 0xFFFFFFFFu);
}

}  // namespace

void Encode(unsigned w, unsigned h, const u8* rgba, std::vector<u8>& out)
{
    std::vector<u8> raw, z, head;
    for (unsigned y = 0; y < h; y++)
    {
        raw.push_back(0);               // (filter: none)
        raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
    }
    // zlib: stored blocks of up to 65535 bytes, then the Adler-32
    z.push_back(0x78);
    z.push_back(0x01);
    size_t at = 0;
    do
    {
        size_t n = raw.size() - at > 65535 ? 65535 : raw.size() - at;
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.push_back((u8)n);
        z.push_back((u8)(n >> 8));
        z.push_back((u8)~n);
        z.push_back((u8)(~n >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
    } while (at < raw.size());
    unsigned a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); i++)
    {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    Be32Out(z, (b << 16) | a);
    static const u8 kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    out.assign(kSignature, kSignature + 8);
    Be32Out(head, w);
    Be32Out(head, h);
    head.push_back(8);                  // 8 bits, RGBA, deflate, no filter, no interlace
    head.push_back(6);
    head.push_back(0);
    head.push_back(0);
    head.push_back(0);
    Chunk(out, "IHDR", head);
    Chunk(out, "IDAT", z);
    Chunk(out, "IEND", std::vector<u8>());
}

}  // namespace png
