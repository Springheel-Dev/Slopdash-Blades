#include "core/pak.h"

#include <stdio.h>
#include <string.h>

namespace pak {

namespace {

unsigned Be32(const unsigned char* p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

}  // namespace

bool List(const unsigned char* data, size_t size, std::vector<Entry>& out, std::string& error)
{
    out.clear();
    if (size < 12 || memcmp(data, "SDPK", 4) != 0)
    {
        error = "not a SlopDash theme pack";
        return false;
    }
    const unsigned version = Be32(data + 4);
    if (version != kVersion)
    {
        char buf[64];
        sprintf(buf, "theme pack version %u (this SlopDash reads %u)", version, kVersion);
        error = buf;
        return false;
    }
    const unsigned count = Be32(data + 8);
    size_t p = 12;
    for (unsigned i = 0; i < count; i++)
    {
        if (p + 1 > size || p + 1 + data[p] + 8 > size)
        {
            error = "theme pack's table is cut short";
            out.clear();
            return false;
        }
        Entry e;
        e.name.assign((const char*)data + p + 1, data[p]);
        p += 1 + data[p];
        e.offset = Be32(data + p);
        e.size = Be32(data + p + 4);
        p += 8;
        if ((size_t)e.offset > size || (size_t)e.size > size - e.offset)
        {
            error = "theme pack's " + e.name + " runs past its end";
            out.clear();
            return false;
        }
        out.push_back(e);
    }
    return true;
}

const Entry* Find(const std::vector<Entry>& list, const std::string& name)
{
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].name == name)
            return &list[i];
    return NULL;
}

}  // namespace pak
