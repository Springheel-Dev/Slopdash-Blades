#include "platform/resources.h"

#include <xtl.h>
#include <stdio.h>
#include <map>
#include <set>
#include <vector>

#include "core/pak.h"
#include "core/xex.h"
#include "core/xma.h"
#include "core/xur.h"
#include "core/xzp.h"
#include "platform/library_settings.h"
#include "platform/log.h"

namespace {

struct Package
{
    std::wstring name;
    const unsigned char* data;
    unsigned size;
    std::set<std::wstring> files;   // lower case
    std::map<std::wstring, std::pair<unsigned, unsigned> > where;   // lower-case name -> offset, size
};

xex::Image g_dash;
std::vector<unsigned char> g_sharedRes;
std::vector<xex::Image*> g_modules;         // a 1888 theme's dash.xex, xam.xex (kept for the run)
std::vector<unsigned char> g_pack;          // or its theme pack (core/pak.h), kept for the run
std::vector<pak::Entry> g_packList;
std::vector<Package> g_packages;
std::map<std::wstring, size_t> g_owner;     // lower-case file -> package holding it

std::wstring Lower(const std::wstring& s)
{
    std::wstring out(s);
    for (size_t i = 0; i < out.size(); i++)
        if (out[i] >= L'A' && out[i] <= L'Z')
            out[i] = (wchar_t)(out[i] + 32);
    return out;
}

bool ReadAll(const char* path, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    out.resize(size);
    BOOL ok = size == 0 || (ReadFile(h, &out[0], size, &got, NULL) && got == size);
    CloseHandle(h);
    return ok != FALSE;
}

void AddPackage(const std::wstring& name, const unsigned char* data, unsigned size)
{
    std::vector<xzp::Entry> list;
    if (!xzp::List(data, size, list))
        return;
    Package p;
    p.name = name;
    p.data = data;
    p.size = size;
    for (size_t i = 0; i < list.size(); i++)
    {
        p.files.insert(Lower(list[i].name));
        p.where[Lower(list[i].name)] = std::make_pair(list[i].offset, list[i].size);
    }
    g_packages.push_back(p);
    const size_t index = g_packages.size() - 1;
    for (std::set<std::wstring>::const_iterator f = p.files.begin(); f != p.files.end(); ++f)
        g_owner.insert(std::make_pair(*f, index));      // first package holding a name wins
}

std::wstring Locator(const Package& p)
{
    wchar_t buf[64];
    _snwprintf_s(buf, 64, _TRUNCATE, L"memory://%X,%X#", (unsigned)(UINT_PTR)p.data, p.size);
    return buf;
}

const Package* Find(const std::wstring& name)
{
    std::wstring want = Lower(name);
    if (want == L"shrdres")             // xam's own name for the shared images
        want = L"sharedres";
    for (size_t i = 0; i < g_packages.size(); i++)
        if (Lower(g_packages[i].name) == want)
            return &g_packages[i];
    return NULL;
}

struct UpgradeCount
{
    unsigned scenes, failed, blocks;
};

// Adds one of a module's resource packages. With `custom`, every scene in it is
// upgraded from 2.0.1888's layout (core/xur.h) first, in place.
void AddModulePackage(const std::string& resource, unsigned char* data, unsigned size,
                      const std::vector<xur::CustomClass>* custom, const wchar_t* alias, UpgradeCount& n)
{
    std::string err;
    if (custom)
    {
        std::vector<xzp::Entry> list;
        if (xzp::List(data, size, list))
            for (size_t e = 0; e < list.size(); e++)
            {
                unsigned char* d = data + list[e].offset;
                if (!xur::IsScene(d, list[e].size))
                    continue;
                xur::UpgradeStats st;
                n.scenes++;
                if (xur::Upgrade1888(d, list[e].size, *custom, &st, err))
                    n.blocks += st.controlBlocks;
                else if (++n.failed <= 10)
                    Log("  %s#%S: not upgraded: %s", resource.c_str(), list[e].name.c_str(), err.c_str());
            }
    }
    // xam's shared images are the scenes' "sharedres://".
    std::wstring name(resource.begin(), resource.end());
    if (*alias)
        name = alias;
    AddPackage(name == L"shrdres" ? L"sharedres" : name, data, size);
}

// Reads and unpacks one system module and adds its XUI packages (with
// `custom`, their scenes upgraded: AddModulePackage).
// `alias`: the name to give the module's one package (the system's satellite
// modules all call theirs "media", "skin"...); empty to keep its own names.
bool LoadModule(const std::string& path, const std::vector<xur::CustomClass>* custom, const wchar_t* alias = L"")
{
    DWORD t0 = GetTickCount();
    std::vector<unsigned char> file;
    if (!ReadAll(path.c_str(), file))
    {
        Log("can't read %s (%u)", path.c_str(), (unsigned)GetLastError());
        return false;
    }
    DWORD t1 = GetTickCount();
    xex::Image* img = new xex::Image;
    std::string err;
    if (!xex::Load(&file[0], file.size(), *img, err))
    {
        Log("can't unpack %s: %s", path.c_str(), err.c_str());
        delete img;
        return false;
    }
    std::vector<unsigned char>().swap(file);
    g_modules.push_back(img);
    Log("read %s in %u ms, unpacked in %u ms: %u resources", path.c_str(), (unsigned)(t1 - t0),
        (unsigned)(GetTickCount() - t1), (unsigned)img->resources.size());
    UpgradeCount n = { 0, 0, 0 };
    for (size_t i = 0; i < img->resources.size(); i++)
    {
        const xex::Resource& r = img->resources[i];
        AddModulePackage(r.name, &img->data[r.offset], r.size, custom, alias, n);
    }
    if (custom)
        Log("  %u scenes upgraded from 1888 (%u control blocks), %u failed", n.scenes - n.failed, n.blocks, n.failed);
    return true;
}

// A module's packages from the theme pack ("<module>/<resource>"), added as
// LoadModule adds them from the module's .xex. False if the pack has none.
bool LoadPackModule(const std::string& module, const std::vector<xur::CustomClass>* custom, const wchar_t* alias = L"")
{
    const std::string prefix = module + "/";
    UpgradeCount n = { 0, 0, 0 };
    unsigned found = 0;
    for (size_t i = 0; i < g_packList.size(); i++)
    {
        const pak::Entry& e = g_packList[i];
        if (e.name.compare(0, prefix.size(), prefix) != 0)
            continue;
        AddModulePackage(e.name.substr(prefix.size()), &g_pack[e.offset], e.size, custom, alias, n);
        found++;
    }
    if (!found)
        return false;
    Log("%s: %u packages from the theme pack", module.c_str(), found);
    if (custom)
        Log("  %u scenes upgraded from 1888 (%u control blocks), %u failed", n.scenes - n.failed, n.blocks, n.failed);
    return true;
}

// One of the theme pack's entries added as a package under `as`.
bool AddPackEntry(const char* name, const wchar_t* as)
{
    const pak::Entry* e = pak::Find(g_packList, name);
    if (!e)
    {
        Log("no %s in the theme pack", name);
        return false;
    }
    AddPackage(as, &g_pack[e->offset], e->size);
    return true;
}

// The Guide and xam's other screens, each its own module: optional (a missing
// one's screens just don't open).
const char* const kModules[] = { "hud", "huduiskin", "signin", "createprofile", "gamerprofile", "friends",
                                 "voicemail", "quickchat", "feedback", "vk", "deviceselector",
                                 "minimediaplayer", "marketplace", "updater" };

// Another dashboard's dash.xex, unpacked once; some of its packages added as
// they are (no upgrade: 6717's scenes are already in the layout the runtime
// reads), each under the name given.
xex::Image* LoadOther(const char* path)
{
    std::vector<unsigned char> file;
    if (!ReadAll(path, file))
    {
        Log("can't read %s (%u)", path, (unsigned)GetLastError());
        return NULL;
    }
    xex::Image* img = new xex::Image;
    std::string err;
    if (!xex::Load(&file[0], file.size(), *img, err))
    {
        Log("can't unpack %s: %s", path, err.c_str());
        delete img;
        return NULL;
    }
    g_modules.push_back(img);
    return img;
}

bool AddFrom(xex::Image* img, const char* name, const wchar_t* as)
{
    for (size_t i = 0; img && i < img->resources.size(); i++)
        if (img->resources[i].name == name)
        {
            AddPackage(as, &img->data[img->resources[i].offset], img->resources[i].size);
            return true;
        }
    Log("no package %s in 6717's dash.xex", name);
    return false;
}

}  // namespace

bool LoadDashboard1888(const std::string& folder, const std::vector<xur::CustomClass>& custom, const char* dash6717)
{
    // 6717's five blades (dashmain.xur, with the Marketplace
    // blade, blademp) with 1888's blade contents; its skin for the four visuals
    // only it has (the Marketplace blade's), loaded under its own prefix
    // (main.cpp). The blades first: the first package with a name is the one found.
    // For comparing: with game:\dashmain1888.test present,
    // 1888's own dashmain.xur (its four blades) instead of 6717's.
    bool own = GetFileAttributesA("game:\\dashmain1888.test") != (DWORD)-1;
    if (own)
        Log("test: 1888's own dashmain (game:\\dashmain1888.test)");
    // Later: 1888's own tab strip with the Marketplace blade
    // added (tools/dashmain5.py builds <folder>\dashmain5.xzp from 1888's
    // dashmain.xur): its dashmain first, 6717's Marketplace content (blademp)
    // and skin still.
    static std::vector<unsigned char> five;
    bool strip5 = !own && ReadAll((folder + "\\dashmain5.xzp").c_str(), five) && !five.empty();
    if (strip5)
    {
        AddPackage(L"dashmain", &five[0], (unsigned)five.size());
        Log("1888's tab strip with the Marketplace blade (dashmain5.xzp, %u bytes)", (unsigned)five.size());
    }
    xex::Image* other = own ? NULL : LoadOther(dash6717);
    if (!other || (!strip5 && !AddFrom(other, "dashmain", L"dashmain")) || !AddFrom(other, "blademp", L"blademp"))
        Log("6717's blades not found (%s): 1888's four", dash6717);
    if (!LoadModule(folder + "\\dash.xex", &custom))
        return false;
    // Its skin after 1888's packages: their files stay the ones found by name.
    if (other)
        AddFrom(other, "dashuisk", L"dashuisk6717");
    if (!LoadModule(folder + "\\xam.xex", &custom))
        Log("no xam.xex in %s: gamer cards, notifications and shared images won't show", folder.c_str());
    for (size_t i = 0; i < sizeof(kModules) / sizeof(kModules[0]); i++)
    {
        std::string file = folder + "\\" + kModules[i] + ".xex";
        std::string name(kModules[i]);
        if (GetFileAttributesA(file.c_str()) != (DWORD)-1)
            LoadModule(file, &custom, std::wstring(name.begin(), name.end()).c_str());
        else
            Log("no %s: its screens won't open", file.c_str());
    }
    Log("%u packages, %u file names indexed", (unsigned)g_packages.size(), (unsigned)g_owner.size());
    return true;
}

bool LoadDashboard1888Pack(const char* path, const std::vector<xur::CustomClass>& custom)
{
    DWORD t0 = GetTickCount();
    std::string err;
    if (!ReadAll(path, g_pack) || g_pack.empty())
    {
        Log("can't read %s (%u)", path, (unsigned)GetLastError());
        return false;
    }
    if (!pak::List(&g_pack[0], g_pack.size(), g_packList, err))
    {
        Log("can't read %s: %s", path, err.c_str());
        return false;
    }
    Log("read %s in %u ms: %u entries, %u bytes", path, (unsigned)(GetTickCount() - t0), (unsigned)g_packList.size(),
        (unsigned)g_pack.size());
    // As LoadDashboard1888, in its order: the tab strip (or, for the test,
    // 1888's own), 6717's Marketplace blade, 1888's dash, 6717's skin, xam,
    // then the other modules.
    bool own = GetFileAttributesA("game:\\dashmain1888.test") != (DWORD)-1;
    if (own)
        Log("test: 1888's own dashmain (game:\\dashmain1888.test)");
    bool strip5 = !own && AddPackEntry("dashmain5.xzp", L"dashmain");
    if (strip5)
        Log("1888's tab strip with the Marketplace blade (dashmain5.xzp)");
    if (!own && ((!strip5 && !AddPackEntry("6717/dashmain", L"dashmain")) || !AddPackEntry("6717/blademp", L"blademp")))
        Log("6717's blades not found in the pack: 1888's four");
    if (!LoadPackModule("dash", &custom))
    {
        Log("no dash packages in %s", path);
        return false;
    }
    if (!own)
        AddPackEntry("6717/dashuisk", L"dashuisk6717");
    if (!LoadPackModule("xam", &custom))
        Log("no xam in %s: gamer cards, notifications and shared images won't show", path);
    for (size_t i = 0; i < sizeof(kModules) / sizeof(kModules[0]); i++)
    {
        std::string name(kModules[i]);
        if (!LoadPackModule(name, &custom, std::wstring(name.begin(), name.end()).c_str()))
            Log("no %s in the theme pack: its screens won't open", kModules[i]);
    }
    Log("%u packages, %u file names indexed (%u ms)", (unsigned)g_packages.size(), (unsigned)g_owner.size(),
        (unsigned)(GetTickCount() - t0));
    return true;
}

bool ThemePackFile(const char* name, const unsigned char** data, unsigned* size)
{
    const pak::Entry* e = pak::Find(g_packList, name);
    if (!e)
        return false;
    *data = &g_pack[e->offset];
    *size = e->size;
    return true;
}

bool LoadDashboard(const char* dashPath, const char* sharedResPath)
{
    DWORD t0 = GetTickCount();
    std::vector<unsigned char> file;
    if (!ReadAll(dashPath, file))
    {
        Log("can't read %s (%u)", dashPath, (unsigned)GetLastError());
        return false;
    }
    DWORD t1 = GetTickCount();
    std::string err;
    if (!xex::Load(&file[0], file.size(), g_dash, err))
    {
        Log("can't unpack %s: %s", dashPath, err.c_str());
        return false;
    }
    std::vector<unsigned char>().swap(file);
    Log("read %s in %u ms, unpacked in %u ms: %u resources", dashPath, (unsigned)(t1 - t0),
        (unsigned)(GetTickCount() - t1), (unsigned)g_dash.resources.size());
    for (size_t i = 0; i < g_dash.resources.size(); i++)
    {
        const xex::Resource& r = g_dash.resources[i];
        AddPackage(std::wstring(r.name.begin(), r.name.end()), &g_dash.data[r.offset], r.size);
    }
    if (ReadAll(sharedResPath, g_sharedRes) && !g_sharedRes.empty())
        AddPackage(L"sharedres", &g_sharedRes[0], (unsigned)g_sharedRes.size());
    else
        Log("can't read %s: shared images won't show", sharedResPath);
    Log("%u packages, %u file names indexed", (unsigned)g_packages.size(), (unsigned)g_owner.size());
    return true;
}

bool PackageLocator(const wchar_t* package, std::wstring& out)
{
    const Package* p = Find(package);
    if (!p)
        return false;
    out = Locator(*p);
    return true;
}

unsigned char* PackageFileForEdit(const wchar_t* package, const wchar_t* file, unsigned* size)
{
    const Package* p = Find(package);
    if (!p)
        return NULL;
    std::map<std::wstring, std::pair<unsigned, unsigned> >::const_iterator w = p->where.find(Lower(file));
    if (w == p->where.end() || w->second.first + w->second.second > p->size)
        return NULL;
    if (size)
        *size = w->second.second;
    // (Every package's bytes are SlopDash's own copy: the unpacked images.)
    return const_cast<unsigned char*>(p->data) + w->second.first;
}

namespace {
bool ResolveInPackages(const wchar_t* locator, std::wstring& out);

// Sounds converted to XMA2 (core/xma.h): each as a one-file package of its own,
// made the first time XUI asks for it. Source file's bytes -> that package, in
// physical memory, 2048-aligned with the file placed so its packets are too
// (the XMA hardware decoder reads whole aligned packets; ToXma2 starts them
// 2048 bytes into the file).
struct SoundPackage
{
    unsigned char* data;
    unsigned size;
};
std::map<const unsigned char*, SoundPackage> g_soundPackages;
const unsigned kSoundAlign = 2048;

bool IsOurSound(unsigned addr)
{
    for (std::map<const unsigned char*, SoundPackage>::const_iterator i = g_soundPackages.begin();
         i != g_soundPackages.end(); ++i)
        if ((unsigned)(UINT_PTR)i->second.data == addr)
            return true;
    return false;
}

// A four-character tag. (Not insert("RIFF", "RIFF" + 4): the XDK's compiler
// keeps each literal its own copy, so that range ran backwards and asked
// for 4 GB - 0.0.111's sound test died there.)
void PutTag(std::vector<unsigned char>& o, const char* tag)
{
    o.insert(o.end(), (const unsigned char*)tag, (const unsigned char*)tag + 4);
}

void PutBe(std::vector<unsigned char>& o, unsigned v, int bytes)
{
    for (int i = bytes - 1; i >= 0; i--)
        o.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}

// An XMA1 sound's locator -> its XMA2 copy's; false for anything else.
bool SoundLocator(const wchar_t* locator, std::wstring& out)
{
    std::wstring s(locator);
    size_t hash = s.rfind(L'#');
    std::wstring file = Lower(hash == std::wstring::npos ? s.substr(s.rfind(L'/') + 1) : s.substr(hash + 1));
    if (file.size() < 4 || file.compare(file.size() - 4, 4, L".xma") != 0)
        return false;
    if (s.compare(0, 9, L"memory://") == 0)
    {
        unsigned addr = 0;
        swscanf_s(s.c_str() + 9, L"%X", &addr);
        if (IsOurSound(addr))
            return false;                   // already the converted one
    }
    // The file's bytes: where the locator points, or where it resolves to.
    const unsigned char* data = NULL;
    unsigned size = 0;
    std::wstring resolved;
    if (!LocatorBytes(locator, &data, &size) &&
        !(ResolveInPackages(locator, resolved) && LocatorBytes(resolved.c_str(), &data, &size)))
        return false;
    if (!xma::IsXma1(data, size))
        return false;
    std::map<const unsigned char*, SoundPackage>::iterator have = g_soundPackages.find(data);
    if (have == g_soundPackages.end())
    {
        std::vector<unsigned char> sound;
        if (!xma::ToXma2(data, size, sound))
        {
            Log("sound %S: not converted", file.c_str());
            return false;
        }
        // A version 1 XUIZ package (as tools/xzp.py builds): header, one
        // directory entry {size, offset, name length, UTF-16 name}, then the
        // file at the next 2048 boundary (its offset in the entry counts from
        // the directory's end).
        unsigned dirSize = 4 + 4 + 1 + 2 * (unsigned)file.size();
        unsigned start = (22 + dirSize + kSoundAlign - 1) / kSoundAlign * kSoundAlign;
        std::vector<unsigned char> dir;
        PutBe(dir, (unsigned)sound.size(), 4);
        PutBe(dir, start - 22 - dirSize, 4);
        PutBe(dir, (unsigned)file.size(), 1);
        for (size_t i = 0; i < file.size(); i++)
            PutBe(dir, file[i], 2);
        std::vector<unsigned char> pkg;
        PutTag(pkg, "XUIZ");
        PutBe(pkg, 1, 4);
        PutBe(pkg, start + (unsigned)sound.size(), 4);
        PutBe(pkg, 0, 4);
        PutBe(pkg, (unsigned)dir.size(), 4);
        PutBe(pkg, 1, 2);
        pkg.insert(pkg.end(), dir.begin(), dir.end());
        pkg.resize(start, 0);
        pkg.insert(pkg.end(), sound.begin(), sound.end());
        SoundPackage sp;
        sp.size = (unsigned)pkg.size();
        sp.data = (unsigned char*)XPhysicalAlloc(sp.size, MAXULONG_PTR, kSoundAlign, PAGE_READWRITE);
        if (!sp.data)
        {
            Log("sound %S: no memory", file.c_str());
            return false;
        }
        memcpy(sp.data, &pkg[0], sp.size);
        have = g_soundPackages.insert(std::make_pair(data, sp)).first;
        Log("sound %S: XMA1 -> XMA2 (%u -> %u bytes) at %08X", file.c_str(), size, (unsigned)sound.size(),
            (unsigned)(UINT_PTR)sp.data);
    }
    wchar_t buf[64];
    _snwprintf_s(buf, 64, _TRUNCATE, L"memory://%X,%X#", (unsigned)(UINT_PTR)have->second.data, have->second.size);
    out = std::wstring(buf) + file;
    return true;
}
}  // namespace

// On (heard right on the console, 2026-10-17), with a guard: the first tests
// died (0.0.111: ToXma2 asked for 4 GB; 0.0.113 turned the console off), and a
// start that dies plays its blade sounds at once, so it would again at every
// start. Each start leaves game:\sounds.starting until SoundsSettled (30 s in);
// a start that finds it there runs one time without converted sounds.
// game:\sounds.off: off.
static volatile bool kConvertSounds = false;
const char* const kSoundsMarker = "game:\\sounds.starting";

void SoundTestArm()
{
    if (GetFileAttributesA("game:\\sounds.off") != (DWORD)-1)
        Log("sounds: off (sounds.off)");
    else if (GetFileAttributesA(kSoundsMarker) != (DWORD)-1)
    {
        DeleteFileA(kSoundsMarker);
        Log("sounds: the last start ended within 30 s: converted sounds off this time");
    }
    else
    {
        HANDLE h = CreateFileA(kSoundsMarker, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        kConvertSounds = true;
        Log("sounds: converted sounds on");
    }
}

void SoundsSettled()
{
    DeleteFileA(kSoundsMarker);
}

bool ResolveLocator(const wchar_t* locator, std::wstring& out)
{
    if (kConvertSounds && SoundLocator(locator, out))
        return true;
    return ResolveInPackages(locator, out);
}

namespace {
bool ResolveInPackages(const wchar_t* locator, std::wstring& out)
{
    std::wstring s(locator);
    const Package* p = NULL;
    std::wstring rest;
    if (s.compare(0, 9, L"common://") == 0)
    {
        p = Find(L"dashcomm");
        rest = s.substr(9);
    }
    else if (s.compare(0, 12, L"sharedres://") == 0)
    {
        p = Find(L"sharedres");
        rest = s.substr(12);
    }
    else if (s.compare(0, 10, L"section://") == 0)
    {
        size_t comma = s.find(L','), hash = s.find(L'#');
        if (comma == std::wstring::npos)
            return false;
        p = Find(s.substr(comma + 1, hash == std::wstring::npos ? std::wstring::npos : hash - comma - 1));
        rest = hash == std::wstring::npos ? L"" : s.substr(hash + 1);
        if (!p)
            Log("locator %S: no such package", locator);
    }
    else if (s.compare(0, 9, L"memory://") == 0)
    {
        // A scene in one package naming a file that lives in another.
        size_t hash = s.find(L'#');
        if (hash == std::wstring::npos || hash + 1 >= s.size())
            return false;
        rest = s.substr(hash + 1);
        // Offline: Games Library's home screen is Xbox LIVE (store, friends,
        // banners); go straight to the list of installed games instead.
        // The 6717 theme's choice only: the 1888 theme stays as it shipped.
        if (g_modules.empty() && Lower(rest) == L"2500_livearcadehome.xur")
            rest = L"2502_TitleSelectorScene.xur";
        // Blades (launch): Played Games is the Games Library.
        if (!g_modules.empty() && Lower(rest) == L"408_gameslist.xur")
            rest = L"2502_TitleSelectorScene.xur";
        unsigned addr = 0;
        swscanf_s(s.c_str() + 9, L"%X", &addr);
        std::wstring file = Lower(rest);
        for (size_t i = 0; i < g_packages.size(); i++)
            if ((unsigned)(UINT_PTR)g_packages[i].data == addr)
            {
                if (g_packages[i].files.count(file))
                    return false;                       // it's there: nothing to do
                break;
            }
        std::map<std::wstring, size_t>::const_iterator owner = g_owner.find(file);
        if (owner == g_owner.end())
        {
            Log("locator %S: %S isn't in any package", locator, rest.c_str());
            return false;
        }
        p = &g_packages[owner->second];
        Log("locator %S -> package %S", rest.c_str(), p->name.c_str());
    }
    if (!p)
        return false;
    out = Locator(*p) + rest;
    return true;
}
}  // namespace

bool LocatorBytes(const wchar_t* locator, const unsigned char** data, unsigned* size)
{
    std::wstring s(locator);
    size_t hash = s.find(L'#');
    if (s.compare(0, 9, L"memory://") != 0 || hash == std::wstring::npos)
        return false;
    unsigned addr = 0;
    swscanf_s(s.c_str() + 9, L"%X", &addr);
    std::wstring file = Lower(s.substr(hash + 1));
    for (size_t i = 0; i < g_packages.size(); i++)
        if ((unsigned)(UINT_PTR)g_packages[i].data == addr)
        {
            std::map<std::wstring, std::pair<unsigned, unsigned> >::const_iterator w = g_packages[i].where.find(file);
            if (w == g_packages[i].where.end())
                return false;
            *data = g_packages[i].data + w->second.first;
            *size = w->second.second;
            return true;
        }
    return false;
}
