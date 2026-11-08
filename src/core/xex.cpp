#include "core/xex.h"

#include <string.h>

namespace xex {

namespace {

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

u32 Be32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
u16 Be16(const u8* p) { return (u16)((p[0] << 8) | p[1]); }

// ---------------------------------------------------------------------------
// AES-128 decryption (tables built at first use)
// ---------------------------------------------------------------------------

u8 g_sbox[256], g_inv[256];
u8 g_mul9[256], g_mul11[256], g_mul13[256], g_mul14[256];     // InvMixColumns products
bool g_aesReady = false;

u8 Mul(u8 a, u8 b)
{
    u8 r = 0;
    while (b)
    {
        if (b & 1)
            r ^= a;
        a = (u8)((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
        b >>= 1;
    }
    return r;
}

void AesTables()
{
    if (g_aesReady)
        return;
    for (int i = 0; i < 256; i++)
    {
        u8 inv = 0;                        // multiplicative inverse in GF(2^8)
        for (int j = 1; j < 256 && i; j++)
            if (Mul((u8)i, (u8)j) == 1)
            {
                inv = (u8)j;
                break;
            }
        u8 s = inv, x = inv;
        for (int k = 0; k < 4; k++)
        {
            x = (u8)((x << 1) | (x >> 7));
            s ^= x;
        }
        s ^= 0x63;
        g_sbox[i] = s;
        g_inv[s] = (u8)i;
    }
    for (int i = 0; i < 256; i++)
    {
        g_mul9[i] = Mul((u8)i, 9);
        g_mul11[i] = Mul((u8)i, 11);
        g_mul13[i] = Mul((u8)i, 13);
        g_mul14[i] = Mul((u8)i, 14);
    }
    g_aesReady = true;
}

struct Aes
{
    u8 rk[176];

    explicit Aes(const u8* key)
    {
        AesTables();
        memcpy(rk, key, 16);
        u8 rcon = 1;
        for (int i = 16; i < 176; i += 4)
        {
            u8 t[4] = { rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1] };
            if (i % 16 == 0)
            {
                u8 first = t[0];
                t[0] = (u8)(g_sbox[t[1]] ^ rcon);
                t[1] = g_sbox[t[2]];
                t[2] = g_sbox[t[3]];
                t[3] = g_sbox[first];
                rcon = Mul(rcon, 2);
            }
            for (int k = 0; k < 4; k++)
                rk[i + k] = (u8)(rk[i - 16 + k] ^ t[k]);
        }
    }

    void DecryptBlock(u8* b) const
    {
        for (int k = 0; k < 16; k++)
            b[k] ^= rk[160 + k];
        for (int round = 9; round >= 0; round--)
        {
            u8 t[16];
            for (int c = 0; c < 4; c++)             // InvShiftRows + InvSubBytes
                for (int r = 0; r < 4; r++)
                    t[((c + r) % 4) * 4 + r] = g_inv[b[c * 4 + r]];
            for (int k = 0; k < 16; k++)
                t[k] ^= rk[round * 16 + k];
            if (round > 0)
                for (int c = 0; c < 4; c++)         // InvMixColumns
                {
                    u8* s = t + c * 4;
                    u8 a0 = s[0], a1 = s[1], a2 = s[2], a3 = s[3];
                    s[0] = (u8)(g_mul14[a0] ^ g_mul11[a1] ^ g_mul13[a2] ^ g_mul9[a3]);
                    s[1] = (u8)(g_mul9[a0] ^ g_mul14[a1] ^ g_mul11[a2] ^ g_mul13[a3]);
                    s[2] = (u8)(g_mul13[a0] ^ g_mul9[a1] ^ g_mul14[a2] ^ g_mul11[a3]);
                    s[3] = (u8)(g_mul11[a0] ^ g_mul13[a1] ^ g_mul9[a2] ^ g_mul14[a3]);
                }
            memcpy(b, t, 16);
        }
    }
};

void CbcDecrypt(const Aes& aes, u8* data, size_t size)
{
    u8 iv[16] = { 0 }, next[16];
    for (size_t p = 0; p + 16 <= size; p += 16)
    {
        memcpy(next, data + p, 16);
        aes.DecryptBlock(data + p);
        for (int k = 0; k < 16; k++)
            data[p + k] ^= iv[k];
        memcpy(iv, next, 16);
    }
}

const u8 kRetailKey[16] = { 0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                            0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91 };

// ---------------------------------------------------------------------------
// LZX
// ---------------------------------------------------------------------------

struct Bits
{
    const u8* d;
    size_t size, pos;
    u32 buf;
    int n;

    Bits(const u8* data, size_t len) : d(data), size(len), pos(0), buf(0), n(0) {}

    void Fill()
    {
        while (n <= 16)
        {
            u32 w = pos + 1 < size ? (u32)(d[pos] | (d[pos + 1] << 8)) : 0;
            pos += 2;
            buf |= w << (16 - n);       // buf holds n bits, left-aligned
            n += 16;
        }
    }
    u32 Peek(int k) { Fill(); return buf >> (32 - k); }
    void Skip(int k) { buf <<= k; n -= k; }
    u32 Read(int k)
    {
        if (k == 0)
            return 0;
        if (k > 16)
        {
            u32 hi = Read(16);
            return (hi << (k - 16)) | Read(k - 16);
        }
        u32 v = Peek(k);
        Skip(k);
        return v;
    }
};

struct Tree
{
    enum { kBits = 16 };
    std::vector<u32> table;     // (symbol << 8) | length, indexed by the next 16 bits

    bool Build(const u8* lengths, int count)
    {
        table.assign(1u << kBits, 0);
        u32 code = 0;
        for (int len = 1; len <= 16; len++)
        {
            for (int sym = 0; sym < count; sym++)
            {
                if (lengths[sym] != len)
                    continue;
                u32 start = code << (kBits - len), span = 1u << (kBits - len);
                if (start + span > table.size())
                    return false;               // over-subscribed code
                for (u32 j = 0; j < span; j++)
                    table[start + j] = ((u32)sym << 8) | (u32)len;
                code++;
            }
            code <<= 1;
        }
        return true;
    }
    int Decode(Bits& b) const
    {
        u32 e = table[b.Peek(kBits)];
        b.Skip((int)(e & 0xFF));
        return (int)(e >> 8);
    }
};

bool ReadLengths(Bits& b, u8* lens, int first, int last)
{
    u8 pre[20];
    for (int i = 0; i < 20; i++)
        pre[i] = (u8)b.Read(4);
    Tree t;
    if (!t.Build(pre, 20))
        return false;
    int i = first;
    while (i < last)
    {
        int z = t.Decode(b);
        int n;
        if (z == 17)
        {
            n = 4 + (int)b.Read(4);
            for (int k = 0; k < n && i < last; k++) lens[i++] = 0;
        }
        else if (z == 18)
        {
            n = 20 + (int)b.Read(5);
            for (int k = 0; k < n && i < last; k++) lens[i++] = 0;
        }
        else if (z == 19)
        {
            n = 4 + (int)b.Read(1);
            z = t.Decode(b);
            u8 v = (u8)((lens[i] - z + 17) % 17);
            for (int k = 0; k < n && i < last; k++) lens[i++] = v;
        }
        else
        {
            lens[i] = (u8)((lens[i] - z + 17) % 17);
            i++;
        }
    }
    return true;
}

}  // namespace

// Undoes LZX's E8 translation: in each 32 KB frame, the 32-bit value after a
// 0xE8 byte was stored as an absolute position (libmspack's lzxd, frames
// beyond 32768 and the last 10 bytes of a frame untouched).
void UndoE8(u8* out, size_t size, int fileSize)
{
    for (size_t frame = 0; frame < size; frame += 32768)
    {
        size_t frameSize = size - frame < 32768 ? size - frame : 32768;
        if (frame / 32768 >= 32768 || frameSize <= 10)
            continue;
        size_t end = frame + frameSize - 10;
        for (size_t i = frame; i < end;)
        {
            if (out[i] != 0xE8)
            {
                i++;
                continue;
            }
            int cur = (int)i;
            u8* v = out + i + 1;
            int absOff = (int)((u32)v[0] | ((u32)v[1] << 8) | ((u32)v[2] << 16) | ((u32)v[3] << 24));
            if (absOff >= -cur && absOff < fileSize)
            {
                u32 rel = (u32)(absOff >= 0 ? absOff - cur : absOff + fileSize);
                v[0] = (u8)rel;
                v[1] = (u8)(rel >> 8);
                v[2] = (u8)(rel >> 16);
                v[3] = (u8)(rel >> 24);
            }
            i += 5;
        }
    }
}

bool Lzx(const u8* in, size_t inSize, unsigned windowBits, u8* out, size_t outSize, std::string& error)
{
    static const int kSlots[] = { 30, 32, 34, 36, 38, 42, 50 };    // window bits 15..21
    if (windowBits < 15 || windowBits > 21)
    {
        error = "unsupported LZX window";
        return false;
    }
    u32 extra[51], base[51];
    for (u32 i = 0, b = 0; i < 51; i++)
    {
        extra[i] = i < 4 ? 0 : ((i - 2) / 2 > 17 ? 17 : (i - 2) / 2);
        base[i] = b;
        b += 1u << extra[i];
    }
    const int mainCount = 256 + kSlots[windowBits - 15] * 8;
    std::vector<u8> mainLens(mainCount, 0), lenLens(249, 0);
    Tree mainTree, lenTree, alignedTree;
    Bits b(in, inSize);
    u32 r0 = 1, r1 = 1, r2 = 1;
    // The E8 ("Intel call") translation: dash.xex doesn't use it, the system
    // files (hud.xex...) do; undone per 32 KB frame at the end (as libmspack).
    u32 e8Size = b.Read(1) ? b.Read(32) : 0;
    size_t pos = 0, frameEnd = 32768;
    u32 remaining = 0;
    int type = 0;
    while (pos < outSize)
    {
        if (remaining == 0)
        {
            type = (int)b.Read(3);
            remaining = (b.Read(16) << 8) | b.Read(8);
            if (type == 2)
            {
                u8 al[8];
                for (int i = 0; i < 8; i++)
                    al[i] = (u8)b.Read(3);
                alignedTree.Build(al, 8);
            }
            if (type == 1 || type == 2)
            {
                if (!ReadLengths(b, &mainLens[0], 0, 256) || !ReadLengths(b, &mainLens[0], 256, mainCount) ||
                    !mainTree.Build(&mainLens[0], mainCount) || !ReadLengths(b, &lenLens[0], 0, 249) ||
                    !lenTree.Build(&lenLens[0], 249))
                {
                    error = "bad LZX code lengths";
                    return false;
                }
            }
            else if (type == 3)
            {
                b.Fill();
                if (b.n > 16)
                    b.pos -= 2;
                b.n = 0;
                b.buf = 0;
                if (b.pos + 12 > inSize)
                {
                    error = "truncated LZX stream";
                    return false;
                }
                const u8* p = in + b.pos;
                r0 = p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
                r1 = p[4] | (p[5] << 8) | (p[6] << 16) | ((u32)p[7] << 24);
                r2 = p[8] | (p[9] << 8) | (p[10] << 16) | ((u32)p[11] << 24);
                b.pos += 12;
            }
            else
            {
                error = "bad LZX block type";
                return false;
            }
        }
        size_t stop = pos + remaining;
        if (stop > frameEnd) stop = frameEnd;
        if (stop > outSize) stop = outSize;
        if (type == 3)
        {
            size_t n = stop - pos;
            if (b.pos + n > inSize)
            {
                error = "truncated LZX stream";
                return false;
            }
            memcpy(out + pos, in + b.pos, n);
            b.pos += n;
            pos += n;
            remaining -= (u32)n;
            if (remaining == 0 && (b.pos & 1))
                b.pos++;
        }
        else
        {
            size_t start = pos;
            while (pos < stop)
            {
                int sym = mainTree.Decode(b);
                if (sym < 256)
                {
                    out[pos++] = (u8)sym;
                    continue;
                }
                sym -= 256;
                u32 len = sym & 7;
                if (len == 7)
                    len += lenTree.Decode(b);
                len += 2;
                int slot = sym >> 3;
                u32 off;
                if (slot == 0)
                    off = r0;
                else if (slot == 1)
                {
                    off = r1; r1 = r0; r0 = off;
                }
                else if (slot == 2)
                {
                    off = r2; r2 = r0; r0 = off;
                }
                else
                {
                    u32 e = extra[slot];
                    off = base[slot] - 2;
                    if (type == 2 && e >= 3)
                    {
                        if (e > 3)
                            off += b.Read((int)e - 3) << 3;
                        off += alignedTree.Decode(b);
                    }
                    else if (e)
                        off += b.Read((int)e);
                    r2 = r1; r1 = r0; r0 = off;
                }
                if (off > pos || pos + len > outSize)
                {
                    error = "bad LZX match";
                    return false;
                }
                for (u32 k = 0; k < len; k++, pos++)
                    out[pos] = out[pos - off];
            }
            if (pos - start > remaining)
            {
                error = "LZX match ran past its block";
                return false;
            }
            remaining -= (u32)(pos - start);
        }
        if (pos >= frameEnd)
        {
            frameEnd += 32768;
            if (type != 3 && b.n)
                b.Skip(b.n & 15);               // realign to 16 bits after each frame
        }
    }
    if (e8Size)
        UndoE8(out, outSize, (int)e8Size);
    return true;
}

const Resource* Image::Find(const char* name) const
{
    for (size_t i = 0; i < resources.size(); i++)
        if (resources[i].name == name)
            return &resources[i];
    return NULL;
}

// The image from the file's body, `master` decrypting the image key. False
// (error set) if it doesn't come out as a PE image (the wrong key, say).
static bool Unpack(const u8* file, size_t size, u32 dataOff, u32 secOff, u32 fmt, const u8* master, Image& out,
                   std::string& error)
{
    const u32 fmtSize = Be32(file + fmt);
    const u16 encryption = Be16(file + fmt + 4), compression = Be16(file + fmt + 6);
    const u32 imageSize = Be32(file + secOff + 4);
    std::vector<u8> body(file + dataOff, file + size);
    if (encryption == 1)
    {
        u8 key[16];
        memcpy(key, file + secOff + 0x150, 16);
        Aes(master).DecryptBlock(key);
        CbcDecrypt(Aes(key), &body[0], body.size() / 16 * 16);
    }
    if (compression != 2)
    {
        // None: the image as it is. Basic: runs of [data size][zeros size].
        out.data.assign(imageSize, 0);
        if (compression == 0)
            memcpy(&out.data[0], &body[0], body.size() < imageSize ? body.size() : imageSize);
        else
        {
            size_t from = 0, to = 0;
            for (u32 o = 8; o + 8 <= fmtSize && fmt + o + 8 <= size; o += 8)
            {
                u32 data = Be32(file + fmt + o), zeros = Be32(file + fmt + o + 4);
                if (from + data > body.size() || to + data + zeros > imageSize)
                {
                    error = "XEX basic block out of range";
                    return false;
                }
                if (data)
                    memcpy(&out.data[to], &body[from], data);
                from += data;
                to += data + zeros;
            }
        }
    }
    else
    {
        // Hash-chained blocks: [next size][next SHA-1] then [u16 len][chunk]... until len 0.
        std::vector<u8> stream;
        u32 window = Be32(file + fmt + 8), blockSize = Be32(file + fmt + 12);
        size_t pos = 0;
        while (blockSize)
        {
            if (pos + blockSize > body.size() || blockSize < 26)
            {
                error = "XEX block out of range";
                return false;
            }
            const u8* blk = &body[pos];
            u32 next = Be32(blk);
            size_t p = 24;
            for (;;)
            {
                if (p + 2 > blockSize)
                {
                    error = "XEX chunk out of range";
                    return false;
                }
                u16 len = Be16(blk + p);
                p += 2;
                if (len == 0)
                    break;
                if (p + len > blockSize)
                {
                    error = "XEX chunk out of range";
                    return false;
                }
                stream.insert(stream.end(), blk + p, blk + p + len);
                p += len;
            }
            pos += blockSize;
            blockSize = next;
        }
        unsigned bits = 0;
        while ((1u << bits) < window)
            bits++;
        out.data.assign(imageSize, 0);
        if (!Lzx(&stream[0], stream.size(), bits, &out.data[0], imageSize, error))
            return false;
    }
    // A PE image; or a resource-only module's, which is its resources as they
    // are (1888's huduiskin.xex: a XUI package, "XUIZ", from its first byte).
    bool pe = imageSize >= 2 && out.data[0] == 'M' && out.data[1] == 'Z';
    bool resources = imageSize >= 4 && !memcmp(&out.data[0], "XUIZ", 4);
    if (!pe && !resources)
    {
        error = "XEX image isn't a PE image (the wrong key?)";
        return false;
    }
    return true;
}

bool Load(const u8* file, size_t size, Image& out, std::string& error)
{
    if (size < 24 || memcmp(file, "XEX2", 4) != 0)
    {
        error = "not an XEX2 file";
        return false;
    }
    const u32 dataOff = Be32(file + 8), secOff = Be32(file + 16), count = Be32(file + 20);
    if (24 + count * 8 > size || secOff + 0x160 > size || dataOff > size)
    {
        error = "XEX header out of range";
        return false;
    }
    u32 fmt = 0, res = 0;
    for (u32 i = 0; i < count; i++)
    {
        u32 key = Be32(file + 24 + i * 8), value = Be32(file + 28 + i * 8);
        if (key == 0x3FF) fmt = value;
        if (key == 0x2FF) res = value;
    }
    if (!fmt || fmt + 36 > size)
    {
        error = "XEX has no file format header";
        return false;
    }
    const u16 encryption = Be16(file + fmt + 4), compression = Be16(file + fmt + 6);
    if (compression > 2)
    {
        error = "XEX compression type isn't none, basic or normal (LZX)";
        return false;
    }
    out.loadAddress = Be32(file + secOff + 0x110);
    // Retail-signed first (the system's files); homebrew is often signed with
    // the devkit's all-zero key instead (Aurora's plugins; trainers).
    if (!Unpack(file, size, dataOff, secOff, fmt, kRetailKey, out, error))
    {
        static const u8 kDevkitKey[16] = { 0 };
        std::string again;
        if (encryption != 1 || !Unpack(file, size, dataOff, secOff, fmt, kDevkitKey, out, again))
            return false;
        error.clear();                  // (the retail key's complaint)
    }

    out.resources.clear();
    if (res && res + 4 <= size)
    {
        u32 rsize = Be32(file + res);
        for (u32 o = 4; o + 16 <= rsize && res + o + 16 <= size; o += 16)
        {
            const u8* e = file + res + o;
            Resource r;
            r.name.assign((const char*)e, strnlen((const char*)e, 8));
            r.offset = Be32(e + 8) - out.loadAddress;
            r.size = Be32(e + 12);
            if ((size_t)r.offset + r.size <= out.data.size())
                out.resources.push_back(r);
        }
    }
    return true;
}

}  // namespace xex
