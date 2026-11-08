#include "core/xzp.h"

#include <string.h>

namespace xzp {

namespace {

unsigned Be32(const unsigned char* p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

}  // namespace

bool List(const unsigned char* data, size_t size, std::vector<Entry>& out)
{
    out.clear();
    if (size < 22 || memcmp(data, "XUIZ", 4) != 0)
        return false;
    const unsigned header = Be32(data + 16);
    const unsigned count = ((unsigned)data[20] << 8) | data[21];
    const size_t base = 22 + (size_t)header;
    size_t p = 22;
    for (unsigned i = 0; i < count; i++)
    {
        if (p + 9 > size)
            return false;
        Entry e;
        e.size = Be32(data + p);
        unsigned off = Be32(data + p + 4);
        unsigned len = data[p + 8];
        p += 9;
        if (p + 2 * (size_t)len > size)
            return false;
        for (unsigned c = 0; c < len; c++)
            e.name += (wchar_t)((data[p + 2 * c] << 8) | data[p + 2 * c + 1]);
        p += 2 * (size_t)len;
        if (base + off + (size_t)e.size > size)
            return false;
        e.offset = (unsigned)(base + off);
        out.push_back(e);
    }
    return true;
}

}  // namespace xzp
