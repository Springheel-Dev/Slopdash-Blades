#include "core/xma.h"

#include <string.h>

namespace xma {

namespace {

const unsigned kPacket = 2048;                      // XMA_BYTES_PER_PACKET
const unsigned kPayloadBits = (kPacket - 4) * 8;    // a packet less its 32-bit header
const unsigned kSamplesPerFrame = 512;
const unsigned kDataAlign = 2048;                   // where the packets start in the file

unsigned Le16(const unsigned char* p) { return p[0] | (p[1] << 8); }
unsigned Le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
unsigned Be32(const unsigned char* p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

void PutLe16(std::vector<unsigned char>& o, unsigned v) { o.push_back(v & 0xFF); o.push_back((v >> 8) & 0xFF); }
void PutLe32(std::vector<unsigned char>& o, unsigned v)
{
    for (int i = 0; i < 4; i++)
        o.push_back((v >> (8 * i)) & 0xFF);
}
// A four-character tag. (Not insert("RIFF", "RIFF" + 4): the XDK's compiler
// keeps each literal its own copy, so that range ran backwards and asked
// for 4 GB - 0.0.111's sound test died there.)
void PutTag(std::vector<unsigned char>& o, const char* tag)
{
    o.insert(o.end(), (const unsigned char*)tag, (const unsigned char*)tag + 4);
}

void PutBe32(std::vector<unsigned char>& o, unsigned v)
{
    for (int i = 3; i >= 0; i--)
        o.push_back((v >> (8 * i)) & 0xFF);
}

struct Chunk
{
    const unsigned char* data;
    unsigned size;
};

bool FindChunk(const unsigned char* d, size_t size, const char* id, Chunk& out)
{
    if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4))
        return false;
    size_t q = 12;
    while (q + 8 <= size)
    {
        unsigned len = Le32(d + q + 4);
        if (!memcmp(d + q, id, 4))
        {
            if (q + 8 + len > size)
                return false;
            out.data = d + q + 8;
            out.size = len;
            return true;
        }
        q += 8 + len + (len & 1);
    }
    return false;
}

// One bit of the packets' payload (their headers skipped), first bit first.
unsigned Bit(const unsigned char* data, unsigned i)
{
    unsigned p = i / kPayloadBits, r = i % kPayloadBits;
    return (data[p * kPacket + 4 + r / 8] >> (7 - r % 8)) & 1;
}

unsigned Bits(const unsigned char* data, unsigned i, unsigned n)
{
    unsigned v = 0;
    for (unsigned k = 0; k < n; k++)
        v = (v << 1) | Bit(data, i + k);
    return v;
}

}  // namespace

bool IsXma1(const unsigned char* data, size_t size)
{
    Chunk fmt;
    return FindChunk(data, size, "fmt ", fmt) && fmt.size >= 2 && Le16(fmt.data) == 0x165;
}

bool ToXma2(const unsigned char* d, size_t size, std::vector<unsigned char>& out)
{
    Chunk fmt, dat;
    if (!IsXma1(d, size) || !FindChunk(d, size, "fmt ", fmt) || !FindChunk(d, size, "data", dat) || fmt.size < 32)
        return false;
    // XMAWAVEFORMAT: tag, bits, options, largest skip, streams, loop count,
    // version, then per stream {pseudo bytes/s, rate, loop start, loop end,
    // subframe, channels, channel mask} (20 bytes each).
    unsigned streams = Le16(fmt.data + 8);
    // One stream only (mono or stereo: every dashboard sound is): several
    // streams' packets interleave, and their headers would need skip counts.
    if (streams != 1 || fmt.size < 12 + 20 * streams)
        return false;
    unsigned channels = 0, rate = 48000, avg = 0;
    for (unsigned s = 0; s < streams; s++)
    {
        const unsigned char* st = fmt.data + 12 + 20 * s;
        avg += Le32(st);
        rate = Le32(st + 4);
        channels += st[17];
    }
    unsigned packets = dat.size / kPacket;
    if (!packets || !channels)
        return false;
    std::vector<unsigned char> data(dat.data, dat.data + packets * kPacket);

    // The frames: each starts with its length in bits (15 bits); 0x7FFF (or
    // 0) is padding to the packet's end, the next frame at the next packet's
    // own first-frame offset (its header's bits 25..11).
    std::vector<unsigned> count(packets, 0);
    unsigned frames = 0;
    unsigned total = packets * kPayloadBits;
    unsigned pos = (Be32(&data[0]) >> 11) & 0x7FFF;
    while (pos + 15 <= total)
    {
        unsigned len = Bits(&data[0], pos, 15);
        if (len == 0x7FFF || len == 0)
        {
            unsigned p = pos / kPayloadBits + 1;
            if (p >= packets)
                break;
            unsigned off = (Be32(&data[p * kPacket]) >> 11) & 0x7FFF;
            if (off == 0x7FFF)
                break;
            pos = p * kPayloadBits + off;
            continue;
        }
        count[pos / kPayloadBits]++;
        frames++;
        pos += len;
    }
    if (!frames)
        return false;
    // XMA2 packet headers (xma2defs.h XMA2PACKET): frames starting in it (6
    // bits; XMA1 has a sequence number there), the first frame's offset (15,
    // kept), metadata (3: always 1 for XMA2) and the packets of other streams
    // to skip (8: none, one stream). XMA1 leaves those 11 low bits 0, which as
    // XMA2 is metadata 0: the 0.0.113 test's converted sound turned the
    // console off.
    for (unsigned p = 0; p < packets; p++)
    {
        unsigned h = Be32(&data[p * kPacket]);
        unsigned n = count[p] > 63 ? 63 : count[p];
        h = (n << 26) | (h & 0x03FFF800) | (1u << 8);
        for (int i = 0; i < 4; i++)
            data[p * kPacket + i] = (unsigned char)((h >> (24 - 8 * i)) & 0xFF);
    }
    unsigned samples = frames * kSamplesPerFrame;

    // XMA2WAVEFORMATEX (52 bytes), a one-block seek table, the packets.
    std::vector<unsigned char> f;
    PutLe16(f, 0x166);
    PutLe16(f, channels);
    PutLe32(f, rate);
    PutLe32(f, avg);
    PutLe16(f, channels * 2);
    PutLe16(f, 16);
    PutLe16(f, 34);
    PutLe16(f, streams);
    PutLe32(f, channels == 1 ? 0x4 : 0x3);     // SPEAKER_FRONT_CENTER; FRONT_LEFT | FRONT_RIGHT
    PutLe32(f, samples);                        // SamplesEncoded
    PutLe32(f, (unsigned)data.size());          // BytesPerBlock
    PutLe32(f, 0);                              // PlayBegin
    PutLe32(f, samples);                        // PlayLength
    PutLe32(f, 0);                              // LoopBegin
    PutLe32(f, 0);                              // LoopLength
    f.push_back(0);                             // LoopCount
    f.push_back(4);                             // EncoderVersion
    PutLe16(f, 1);                              // BlockCount
    std::vector<unsigned char> seek;
    PutBe32(seek, samples);

    // A JUNK chunk before the data, so the packets start 2048 bytes into the
    // file (kDataAlign): the hardware decoder reads whole aligned packets.
    const unsigned head = 12 + 8 + (unsigned)f.size() + 8 + (unsigned)seek.size();
    unsigned junk = (kDataAlign - (head + 8 + 8) % kDataAlign) % kDataAlign;

    out.clear();
    PutTag(out, "RIFF");
    PutLe32(out, (unsigned)(4 + 8 + f.size() + 8 + seek.size() + 8 + junk + 8 + data.size()));
    PutTag(out, "WAVE");
    PutTag(out, "fmt ");
    PutLe32(out, (unsigned)f.size());
    out.insert(out.end(), f.begin(), f.end());
    PutTag(out, "seek");
    PutLe32(out, (unsigned)seek.size());
    out.insert(out.end(), seek.begin(), seek.end());
    PutTag(out, "JUNK");
    PutLe32(out, junk);
    out.insert(out.end(), junk, 0);
    PutTag(out, "data");
    PutLe32(out, (unsigned)data.size());
    out.insert(out.end(), data.begin(), data.end());
    return true;
}

}  // namespace xma
