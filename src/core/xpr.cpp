#include "core/xpr.h"

namespace xpr {

namespace {

unsigned Le16(const unsigned char* p)
{
    return p[0] | (p[1] << 8);
}

unsigned Le32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

void Rgb565(unsigned c, int* rgb)
{
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    rgb[0] = r << 3 | r >> 2;
    rgb[1] = g << 2 | g >> 4;
    rgb[2] = b << 3 | b >> 2;
}

// A DXT block's 16 colours (its 8-byte colour part); DXT1 has its 1-bit alpha.
void Colours(const unsigned char* block, bool dxt1, unsigned char* out)
{
    unsigned c0 = Le16(block), c1 = Le16(block + 2), bits = Le32(block + 4);
    int a[3], b[3], cols[4][4];
    Rgb565(c0, a);
    Rgb565(c1, b);
    for (int k = 0; k < 3; k++)
    {
        cols[0][k] = a[k];
        cols[1][k] = b[k];
        if (c0 > c1 || !dxt1)
        {
            cols[2][k] = (2 * a[k] + b[k]) / 3;
            cols[3][k] = (a[k] + 2 * b[k]) / 3;
        }
        else
        {
            cols[2][k] = (a[k] + b[k]) / 2;
            cols[3][k] = 0;
        }
    }
    cols[0][3] = cols[1][3] = cols[2][3] = 255;
    cols[3][3] = c0 > c1 || !dxt1 ? 255 : 0;
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 4; k++)
            out[i * 4 + k] = (unsigned char)cols[(bits >> (2 * i)) & 3][k];
}

bool DecodeDxt(const unsigned char* d, size_t size, unsigned w, unsigned h, unsigned kind,
               std::vector<unsigned char>& out)
{
    size_t bsize = kind == 0x0C ? 8 : 16, p = 0;
    if ((size_t)((w + 3) / 4) * ((h + 3) / 4) * bsize > size)
        return false;
    out.assign((size_t)w * h * 4, 0);
    unsigned char px[64];
    for (unsigned by = 0; by < h; by += 4)
        for (unsigned bx = 0; bx < w; bx += 4, p += bsize)
        {
            const unsigned char* blk = d + p;
            if (kind == 0x0C)
                Colours(blk, true, px);
            else
            {
                Colours(blk + 8, false, px);
                if (kind == 0x0E)
                    for (int i = 0; i < 16; i++)
                        px[i * 4 + 3] = (unsigned char)(((blk[i / 2] >> (4 * (i & 1))) & 15) * 17);
                else
                {
                    int a0 = blk[0], a1 = blk[1], table[8] = { a0, a1, 0, 0, 0, 0, 0, 0 };
                    for (int i = 0; i < 6; i++)
                        table[2 + i] = a0 > a1 ? ((6 - i) * a0 + (i + 1) * a1) / 7
                                               : i < 4 ? ((4 - i) * a0 + (i + 1) * a1) / 5 : i == 4 ? 0 : 255;
                    unsigned long long bits = 0;
                    for (int i = 0; i < 6; i++)
                        bits |= (unsigned long long)blk[2 + i] << (8 * i);
                    for (int i = 0; i < 16; i++)
                        px[i * 4 + 3] = (unsigned char)table[(bits >> (3 * i)) & 7];
                }
            }
            for (int i = 0; i < 16; i++)
            {
                unsigned x = bx + (i & 3), y = by + (i >> 2);
                if (x < w && y < h)
                    for (int k = 0; k < 4; k++)
                        out[((size_t)y * w + x) * 4 + k] = px[i * 4 + k];
            }
        }
    return true;
}

// Morton order for a power-of-two texture (as XGUnswizzleRect).
size_t Unswizzle(unsigned x, unsigned y, unsigned w, unsigned h)
{
    size_t o = 0;
    int bit = 0;
    for (unsigned mw = w, mh = h; mw > 1 || mh > 1;)
    {
        if (mw > 1)
        {
            o |= (size_t)(x & 1) << bit++;
            x >>= 1;
            mw >>= 1;
        }
        if (mh > 1)
        {
            o |= (size_t)(y & 1) << bit++;
            y >>= 1;
            mh >>= 1;
        }
    }
    return o;
}

}  // namespace

bool Decode(const unsigned char* d, size_t size, unsigned& w, unsigned& h, std::vector<unsigned char>& rgba)
{
    if (size < 32 || d[0] != 'X' || d[1] != 'P' || d[2] != 'R' || d[3] != '0')
        return false;
    unsigned hsize = Le32(d + 8), format = Le32(d + 24), linear = Le32(d + 28), data = Le32(d + 16);
    unsigned kind = (format >> 8) & 0xFF;
    w = 1u << ((format >> 20) & 0xF);
    h = 1u << ((format >> 24) & 0xF);
    if (linear)
    {
        w = (linear & 0xFFF) + 1;
        h = ((linear >> 12) & 0xFFF) + 1;
    }
    size_t at = (size_t)hsize + data;
    if (at >= size || w > 1024 || h > 1024)
        return false;
    const unsigned char* body = d + at;
    size_t left = size - at;
    if (kind == 0x0C || kind == 0x0E || kind == 0x0F)
        return DecodeDxt(body, left, w, h, kind, rgba);
    if (kind == 0x06 || kind == 0x07 || kind == 0x12 || kind == 0x1E)
    {
        if ((size_t)w * h * 4 > left)
            return false;
        bool swizzled = kind == 0x06 || kind == 0x07, alpha = kind == 0x06 || kind == 0x12;
        rgba.resize((size_t)w * h * 4);
        for (unsigned y = 0; y < h; y++)
            for (unsigned x = 0; x < w; x++)
            {
                const unsigned char* s = body + 4 * (swizzled ? Unswizzle(x, y, w, h) : (size_t)y * w + x);
                unsigned char* o = &rgba[((size_t)y * w + x) * 4];
                o[0] = s[2];
                o[1] = s[1];
                o[2] = s[0];
                o[3] = alpha ? s[3] : 255;
            }
        return true;
    }
    return false;
}

}  // namespace xpr
