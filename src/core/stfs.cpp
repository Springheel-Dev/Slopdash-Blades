#include "core/stfs.h"

#include <string.h>

namespace stfs {

namespace {

unsigned Be32(const unsigned char* p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
unsigned Be24(const unsigned char* p) { return (p[0] << 16) | (p[1] << 8) | p[2]; }
unsigned Le24(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16); }
unsigned Le16(const unsigned char* p) { return p[0] | (p[1] << 8); }

struct Package
{
    const unsigned char* d;
    size_t size;
    size_t first;           // the first block's offset
    unsigned shift;         // 0: one hash table per group; 1: two (read-write packages)

    bool Open(const unsigned char* data, size_t n)
    {
        d = data;
        size = n;
        if (n < 0x400 || (memcmp(d, "CON ", 4) && memcmp(d, "LIVE", 4) && memcmp(d, "PIRS", 4)))
            return false;
        unsigned header = Be32(d + 0x340);
        bool readOnly = (d[0x379 + 2] & 1) != 0;
        shift = (((header + 0xFFF) & 0xB000) == 0xB000 || readOnly) ? 0 : 1;
        first = (header + 0xFFF) & ~0xFFFu;
        return true;
    }

    size_t Offset(unsigned b) const
    {
        unsigned blk = b, base = 170;
        for (int i = 0; i < 3; i++)
        {
            blk += ((b + base) / base) << shift;
            if (b < base)
                break;
            base *= 170;
        }
        return first + ((size_t)blk << 12);
    }

    // The hash tables. A read-write package (shift 1) keeps two copies of each
    // table, and only one is current: which, the level above says (bit 0x40 of
    // its entry's status), and for the top table bit 1 of the volume
    // descriptor's block separation. Reading the first copy always followed
    // stale next-block links: a profile's 240 KB Assassin's Creed II GPD ran
    // off the package, wasn't read, and its 16 achievements were missing from
    // the Achievements screen (2026-11-04). The layout checked
    // against every hash in two profiles.
    unsigned Step0() const { return shift ? 0xAC : 0xAB; }
    unsigned Step1() const { return shift ? 0x723A : 0x718F; }
    unsigned Allocated() const { return Be32(d + 0x395); }
    int TopLevel() const { return Allocated() <= 170 ? 0 : Allocated() <= 28900 ? 1 : 2; }

    unsigned BackingL0(unsigned b) const
    {
        if (b < 170)
            return 0;
        unsigned r = (b / 170) * Step0();
        if (b / 28900 == 0)
            return r + (1u << shift);
        return r + (((b / 28900) + 1) << shift) + (1u << shift);
    }
    unsigned BackingL1(unsigned b) const { return b < 28900 ? Step0() : (1u << shift) + (b / 28900) * Step1(); }

    size_t Table(unsigned backing, bool second) const
    {
        return first + ((size_t)backing << 12) + (second && shift ? 4096 : 0);
    }
    size_t Top() const
    {
        int level = TopLevel();
        unsigned backing = level == 0 ? 0 : level == 1 ? BackingL1(0) : Step1();
        return Table(backing, (d[0x37B] & 2) != 0);
    }
    bool Second(size_t entry) const { return entry + 24 <= size && (d[entry + 20] & 0x40) != 0; }
    size_t L1(unsigned b) const
    {
        if (TopLevel() == 1)
            return Top();
        return Table(BackingL1(b), Second(Top() + (b / 28900) * 24));
    }
    size_t L0(unsigned b) const
    {
        if (TopLevel() == 0)
            return Top();
        return Table(BackingL0(b), Second(L1(b) + ((b / 170) % 170) * 24));
    }
    size_t HashEntry(unsigned b) const { return L0(b) + (b % 170) * 24; }

    // count blocks from start (chained through the hash tables unless contiguous).
    bool Read(unsigned start, unsigned count, bool contiguous, std::vector<unsigned char>& out) const
    {
        out.clear();
        unsigned b = start;
        for (unsigned i = 0; i < count; i++)
        {
            size_t at = Offset(b);
            if (at + 4096 > size)
                return false;
            out.insert(out.end(), d + at, d + at + 4096);
            if (contiguous)
                b++;
            else
            {
                size_t e = HashEntry(b);
                if (e + 24 > size)
                    return false;
                b = Be24(d + e + 21);
            }
        }
        return true;
    }

    struct Entry
    {
        std::string name;
        bool dir, contiguous;
        unsigned blocks, start, size;
    };

    bool Entries(std::vector<Entry>& out) const
    {
        const unsigned char* vd = d + 0x379;
        unsigned tableBlocks = Le16(vd + 3), tableStart = Le24(vd + 5);
        if (tableBlocks == 0 || tableBlocks > 4096)
            return false;
        std::vector<unsigned char> table;
        if (!Read(tableStart, tableBlocks, false, table))
            return false;
        out.clear();
        for (size_t i = 0; i + 64 <= table.size(); i += 64)
        {
            const unsigned char* e = &table[i];
            unsigned nameLen = e[0x28] & 0x3F;
            if (!nameLen)
                continue;
            Entry en;
            en.name.assign((const char*)e, nameLen);
            en.dir = (e[0x28] & 0x80) != 0;
            en.contiguous = (e[0x28] & 0x40) != 0;
            en.blocks = Le24(e + 0x29);
            en.start = Le24(e + 0x2F);
            en.size = Be32(e + 0x34);
            out.push_back(en);
        }
        return true;
    }
};

bool SameName(const std::string& a, const char* b)
{
    if (a.size() != strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y)
            return false;
    }
    return true;
}

}  // namespace

bool ListFiles(const unsigned char* data, size_t size, std::vector<std::string>& names)
{
    Package p;
    std::vector<Package::Entry> entries;
    if (!p.Open(data, size) || !p.Entries(entries))
        return false;
    names.clear();
    for (size_t i = 0; i < entries.size(); i++)
        if (!entries[i].dir)
            names.push_back(entries[i].name);
    return true;
}

bool ReadFile(const unsigned char* data, size_t size, const char* name, std::vector<unsigned char>& out)
{
    Package p;
    std::vector<Package::Entry> entries;
    if (!p.Open(data, size) || !p.Entries(entries))
        return false;
    for (size_t i = 0; i < entries.size(); i++)
    {
        const Package::Entry& e = entries[i];
        if (e.dir || !SameName(e.name, name))
            continue;
        if (!p.Read(e.start, e.blocks, e.contiguous, out) || out.size() < e.size)
            return false;
        out.resize(e.size);
        return true;
    }
    return false;
}

// XDBF: "XDBF", u32 version, u32 entry-table length, u32 entry count, u32
// free-table length, u32 free count; entries {u16 namespace, u64 id, u32
// offset, u32 length}; free entries 8 bytes; the data after both tables. A
// setting (namespace 3): u32 id, 4 bytes, u8 type, 7 bytes, then its value
// (an int32 at +0x10 for type 1).
bool GamerCardNumbers(const unsigned char* gpd, size_t size, int& gamerscore, int& played, int& earned)
{
    gamerscore = played = earned = -1;
    if (size < 0x18 || memcmp(gpd, "XDBF", 4))
        return false;
    unsigned tableLen = Be32(gpd + 8), count = Be32(gpd + 12), freeLen = Be32(gpd + 16);
    size_t dataAt = 0x18 + (size_t)tableLen * 18 + (size_t)freeLen * 8;
    if (count > tableLen || dataAt > size)
        return false;
    for (unsigned i = 0; i < count; i++)
    {
        const unsigned char* e = gpd + 0x18 + (size_t)i * 18;
        unsigned ns = (e[0] << 8) | e[1];
        unsigned idLow = Be32(e + 6);
        size_t at = dataAt + Be32(e + 10), len = Be32(e + 14);
        if (ns != 3 || at + len > size || len < 0x18)
            continue;
        const unsigned char* v = gpd + at;
        if (Be32(v) != idLow || v[8] != 1)          // type 1: int32
            continue;
        int value = (int)Be32(v + 0x10);
        // XPROFILE_GAMERCARD_CRED 6, _TITLES_PLAYED 18, _ACHIEVEMENTS_EARNED 19
        // (xonline.h); 7 and 8, read before, kept only where those are missing.
        if (idLow == 0x10040006) gamerscore = value;
        else if (idLow == 0x10040012) played = value;
        else if (idLow == 0x10040013) earned = value;
        else if (idLow == 0x10040007 && played < 0) played = value;
        else if (idLow == 0x10040008 && earned < 0) earned = value;
    }
    return true;
}

}  // namespace stfs
