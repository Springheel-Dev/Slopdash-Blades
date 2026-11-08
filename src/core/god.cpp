#include "core/god.h"

#include <string.h>

namespace god {

namespace {

typedef unsigned char u8;

const size_t kBlock = 0x1000, kPerFile = 0xA1C4, kPerGroup = 0xCC;

unsigned Le32(const u8* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

struct Volume
{
    ReadFn read;
    void* context;
    unsigned long long shift;   // the image's bytes left out before data block 0

    // n bytes of the disc image at x
    bool Read(unsigned long long x, size_t n, u8* out) const
    {
        while (n)
        {
            if (x < shift)
                return false;
            unsigned long long block = (x - shift) / kBlock;
            int file = (int)(block / kPerFile);
            size_t k = (size_t)(block % kPerFile);
            size_t offset = kBlock + (k / kPerGroup) * (kPerGroup + 1) * kBlock + kBlock + (k % kPerGroup) * kBlock +
                            (size_t)((x - shift) % kBlock);
            size_t take = kBlock - (size_t)((x - shift) % kBlock);
            if (take > n)
                take = n;
            if (!read(context, file, offset, take, out))
                return false;
            x += take;
            out += take;
            n -= take;
        }
        return true;
    }
};

bool IsVolume(const Volume& v, u8* descriptor)
{
    return v.Read(0x10000, 0x800, descriptor) && !memcmp(descriptor, "MICROSOFT*XBOX*MEDIA", 20);
}

// A root file, whole (at most limit bytes), or with head its first limit bytes.
bool ReadRootPart(ReadFn read, void* context, const char* name, size_t limit, bool head, std::vector<unsigned char>& out)
{
    Volume v = { read, context, 0 };
    u8 descriptor[0x800];
    if (!IsVolume(v, descriptor))
    {
        v.shift = 0x10000;              // (the enhanced layout: god.h)
        if (!IsVolume(v, descriptor))
            return false;
    }
    unsigned rootSector = Le32(descriptor + 20), rootSize = Le32(descriptor + 24);
    if (!rootSize || rootSize > 0x100000)
        return false;
    std::vector<u8> dir(rootSize);
    if (!v.Read((unsigned long long)rootSector * 0x800, rootSize, &dir[0]))
        return false;
    size_t want = strlen(name);
    for (size_t i = 0; i + 14 <= dir.size();)
    {
        if (dir[i] == 0xFF && dir[i + 1] == 0xFF)
        {
            i = (i / 0x800 + 1) * 0x800;    // (the sector's padding)
            continue;
        }
        unsigned sector = Le32(&dir[i + 4]), size = Le32(&dir[i + 8]);
        size_t n = dir[i + 13];
        if (i + 14 + n > dir.size())
            break;
        bool same = n == want;
        for (size_t k = 0; same && k < n; k++)
        {
            char a = (char)dir[i + 14 + k], b = name[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            same = a == b;
        }
        if (same)
        {
            if (!size || (!head && size > limit))
                return false;
            out.resize(head && size > limit ? limit : size);
            return v.Read((unsigned long long)sector * 0x800, out.size(), &out[0]);
        }
        i = (i + 14 + n + 3) & ~(size_t)3;
    }
    return false;
}

}  // namespace

bool ReadRootFile(ReadFn read, void* context, const char* name, size_t limit, std::vector<unsigned char>& out)
{
    return ReadRootPart(read, context, name, limit, false, out);
}

bool ReadRootFileHead(ReadFn read, void* context, const char* name, size_t n, std::vector<unsigned char>& out)
{
    return ReadRootPart(read, context, name, n, true, out);
}

}  // namespace god
