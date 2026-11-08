#include "core/library.h"

#include <ctype.h>
#include <wctype.h>
#include <stdio.h>
#include <string.h>

#include "core/png.h"
#include "core/xex.h"
#include "core/xpr.h"

namespace library {

namespace {

typedef unsigned char u8;

unsigned Be32(const u8* p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
unsigned Be16(const u8* p) { return ((unsigned)p[0] << 8) | p[1]; }
unsigned Le32(const u8* p) { return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24); }

// A fixed-size UTF-16 field, up to the first NUL, trailing spaces trimmed.
std::wstring Utf16(const u8* p, size_t bytes, bool bigEndian)
{
    std::wstring s;
    for (size_t i = 0; i + 1 < bytes; i += 2)
    {
        wchar_t c = bigEndian ? (wchar_t)((p[i] << 8) | p[i + 1]) : (wchar_t)(p[i] | (p[i + 1] << 8));
        if (!c)
            break;
        s += c;
    }
    while (!s.empty() && (s[s.size() - 1] == L' ' || s[s.size() - 1] == L'\r' || s[s.size() - 1] == L'\n'))
        s.erase(s.size() - 1);
    return s;
}

bool IsPngAt(const u8* p, size_t n)
{
    return n >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8);
}

// A package header's image: its PNG at the slot, or a byte or few after it,
// mended when the tool that packed it broke it. Some Arcade packages (Castle
// Crashers, 584108B7) carry "89 50 4E 47 0D 0D 0A 1A 0A": the signature with
// an extra CR (its "\r\n" written as text), the rest whole but its size
// counting the extra byte, so its IEND one byte short; the title's own image
// sits a byte later (0x571B) for the same reason. Read as they are, they gave
// no icon at all.
bool TakePng(const u8* data, size_t size, size_t at, size_t n, std::vector<u8>& out)
{
    static const u8 kBroken[9] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0D, 0x0A, 0x1A, 0x0A };
    for (size_t start = at; start <= at + 3 && start + 8 <= size; start++)
    {
        size_t len = start + n <= size ? n : size - start;
        const u8* p = data + start;
        if (IsPngAt(p, len))
        {
            out.assign(p, p + len);
            return true;
        }
        if (len > 9 && !memcmp(p, kBroken, 9))
        {
            static const u8 kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
            out.assign(kSignature, kSignature + 8);
            out.insert(out.end(), p + 9, p + len);
            // its IEND whole again (the chunk is 12 bytes, always the same)
            for (size_t i = out.size() >= 4 ? out.size() - 4 : 0; i > 8; i--)
                if (!memcmp(&out[i], "IEND", 4))
                {
                    static const u8 kEnd[12] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
                    out.resize(i - 4);
                    out.insert(out.end(), kEnd, kEnd + 12);
                    break;
                }
            return true;
        }
    }
    return false;
}

}  // namespace

const char* KindName(Kind k)
{
    switch (k)
    {
    case kXbox360: return "Xbox 360";
    case kArcade: return "Xbox LIVE Arcade";
    case kXboxOriginal: return "Xbox";
    default: return "?";
    }
}

bool IsGameContentType(unsigned t)
{
    return t == 0x7000 || t == 0xD0000 || t == 0x5000 || t == 0x80000 /* demo */;
}

// --- STFS ------------------------------------------------------------------------
// Header fields (big endian): content type 0x344, metadata version 0x348,
// title ID 0x360, display names 0x411 (9 locales x 0x80, English first),
// description 0xD11, publisher 0x1611, title name 0x1691, thumbnail size
// 0x1712, title-thumbnail size 0x1716, thumbnail 0x171A, title thumbnail 0x571A
// (each up to 0x4000, or 0x3D00 in metadata version 2).

bool ReadStfs(const u8* data, size_t size, Game& out)
{
    if (size < 0x1000 || (memcmp(data, "CON ", 4) && memcmp(data, "LIVE", 4) && memcmp(data, "PIRS", 4)))
        return false;
    out = Game();
    out.contentType = Be32(data + 0x344);
    if (!IsGameContentType(out.contentType))
        return false;
    out.titleId = Be32(data + 0x360);
    out.kind = out.contentType == 0xD0000 ? kArcade : out.contentType == 0x5000 ? kXboxOriginal : kXbox360;
    if (size >= 0x1711)
    {
        out.name = Utf16(data + 0x411, 0x80, true);
        out.description = Utf16(data + 0xD11, 0x80 * 3, true);
        out.publisher = Utf16(data + 0x1611, 0x80, true);
        if (out.name.empty())
            out.name = Utf16(data + 0x1691, 0x80, true);
    }
    if (size >= 0x171A)
    {
        const unsigned version = Be32(data + 0x348);
        const size_t maxImage = version >= 2 ? 0x3D00 : 0x4000;
        const unsigned thumb = Be32(data + 0x1712), titleThumb = Be32(data + 0x1716);
        // The title's own tile first, then the content's thumbnail.
        if (!(titleThumb && titleThumb <= maxImage && TakePng(data, size, 0x571A, titleThumb, out.icon)) &&
            thumb && thumb <= maxImage)
            TakePng(data, size, 0x171A, thumb, out.icon);
    }
    return true;
}

bool ReadContentHeader(const u8* data, size_t size, ContentHeader& out)
{
    if (size < 0x171A || (memcmp(data, "CON ", 4) && memcmp(data, "LIVE", 4) && memcmp(data, "PIRS", 4)))
        return false;
    out = ContentHeader();
    out.contentType = Be32(data + 0x344);
    out.titleId = Be32(data + 0x360);
    out.name = Utf16(data + 0x411, 0x80, true);
    out.titleName = Utf16(data + 0x1691, 0x80, true);
    const size_t maxImage = Be32(data + 0x348) >= 2 ? 0x3D00 : 0x4000;
    const unsigned thumb = Be32(data + 0x1712), titleThumb = Be32(data + 0x1716);
    if (thumb && thumb <= maxImage)
        TakePng(data, size, 0x171A, thumb, out.icon);
    if (titleThumb && titleThumb <= maxImage)
        TakePng(data, size, 0x571A, titleThumb, out.titleIcon);
    return true;
}

bool ReadExecution(const u8* data, size_t size, Execution& out)
{
    out = Execution();
    const u8* e = NULL;
    if (size >= 0x368 && (!memcmp(data, "CON ", 4) || !memcmp(data, "LIVE", 4) || !memcmp(data, "PIRS", 4)))
        e = data + 0x354;
    else if (size >= 24 && !memcmp(data, "XEX2", 4))
    {
        const unsigned count = Be32(data + 20);
        for (unsigned i = 0; i < count && 24 + i * 8 + 8 <= size && !e; i++)
        {
            unsigned key = Be32(data + 24 + i * 8), value = Be32(data + 28 + i * 8);
            if (key == 0x40006 && value + 0x14 <= size)
                e = data + value;
        }
    }
    if (!e)
        return false;
    out.mediaId = Be32(e);
    out.version = Be32(e + 4);
    out.baseVersion = Be32(e + 8);
    out.titleId = Be32(e + 12);
    out.discNum = e[18];
    out.discCount = e[19];
    return true;
}

int TitleUpdateNumber(const std::string& name)
{
    // "tu" + 8 hex digits + "_" + 8 hex digits, any case.
    if (name.size() != 19 || (name[0] != 't' && name[0] != 'T') || (name[1] != 'u' && name[1] != 'U') ||
        name[10] != '_')
        return -1;
    unsigned n = 0;
    for (size_t i = 2; i < 19; i++)
    {
        if (i == 10)
            continue;
        if (!isxdigit((unsigned char)name[i]))
            return -1;
        if (i < 10)
            n = n * 16 + (isdigit((unsigned char)name[i]) ? name[i] - '0' : (tolower((unsigned char)name[i]) - 'a' + 10));
    }
    return n > 0x7FFFFFFF ? -1 : (int)n;
}

bool ReadTitleUpdate(const u8* data, size_t size, TitleUpdate& out)
{
    out = TitleUpdate();
    if (size < 0x491 || (memcmp(data, "LIVE", 4) && memcmp(data, "PIRS", 4) && memcmp(data, "CON ", 4)) ||
        Be32(data + 0x344) != 0xB0000)
        return false;
    out.mediaId = Be32(data + 0x354);
    out.titleId = Be32(data + 0x360);
    std::wstring name = Utf16(data + 0x411, 0x80, true);
    size_t hash = name.rfind(L'#');
    if (hash != std::wstring::npos && hash + 1 < name.size() && iswdigit(name[hash + 1]))
    {
        int n = 0;
        for (size_t i = hash + 1; i < name.size() && iswdigit(name[i]) && n < 100000; i++)
            n = n * 10 + (name[i] - L'0');
        out.number = n;
    }
    return true;
}

bool ReadProfileHeader(const u8* data, size_t size, std::wstring& gamertag, std::vector<unsigned char>& picture)
{
    if (size < 0x171A || (memcmp(data, "CON ", 4) && memcmp(data, "LIVE", 4) && memcmp(data, "PIRS", 4)) ||
        Be32(data + 0x344) != 0x10000)
        return false;
    gamertag = Utf16(data + 0x411, 0x80, true);
    picture.clear();
    const size_t maxImage = Be32(data + 0x348) >= 2 ? 0x3D00 : 0x4000;
    const unsigned thumb = Be32(data + 0x1712);
    if (thumb && thumb <= maxImage && 0x171A + (size_t)thumb <= size && IsPngAt(data + 0x171A, thumb))
        picture.assign(data + 0x171A, data + 0x171A + thumb);
    return true;
}

// --- XBE ---------------------------------------------------------------------------
// "XBEH"; base address 0x104 and certificate address 0x118 (virtual, little
// endian); certificate: title ID +0x8, title name +0xC (40 UTF-16LE characters).

bool ReadXbe(const u8* data, size_t size, Game& out)
{
    if (size < 0x178 || memcmp(data, "XBEH", 4))
        return false;
    out = Game();
    const unsigned base = Le32(data + 0x104), cert = Le32(data + 0x118);
    if (cert < base || cert - base + 0xC + 80 > size)
        return false;
    const u8* c = data + (cert - base);
    out.kind = kXboxOriginal;
    out.titleId = Le32(c + 0x8);
    out.name = Utf16(c + 0xC, 80, false);
    return true;
}

// The section headers (count 0x11C, address 0x120; 0x38 bytes each: flags,
// virtual address and size, raw address +0xC and size +0x10, name address
// +0x14) and their names are in the header, which the first 64 KB hold.
bool XbeTitleImage(const u8* data, size_t size, size_t& offset, size_t& length)
{
    if (size < 0x178 || memcmp(data, "XBEH", 4))
        return false;
    const unsigned base = Le32(data + 0x104), count = Le32(data + 0x11C), table = Le32(data + 0x120);
    if (table < base || count > 256 || table - base + (size_t)count * 0x38 > size)
        return false;
    for (unsigned i = 0; i < count; i++)
    {
        const u8* h = data + (table - base) + i * 0x38;
        unsigned name = Le32(h + 0x14);
        if (name < base || name - base + 10 > size)
            continue;
        if (!memcmp(data + (name - base), "$$XTIMAGE", 10))
        {
            offset = Le32(h + 0xC);
            length = Le32(h + 0x10);
            return length >= 32 && length <= 0x100000;
        }
    }
    return false;
}

bool IconFromXpr(const u8* data, size_t size, std::vector<u8>& icon)
{
    unsigned w = 0, h = 0;
    std::vector<u8> rgba;
    if (!xpr::Decode(data, size, w, h, rgba))
        return false;
    png::Encode(w, h, &rgba[0], icon);
    return true;
}

// --- XDBF (SPA) ----------------------------------------------------------------------
// "XDBF", version, entry-table length, entry count, free-table length, free
// count; entries are {u16 namespace, u64 id, u32 offset, u32 length}, the data
// after both tables. Namespace 3 = string tables by locale (id 1 = English),
// 2 = images (id 0x8000 = the title image); "XSTC" (metadata, namespace 1) names
// the default locale. A string table "XSTR": version, size, u16 count, then
// {u16 id, u16 length, UTF-8} - the title name is string 0x8000.

bool ReadXdbf(const u8* data, size_t size, std::wstring& name, std::vector<u8>& icon)
{
    name.clear();
    icon.clear();
    if (size < 24 || memcmp(data, "XDBF", 4))
        return false;
    const unsigned entryMax = Be32(data + 8), entries = Be32(data + 12), freeMax = Be32(data + 16);
    const size_t base = 24 + (size_t)entryMax * 18 + (size_t)freeMax * 8;
    if (base > size || entries > entryMax)
        return false;
    unsigned locale = 1;
    // first pass: the default locale
    for (unsigned i = 0; i < entries; i++)
    {
        const u8* e = data + 24 + i * 18;
        unsigned ns = Be16(e), off = Be32(e + 10), len = Be32(e + 14);
        if (ns == 1 && base + off + 16 <= size && !memcmp(data + base + off, "XSTC", 4) && len >= 16)
            locale = Be32(data + base + off + 12);
    }
    for (unsigned i = 0; i < entries; i++)
    {
        const u8* e = data + 24 + i * 18;
        unsigned ns = Be16(e);
        unsigned idLow = Be32(e + 6);           // the low 32 bits of the 64-bit id
        unsigned off = Be32(e + 10), len = Be32(e + 14);
        if (base + (size_t)off + len > size)
            continue;
        const u8* d = data + base + off;
        if (ns == 2 && idLow == 0x8000 && IsPngAt(d, len))
            icon.assign(d, d + len);
        else if (ns == 3 && idLow == locale && len >= 14 && !memcmp(d, "XSTR", 4))
        {
            unsigned count = Be16(d + 12);
            size_t p = 14;
            for (unsigned s = 0; s < count && p + 4 <= len; s++)
            {
                unsigned id = Be16(d + p), n = Be16(d + p + 2);
                p += 4;
                if (p + n > len)
                    break;
                if (id == 0x8000)
                {
                    // UTF-8 -> UTF-16 (BMP)
                    std::wstring w;
                    for (size_t k = p; k < p + n;)
                    {
                        unsigned ch = d[k];
                        if (ch < 0x80) { w += (wchar_t)ch; k += 1; }
                        else if ((ch & 0xE0) == 0xC0 && k + 1 < p + n) { w += (wchar_t)(((ch & 0x1F) << 6) | (d[k + 1] & 0x3F)); k += 2; }
                        else if ((ch & 0xF0) == 0xE0 && k + 2 < p + n) { w += (wchar_t)(((ch & 0x0F) << 12) | ((d[k + 1] & 0x3F) << 6) | (d[k + 2] & 0x3F)); k += 3; }
                        else k += 1;
                    }
                    name = w;
                }
                p += n;
            }
        }
    }
    return !name.empty() || !icon.empty();
}

// --- XEX -------------------------------------------------------------------------
// Title ID: execution info (optional header 0x40006, title ID at +0xC). Name
// and icon: the XDBF resource named after the title ID ("4D5307E6").

bool ReadXex(const u8* data, size_t size, Game& out)
{
    if (size < 24 || memcmp(data, "XEX2", 4))
        return false;
    out = Game();
    out.kind = kXbox360;
    const unsigned count = Be32(data + 20);
    for (unsigned i = 0; i < count && 24 + i * 8 + 8 <= size; i++)
    {
        unsigned key = Be32(data + 24 + i * 8), value = Be32(data + 28 + i * 8);
        if (key == 0x40006 && value + 0x10 <= size)
            out.titleId = Be32(data + value + 0xC);
    }
    xex::Image img;
    std::string err;
    if (xex::Load(data, size, img, err))
    {
        char want[9];
        sprintf(want, "%08X", out.titleId);
        const xex::Resource* r = img.Find(want);
        if (r)
            ReadXdbf(&img.data[r->offset], r->size, out.name, out.icon);
    }
    return out.titleId != 0;
}

int XexEsrb(const u8* data, size_t size)
{
    if (size < 24 || memcmp(data, "XEX2", 4))
        return -1;
    const unsigned count = Be32(data + 20);
    for (unsigned i = 0; i < count && 24 + i * 8 + 8 <= size; i++)
    {
        unsigned key = Be32(data + 24 + i * 8), value = Be32(data + 28 + i * 8);
        if (key == 0x40310 && value < size)
            return data[value] == 0xFF ? -1 : data[value];
    }
    return -1;
}

}  // namespace library
