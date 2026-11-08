#include "core/mjpeg.h"

#include <math.h>
#include <string.h>

namespace mjpeg {

namespace {

// ITU T.81 Annex K: the example tables every decoder knows.
const unsigned char kLumaQ[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
const unsigned char kChromaQ[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
const unsigned char kZigzag[64] = {
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42, 3, 8, 12, 17, 25, 30, 41, 43,
    9, 11, 18, 24, 31, 40, 44, 53, 10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63};

const unsigned char kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const unsigned char kDcLumaVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const unsigned char kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const unsigned char kDcChromaVals[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const unsigned char kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
const unsigned char kAcLumaVals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
const unsigned char kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
const unsigned char kAcChromaVals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

struct Code { unsigned short bits, length; };

// A Huffman table's codes by symbol, from its counts and values (T.81 C.2).
void Codes(const unsigned char* bits, const unsigned char* vals, Code* out)
{
    unsigned code = 0, k = 0;
    for (int len = 1; len <= 16; len++)
    {
        for (int i = 0; i < bits[len - 1]; i++)
        {
            out[vals[k]].bits = (unsigned short)code++;
            out[vals[k]].length = (unsigned short)len;
            k++;
        }
        code <<= 1;
    }
}

// A fixed-size output, for the recorder (no allocation while recording: the
// system heap SlopGuide shares is small, and growing a vector there each
// frame slowed the recorder to a frame a second, then hung it, 2026-10-20).
// What doesn't fit is dropped, and full says so.
struct FixedOut
{
    unsigned char* data;
    size_t size, capacity;
    bool full;
    unsigned char* end() { return data + size; }
    void push_back(unsigned char b)
    {
        if (size < capacity)
            data[size++] = b;
        else
            full = true;
    }
    template <class It>
    void insert(unsigned char*, It first, It last)
    {
        for (; first != last; ++first)
            push_back((unsigned char)*first);
    }
};

template <class O>
struct Bits
{
    O* out;
    unsigned buffer;
    int count;
    explicit Bits(O& o) : out(&o), buffer(0), count(0) {}
    void Put(unsigned bits, int length)
    {
        buffer = (buffer << length) | (bits & ((1u << length) - 1));
        count += length;
        while (count >= 8)
        {
            unsigned char b = (unsigned char)(buffer >> (count - 8));
            out->push_back(b);
            if (b == 0xFF)
                out->push_back(0);          // (stuffed: 0xFF in the data is followed by 0)
            count -= 8;
        }
    }
};

template <class O> void Byte(O& o, int b) { o.push_back((unsigned char)b); }
template <class O> void Word(O& o, int w) { o.push_back((unsigned char)(w >> 8)); o.push_back((unsigned char)w); }

// The AAN forward DCT (Arai, Agui, Nakajima) on 8 values, in place.
void Dct8(float* d, int step)
{
    float t0 = d[0] + d[7 * step], t7 = d[0] - d[7 * step];
    float t1 = d[step] + d[6 * step], t6 = d[step] - d[6 * step];
    float t2 = d[2 * step] + d[5 * step], t5 = d[2 * step] - d[5 * step];
    float t3 = d[3 * step] + d[4 * step], t4 = d[3 * step] - d[4 * step];
    float t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
    d[0] = t10 + t11;
    d[4 * step] = t10 - t11;
    float z1 = (t12 + t13) * 0.707106781f;
    d[2 * step] = t13 + z1;
    d[6 * step] = t13 - z1;
    t10 = t4 + t5;
    t11 = t5 + t6;
    t12 = t6 + t7;
    float z5 = (t10 - t12) * 0.382683433f, z2 = t10 * 0.541196100f + z5, z4 = t12 * 1.306562965f + z5;
    float z3 = t11 * 0.707106781f, z11 = t7 + z3, z13 = t7 - z3;
    d[5 * step] = z13 + z2;
    d[3 * step] = z13 - z2;
    d[step] = z11 + z4;
    d[7 * step] = z11 - z4;
}

void Number(int v, unsigned& bits, int& length)
{
    int a = v < 0 ? -v : v;
    length = 0;
    while (a)
    {
        length++;
        a >>= 1;
    }
    bits = (unsigned)(v < 0 ? v - 1 : v) & ((1u << length) - 1);
}

// One 8 x 8 block: transformed, quantised (scale: the table with the DCT's
// own factors), its DC as a difference, its AC as runs.
template <class O>
int Block(Bits<O>& out, float* block, const float* scale, int dc, const Code* dcCodes, const Code* acCodes)
{
    for (int r = 0; r < 8; r++)
        Dct8(block + r * 8, 1);
    for (int c = 0; c < 8; c++)
        Dct8(block + c, 8);
    int q[64];
    for (int i = 0; i < 64; i++)
    {
        float v = block[i] * scale[i];
        q[kZigzag[i]] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    }
    unsigned bits;
    int length;
    Number(q[0] - dc, bits, length);
    out.Put(dcCodes[length].bits, dcCodes[length].length);
    if (length)
        out.Put(bits, length);
    int last = 63;
    while (last > 0 && !q[last])
        last--;
    for (int i = 1, run = 0; i <= last; i++)
    {
        if (!q[i])
        {
            run++;
            continue;
        }
        while (run >= 16)
        {
            out.Put(acCodes[0xF0].bits, acCodes[0xF0].length);     // 16 zeros
            run -= 16;
        }
        Number(q[i], bits, length);
        int symbol = (run << 4) | length;
        out.Put(acCodes[symbol].bits, acCodes[symbol].length);
        out.Put(bits, length);
        run = 0;
    }
    if (last < 63)
        out.Put(acCodes[0].bits, acCodes[0].length);               // end of block
    return q[0];
}

template <class O>
void Table(O& o, int klass, int id, const unsigned char* bits, const unsigned char* vals)
{
    int n = 0;
    for (int i = 0; i < 16; i++)
        n += bits[i];
    Word(o, 0xFFC4);
    Word(o, 3 + 16 + n);
    Byte(o, (klass << 4) | id);
    o.insert(o.end(), bits, bits + 16);
    o.insert(o.end(), vals, vals + n);
}

template <class O>
void EncodeTo(const unsigned char* rgb, int width, int height, int stride, int quality, O& out)
{
    static Code dcY[256], dcC[256], acY[256], acC[256];
    static bool made = false;
    if (!made)
    {
        Codes(kDcLumaBits, kDcLumaVals, dcY);
        Codes(kDcChromaBits, kDcChromaVals, dcC);
        Codes(kAcLumaBits, kAcLumaVals, acY);
        Codes(kAcChromaBits, kAcChromaVals, acC);
        made = true;
    }
    if (quality < 1)
        quality = 1;
    if (quality > 100)
        quality = 100;
    int s = quality < 50 ? 5000 / quality : 200 - quality * 2;
    unsigned char qY[64], qC[64];
    for (int i = 0; i < 64; i++)
    {
        int y = (kLumaQ[i] * s + 50) / 100, c = (kChromaQ[i] * s + 50) / 100;
        qY[kZigzag[i]] = (unsigned char)(y < 1 ? 1 : y > 255 ? 255 : y);
        qC[kZigzag[i]] = (unsigned char)(c < 1 ? 1 : c > 255 ? 255 : c);
    }
    // Each coefficient's divisor with the AAN DCT's own scale folded in.
    static const float kAan[8] = {1.0f, 1.387039845f, 1.306562965f, 1.175875602f,
                                  1.0f, 0.785694958f, 0.541196100f, 0.275899379f};
    float sY[64], sC[64];
    for (int r = 0, i = 0; r < 8; r++)
        for (int c = 0; c < 8; c++, i++)
        {
            sY[i] = 1.0f / (qY[kZigzag[i]] * kAan[r] * kAan[c] * 8.0f);
            sC[i] = 1.0f / (qC[kZigzag[i]] * kAan[r] * kAan[c] * 8.0f);
        }

    Word(out, 0xFFD8);                                  // SOI
    Word(out, 0xFFE0);                                  // APP0 JFIF
    Word(out, 16);
    const char jfif[] = "JFIF";
    out.insert(out.end(), jfif, jfif + 5);
    Word(out, 0x0101);
    Byte(out, 0);
    Word(out, 1);
    Word(out, 1);
    Byte(out, 0);
    Byte(out, 0);
    Word(out, 0xFFDB);                                  // DQT: both tables
    Word(out, 2 + 2 * 65);
    Byte(out, 0);
    out.insert(out.end(), qY, qY + 64);
    Byte(out, 1);
    out.insert(out.end(), qC, qC + 64);
    Word(out, 0xFFC0);                                  // SOF0: 3 components, Y 2x2
    Word(out, 17);
    Byte(out, 8);
    Word(out, height);
    Word(out, width);
    Byte(out, 3);
    Byte(out, 1); Byte(out, 0x22); Byte(out, 0);
    Byte(out, 2); Byte(out, 0x11); Byte(out, 1);
    Byte(out, 3); Byte(out, 0x11); Byte(out, 1);
    Table(out, 0, 0, kDcLumaBits, kDcLumaVals);        // DHT (in every frame: AVI's MJPEG may leave them out,
    Table(out, 1, 0, kAcLumaBits, kAcLumaVals);        //  but every decoder takes them)
    Table(out, 0, 1, kDcChromaBits, kDcChromaVals);
    Table(out, 1, 1, kAcChromaBits, kAcChromaVals);
    Word(out, 0xFFDA);                                  // SOS
    Word(out, 12);
    Byte(out, 3);
    Byte(out, 1); Byte(out, 0x00);
    Byte(out, 2); Byte(out, 0x11);
    Byte(out, 3); Byte(out, 0x11);
    Byte(out, 0); Byte(out, 63); Byte(out, 0);

    Bits<O> bits(out);
    int dcY_ = 0, dcCb = 0, dcCr = 0;
    float Y[4][64], Cb[64], Cr[64];
    for (int my = 0; my < height; my += 16)
        for (int mx = 0; mx < width; mx += 16)
        {
            memset(Cb, 0, sizeof(Cb));
            memset(Cr, 0, sizeof(Cr));
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                {
                    int px = mx + x < width ? mx + x : width - 1, py = my + y < height ? my + y : height - 1;
                    const unsigned char* p = rgb + py * stride + px * 3;
                    float r = p[0], g = p[1], b = p[2];
                    Y[(y >> 3) * 2 + (x >> 3)][(y & 7) * 8 + (x & 7)] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                    int c = (y >> 1) * 8 + (x >> 1);
                    Cb[c] += (-0.168736f * r - 0.331264f * g + 0.5f * b) * 0.25f;
                    Cr[c] += (0.5f * r - 0.418688f * g - 0.081312f * b) * 0.25f;
                }
            for (int i = 0; i < 4; i++)
                dcY_ = Block(bits, Y[i], sY, dcY_, dcY, acY);
            dcCb = Block(bits, Cb, sC, dcCb, dcC, acC);
            dcCr = Block(bits, Cr, sC, dcCr, dcC, acC);
        }
    // The last byte filled with 1s (T.81 F.1.2.3).
    if (bits.count % 8)
        bits.Put(0x7F, 8 - bits.count % 8);
    Word(out, 0xFFD9);                                  // EOI
}

}  // namespace

void Encode(const unsigned char* rgb, int width, int height, int stride, int quality, std::vector<unsigned char>& out)
{
    EncodeTo(rgb, width, height, stride, quality, out);
}

size_t Encode(const unsigned char* rgb, int width, int height, int stride, int quality, unsigned char* out,
              size_t capacity)
{
    FixedOut o = {out, 0, capacity, false};
    EncodeTo(rgb, width, height, stride, quality, o);
    return o.full ? 0 : o.size;
}

// --- AVI ---------------------------------------------------------------------------
namespace {

template <class O>
void Le32(O& o, unsigned v)
{
    o.push_back((unsigned char)v);
    o.push_back((unsigned char)(v >> 8));
    o.push_back((unsigned char)(v >> 16));
    o.push_back((unsigned char)(v >> 24));
}
template <class O>
void Le16(O& o, unsigned v)
{
    o.push_back((unsigned char)v);
    o.push_back((unsigned char)(v >> 8));
}
template <class O>
void Four(O& o, const char* f) { o.insert(o.end(), f, f + 4); }

const unsigned kHeader = 224;           // RIFF, hdrl (avih, strl: strh, strf), movi's start

}  // namespace

AviWriter::AviWriter(int width, int height, int fps)
    : m_width(width), m_height(height), m_fps(fps), m_movie(0), m_frames(0), m_largest(0)
{
}

unsigned AviWriter::Count(size_t jpegSize)
{
    unsigned at = (unsigned)(4 + m_movie);              // from the 'movi' tag
    m_movie += 8 + jpegSize + (jpegSize & 1);
    m_frames++;
    if (jpegSize > m_largest)
        m_largest = (unsigned)jpegSize;
    return at;
}

std::vector<unsigned char> AviWriter::Header() const
{
    std::vector<unsigned char> o;
    HeaderTo(o);
    return o;
}

size_t AviWriter::Header(unsigned char* out) const
{
    FixedOut o = {out, 0, kHeader, false};
    HeaderTo(o);
    return o.size;
}

template <class O>
void AviWriter::HeaderTo(O& o) const
{
    unsigned frames = (unsigned)m_frames;
    unsigned index = 8 + 16 * frames;
    Four(o, "RIFF");
    Le32(o, (unsigned)(kHeader - 8 + m_movie + index));
    Four(o, "AVI ");
    Four(o, "LIST");
    Le32(o, 192);
    Four(o, "hdrl");
    Four(o, "avih");
    Le32(o, 56);
    Le32(o, 1000000 / m_fps);                           // microseconds a frame
    Le32(o, m_largest * m_fps);                         // most bytes a second
    Le32(o, 0);
    Le32(o, 0x10);                                      // AVIF_HASINDEX
    Le32(o, frames);
    Le32(o, 0);
    Le32(o, 1);                                         // streams
    Le32(o, m_largest + 8);
    Le32(o, m_width);
    Le32(o, m_height);
    Le32(o, 0); Le32(o, 0); Le32(o, 0); Le32(o, 0);
    Four(o, "LIST");
    Le32(o, 116);
    Four(o, "strl");
    Four(o, "strh");
    Le32(o, 56);
    Four(o, "vids");
    Four(o, "MJPG");
    Le32(o, 0);
    Le16(o, 0);
    Le16(o, 0);
    Le32(o, 0);
    Le32(o, 1);                                         // scale
    Le32(o, m_fps);                                     // rate: frames a second
    Le32(o, 0);
    Le32(o, frames);
    Le32(o, m_largest + 8);
    Le32(o, 0xFFFFFFFF);                                // quality: default
    Le32(o, 0);
    Le16(o, 0); Le16(o, 0); Le16(o, m_width); Le16(o, m_height);
    Four(o, "strf");
    Le32(o, 40);
    Le32(o, 40);                                        // BITMAPINFOHEADER
    Le32(o, m_width);
    Le32(o, m_height);
    Le16(o, 1);
    Le16(o, 24);
    Four(o, "MJPG");
    Le32(o, m_width * m_height * 3);
    Le32(o, 0); Le32(o, 0); Le32(o, 0); Le32(o, 0);
    Four(o, "LIST");
    Le32(o, (unsigned)(4 + m_movie));
    Four(o, "movi");
}

std::vector<unsigned char> AviWriter::Frame(const std::vector<unsigned char>& jpeg)
{
    std::vector<unsigned char> o;
    Four(o, "00dc");
    Le32(o, (unsigned)jpeg.size());
    o.insert(o.end(), jpeg.begin(), jpeg.end());
    if (jpeg.size() & 1)
        o.push_back(0);                                 // (chunks are word aligned)
    m_offsets.push_back(Count(jpeg.size()));
    m_sizes.push_back((unsigned)jpeg.size());
    return o;
}

std::vector<unsigned char> AviWriter::Index() const
{
    std::vector<unsigned char> o;
    Four(o, "idx1");
    Le32(o, (unsigned)(16 * m_sizes.size()));
    for (size_t i = 0; i < m_sizes.size(); i++)
    {
        Four(o, "00dc");
        Le32(o, 0x10);                                  // AVIIF_KEYFRAME
        Le32(o, m_offsets[i]);
        Le32(o, m_sizes[i]);
    }
    return o;
}

}  // namespace mjpeg
