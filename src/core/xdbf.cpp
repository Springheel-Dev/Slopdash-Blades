#include "core/xdbf.h"

#include <string.h>

namespace xdbf {

namespace {

unsigned Be32(const unsigned char* p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

unsigned long long Be64(const unsigned char* p)
{
    return ((unsigned long long)Be32(p) << 32) | Be32(p + 4);
}

// A 0-terminated UTF-16BE string at p (up to end); p moves past it.
std::wstring Utf16(const unsigned char*& p, const unsigned char* end)
{
    std::wstring s;
    while (p + 1 < end)
    {
        wchar_t c = (wchar_t)((p[0] << 8) | p[1]);
        p += 2;
        if (!c)
            break;
        s += c;
    }
    return s;
}

}  // namespace

bool Entries(const unsigned char* gpd, size_t size, std::vector<Entry>& out)
{
    out.clear();
    if (!gpd || size < 0x18 || memcmp(gpd, "XDBF", 4))
        return false;
    unsigned tableLen = Be32(gpd + 8), count = Be32(gpd + 12), freeLen = Be32(gpd + 16);
    size_t dataAt = 0x18 + (size_t)tableLen * 18 + (size_t)freeLen * 8;
    if (count > tableLen || dataAt > size)
        return false;
    for (unsigned i = 0; i < count; i++)
    {
        const unsigned char* e = gpd + 0x18 + (size_t)i * 18;
        Entry x;
        x.ns = (e[0] << 8) | e[1];
        x.id = Be64(e + 2);
        size_t at = dataAt + Be32(e + 10);
        x.size = Be32(e + 14);
        if (at > size || x.size > size - at)
            continue;
        x.data = gpd + at;
        out.push_back(x);
    }
    return true;
}

const Entry* Find(const std::vector<Entry>& entries, unsigned ns, unsigned long long id)
{
    for (size_t i = 0; i < entries.size(); i++)
        if (entries[i].ns == ns && entries[i].id == id)
            return &entries[i];
    return 0;
}

bool ReadTitle(const Entry& e, Title& t)
{
    if (e.ns != kTitle || e.size < 0x2A || e.id > 0xFFFFFFFFull)
        return false;
    const unsigned char* d = e.data;
    t.titleId = Be32(d);
    t.achievements = (int)Be32(d + 4);
    t.unlocked = (int)Be32(d + 8);
    t.gamerscore = (int)Be32(d + 0x0C);
    t.earned = (int)Be32(d + 0x10);
    t.lastPlayed = Be64(d + 0x20);
    const unsigned char* p = d + 0x28;
    t.name = Utf16(p, d + e.size);
    return t.titleId != 0;
}

bool ReadAchievement(const Entry& e, Achievement& a)
{
    // Ids above 32 bits are the file's sync records, not achievements; each
    // achievement starts with its own record size, 0x1C.
    if (e.ns != kAchievement || e.size < 0x1C || e.id > 0xFFFFFFFFull || Be32(e.data) != 0x1C)
        return false;
    const unsigned char* d = e.data;
    a.id = Be32(d + 4);
    a.imageId = Be32(d + 8);
    a.gamerscore = (int)Be32(d + 0x0C);
    a.flags = Be32(d + 0x10);
    a.unlocked = Be64(d + 0x14);
    const unsigned char* p = d + 0x1C;
    const unsigned char* end = d + e.size;
    a.name = Utf16(p, end);
    a.description = Utf16(p, end);
    a.lockedDescription = Utf16(p, end);
    return true;
}

static const Entry* Setting(const std::vector<Entry>& entries, unsigned id, unsigned type)
{
    for (size_t i = 0; i < entries.size(); i++)
    {
        const Entry& e = entries[i];
        if (e.ns == kSetting && (unsigned)e.id == id && e.size >= 0x18 && Be32(e.data) == id && e.data[8] == type)
            return &e;
    }
    return 0;
}

bool SettingInt(const std::vector<Entry>& entries, unsigned id, int& value)
{
    const Entry* e = Setting(entries, id, 1);
    if (!e)
        return false;
    value = (int)Be32(e->data + 0x10);
    return true;
}

bool SettingFloat(const std::vector<Entry>& entries, unsigned id, float& value)
{
    const Entry* e = Setting(entries, id, 5);
    if (!e)
        return false;
    unsigned bits = Be32(e->data + 0x10);
    memcpy(&value, &bits, 4);
    return true;
}

}  // namespace xdbf
