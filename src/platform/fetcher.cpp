#include "platform/fetcher.h"

#include <xtl.h>
#include <xui.h>
#include <d3dx9.h>
#include <ctype.h>
#include <stdio.h>

#include "core/png.h"
#include "platform/https.h"
#include "platform/log.h"
#include "platform/net.h"
#include "platform/scanner.h"

namespace {

struct Entry
{
    bool hasDetails, hasCover, hasWrap, hasIcon, hasIconJpg;
    bool noIcon;                        // the icon service has none for it ("noicon"): not asked again
    bool hasShot, noShot;               // its screenshot (shot.jpg; the Launch view's), or none to have ("noshot")
    catalog::Details details;
    std::vector<unsigned char> icon;    // a PNG (icon.png); XboxUnity's homebrew icons are JPEGs (icon.jpg)
    Entry() : hasDetails(false), hasCover(false), hasWrap(false), hasIcon(false), hasIconJpg(false), noIcon(false),
              hasShot(false), noShot(false) {}

    // The catalog named a screenshot (not just the box art again) not saved yet.
    bool NeedsShot() const
    {
        return details.found && !details.screenshotUrl.empty() && !hasShot && !noShot;
    }

    // Looked up, and nothing left to fetch: the catalog's details and their
    // cover (or none to have), or for a title the catalog doesn't know, the
    // second look made. (Entries from before covers carry no cover URL, and
    // from before the second look no "second": one more lookup each.)
    bool Complete() const
    {
        if (!hasDetails)
            return false;
        if (details.found && details.source.empty())
            return hasCover || details.coverUrl == "none";
        return details.second && (!details.found || hasCover || hasWrap || details.coverUrl == "none");
    }
};

CRITICAL_SECTION g_lock;
bool g_lockReady = false;
std::vector<Entry> g_entries;       // parallel to AllGames() (a scope change leaves it be)
std::vector<std::string> g_keys;    // each one's cache folder name
HANDLE g_worker = NULL;
volatile LONG g_stop = 0;           // StopFetcher: the worker ends after its current title
volatile LONG g_learned = 0;        // the worker found official names the library doesn't show yet

const wchar_t kWrapPrefix[] = L"slopdash://wrapcover/";

// A cache folder per title. An executable without a title ID (homebrew, apps,
// emulators) gets one of its own, a number made from its path without the
// drive (the same app on the hard drive or the USB stick: one entry).
std::string Key(const library::Game& g)
{
    char b[16];
    if (g.titleId)
    {
        _snprintf_s(b, sizeof(b), _TRUNCATE, "%08X", g.titleId);
        return b;
    }
    unsigned h = 2166136261u;
    size_t colon = g.path.find(':');
    for (size_t i = colon == std::string::npos ? 0 : colon + 1; i < g.path.size(); i++)
    {
        h ^= (unsigned char)tolower((unsigned char)g.path[i]);
        h *= 16777619u;
    }
    _snprintf_s(b, sizeof(b), _TRUNCATE, "HB%06X", h & 0xFFFFFF);
    return b;
}

std::string Dir(const std::string& key)
{
    return "game:\\cache\\" + key;
}

bool ReadFileAll(const std::string& path, std::vector<unsigned char>& out)
{
    out.clear();
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    out.resize(size);
    BOOL ok = size == 0 || (ReadFile(h, &out[0], size, &got, NULL) && got == size);
    CloseHandle(h);
    return ok != FALSE;
}

bool WriteFileAll(const std::string& path, const void* data, size_t size)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, (DWORD)size, &written, NULL) && written == size;
    CloseHandle(h);
    return ok != FALSE;
}

bool Exists(const std::string& path)
{
    return GetFileAttributesA(path.c_str()) != (DWORD)-1;
}

// The image Xbox's icon service sends for a title it doesn't know: a white X
// on grey (1736 bytes, CRC32 734CB788; seen for homebrew, 2026-10-14). Not an
// icon: the list shows nothing rather than it.
bool IsPlaceholderIcon(const std::vector<unsigned char>& png)
{
    if (png.size() != 1736)
        return false;
    unsigned crc = 0xFFFFFFFF;
    for (size_t i = 0; i < png.size(); i++)
    {
        crc ^= png[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
    }
    return ~crc == 0x734CB788;
}

// Loads one game's cache entry into e. True if the title has been looked up before.
bool LoadCached(const std::string& key, Entry& e)
{
    std::string dir = Dir(key);
    std::vector<unsigned char> text;
    if (!ReadFileAll(dir + "\\info.txt", text))
        return false;
    e.hasDetails = catalog::Deserialize(std::string(text.begin(), text.end()), e.details);
    e.hasCover = Exists(dir + "\\cover.jpg");
    e.hasWrap = !e.hasCover && Exists(dir + "\\wrap.jpg");
    e.hasIconJpg = Exists(dir + "\\icon.jpg");
    e.noIcon = Exists(dir + "\\noicon");
    e.hasShot = Exists(dir + "\\shot.jpg");
    e.noShot = Exists(dir + "\\noshot");
    std::vector<unsigned char> icon;
    if (ReadFileAll(dir + "\\icon.png", icon) && png::IsPng(icon.empty() ? NULL : &icon[0], icon.size()) &&
        !IsPlaceholderIcon(icon))
    {
        e.icon = icon;
        e.hasIcon = true;
    }
    return true;
}

// A GET over http or https; true with a 2xx response's body.
bool Get(const std::string& url, std::vector<unsigned char>& body, std::string& error, size_t maxBytes)
{
    int status = 0;
    error.clear();
    return HttpsGet(url, body, status, error, maxBytes);
}

std::string Text(const std::vector<unsigned char>& body)
{
    return std::string(body.begin(), body.end());
}

std::string Narrow(const std::wstring& w)
{
    std::string s;
    for (size_t i = 0; i < w.size(); i++)
        s += w[i] < 128 ? (char)w[i] : ' ';
    return s;
}

// The second look, for a title Microsoft's catalog doesn't know.
void SecondLook(const library::Game& g, const std::string& dir, Entry& e)
{
    catalog::Details& d = e.details;
    d.second = true;
    std::vector<unsigned char> body;
    std::string err;
    // An Original Xbox game: xemu's xdb, by title ID: its official name, a front cover.
    if (g.kind == library::kXboxOriginal && g.titleId)
    {
        std::string url = catalog::XdbInfoUrl(g.titleId);
        std::wstring name;
        if (!url.empty() && Get(url, body, err, 64 * 1024) && catalog::ParseXdbName(Text(body), name))
        {
            d.found = true;
            d.source = "xdb";
            d.title = name;
            d.coverUrl = catalog::XdbCoverUrl(g.titleId);
            d.wrap = false;
        }
        else
            Log("fetch %08X xdb: %s", g.titleId, err.empty() ? "not listed" : err.c_str());
    }
    // XboxUnity, by title ID, else by name: a cover (a whole case wrap).
    if (d.coverUrl.empty())
    {
        std::vector<catalog::UnityTitle> titles;
        int pick = -1;
        if (g.titleId)
        {
            char hex[9];
            _snprintf_s(hex, sizeof(hex), _TRUNCATE, "%08X", g.titleId);
            if (Get(catalog::UnitySearchUrl(hex), body, err, 256 * 1024) && catalog::ParseUnityTitles(Text(body), titles))
                pick = catalog::PickUnityTitle(titles, g.titleId, L"");
        }
        if (pick < 0 && !g.name.empty() && Get(catalog::UnitySearchUrl(Narrow(g.name)), body, err, 256 * 1024) &&
            catalog::ParseUnityTitles(Text(body), titles))
            pick = catalog::PickUnityTitle(titles, 0, g.name);
        if (pick >= 0 && titles[pick].covers > 0 &&
            Get(catalog::UnityCoversUrl(titles[pick].id), body, err, 256 * 1024))
        {
            std::string cover = catalog::UnityCoverUrl(Text(body));
            if (!cover.empty())
            {
                d.found = true;
                if (d.source.empty())
                    d.source = "unity";
                d.coverUrl = cover;
                d.wrap = true;
            }
        }
        // Its icon, for homebrew without one of its own (Media > Applications'
        // list): XboxUnity's, a JPEG (or a PNG that isn't its placeholder).
        if (pick >= 0 && g.icon.empty() && !e.hasIcon &&
            Get(catalog::UnityIconUrl(titles[pick].id), body, err, 256 * 1024) && body.size() > 4)
        {
            if (body[0] == 0xFF && body[1] == 0xD8)
                e.hasIconJpg = WriteFileAll(dir + "\\icon.jpg", &body[0], body.size());
            else if (png::IsPng(&body[0], body.size()) && !IsPlaceholderIcon(body))
            {
                WriteFileAll(dir + "\\icon.png", &body[0], body.size());
                e.icon = body;
                e.hasIcon = true;
            }
        }
        if (pick < 0)
            Log("fetch %S: not on XboxUnity%s%s", g.name.c_str(), err.empty() ? "" : ": ", err.c_str());
    }
}

// A game whose icon is still to fetch: none of its own we can use (missing,
// blank in the header - many GOD conversions keep the sizes but not the
// pictures - or damaged), none fetched, and the service not asked in vain.
bool NeedsIcon(const library::Game& g, const Entry& e)
{
    if (e.hasIcon || e.hasIconJpg || e.noIcon || !g.titleId)
        return false;
    png::Image local;
    std::string perr;
    return g.icon.empty() || !png::Decode(&g.icon[0], g.icon.size(), local, perr);
}

// The icon from the icon service, by title ID. Its placeholder (or no picture)
// means it has none: "noicon", so it isn't asked again; a failed request is
// tried again on the next start.
void FetchIcon(const library::Game& g, const std::string& dir, Entry& e)
{
    std::vector<unsigned char> body;
    std::string err;
    if (!HttpGet(catalog::IconUrl(g.titleId), body, err, 256 * 1024))
    {
        Log("fetch %08X icon: %s", g.titleId, err.c_str());
        return;
    }
    if (body.size() > 8 && png::IsPng(&body[0], body.size()) && !IsPlaceholderIcon(body))
    {
        WriteFileAll(dir + "\\icon.png", &body[0], body.size());
        e.icon = body;
        e.hasIcon = true;
    }
    else
    {
        WriteFileAll(dir + "\\noicon", "", 0);
        e.noIcon = true;
        Log("fetch %08X icon: the service has none", g.titleId);
    }
}

// The catalog's screenshot, for the Launch view: shot.jpg
// beside cover.jpg. When the catalog had only the box art again: "noshot".
void FetchShot(const std::string& dir, Entry& e)
{
    if (e.details.screenshotUrl == e.details.coverUrl)
    {
        WriteFileAll(dir + "\\noshot", "", 0);
        e.noShot = true;
        return;
    }
    std::vector<unsigned char> body;
    std::string err;
    if (Get(e.details.screenshotUrl, body, err, 2 * 1024 * 1024) && body.size() > 4)
    {
        WriteFileAll(dir + "\\shot.jpg", &body[0], body.size());
        e.hasShot = true;
    }
    else
        Log("fetch screenshot %s: %s", e.details.screenshotUrl.c_str(), err.c_str());
}

void FetchOne(size_t index, const library::Game& g, const std::string& key, const Entry& cached)
{
    std::string dir = Dir(key);
    CreateDirectoryA("game:\\cache", NULL);
    CreateDirectoryA(dir.c_str(), NULL);
    Entry e;
    if (cached.hasIcon)
    {
        e.icon = cached.icon;               // already fetched once
        e.hasIcon = true;
    }
    e.hasIconJpg = cached.hasIconJpg;
    e.noIcon = cached.noIcon;
    std::string err;
    std::vector<unsigned char> body;

    // The icon, when the game's own package has none we can use (missing, or
    // damaged: several XBLA packages here went through a text-mode copy).
    if (NeedsIcon(g, e))
        FetchIcon(g, dir, e);

    // Description, developer, publisher... and a screenshot: Microsoft's
    // catalog, for a game with a title ID it hasn't already said no to.
    bool catalogSaidNo = cached.hasDetails && !cached.details.found && cached.details.source.empty();
    if (g.titleId && g.place == kPlaceGames && !catalogSaidNo)
    {
        if (HttpGet(catalog::CatalogUrl(g.titleId), body, err, 1024 * 1024))
            catalog::ParseCatalog(std::string(body.begin(), body.end()), e.details);
        else
            Log("fetch %08X catalog: %s", g.titleId, err.c_str());
    }
    if (!e.details.found)
        SecondLook(g, dir, e);
    // The front cover, or a case wrap
    // whose front is cropped when it's first shown.
    if (!e.details.coverUrl.empty() && e.details.coverUrl != "none")
    {
        const char* file = e.details.wrap ? "\\wrap.jpg" : "\\cover.jpg";
        // (XboxUnity's large case wraps run past 1 MB: GTA San Andreas's)
        if (Get(e.details.coverUrl, body, err, 4 * 1024 * 1024) && body.size() > 4)
        {
            WriteFileAll(dir + file, &body[0], body.size());
            (e.details.wrap ? e.hasWrap : e.hasCover) = true;
        }
        else
            Log("fetch %s cover: %s", key.c_str(), err.c_str());
    }
    else if (e.details.found)
        e.details.coverUrl = "none";        // no cover anywhere: don't ask again
    if (e.NeedsShot())
        FetchShot(dir, e);
    // Written last, and always (found=0 too): this title has been looked up.
    std::string info = catalog::Serialize(e.details);
    WriteFileAll(dir + "\\info.txt", info.data(), info.size());
    e.hasDetails = true;
    Log("fetched %s %S: %s%s%s%s%S", key.c_str(), g.name.c_str(),
        !e.details.found ? "found nowhere" : e.details.source.empty() ? "details" : e.details.source.c_str(),
        e.hasCover ? ", cover" : "", e.hasWrap ? ", case wrap" : "", e.hasIcon || e.hasIconJpg ? ", icon" : "",
        e.details.title.empty() ? L"" : (L", named " + e.details.title).c_str());
    if (!e.details.title.empty() && e.details.title != g.name)
        InterlockedExchange(&g_learned, 1);

    EnterCriticalSection(&g_lock);
    g_entries[index] = e;
    LeaveCriticalSection(&g_lock);
}

DWORD WINAPI Worker(LPVOID)
{
    const std::vector<library::Game>& games = AllGames();
    unsigned fetched = 0;
    for (size_t i = 0; i < games.size() && !g_stop; i++)
    {
        EnterCriticalSection(&g_lock);
        Entry cached = g_entries[i];
        std::string key = g_keys[i];
        LeaveCriticalSection(&g_lock);
        bool complete = cached.Complete();
        if (complete && !NeedsIcon(games[i], cached) && !cached.NeedsShot())
            continue;
        if (fetched == 0 && !NetStartup())
        {
            Log("fetcher: no network; using the cache only");
            return 0;
        }
        if (complete)
        {
            // Looked up already, its icon (a request that failed before) or its
            // screenshot (looked up before screenshots were kept) still missing.
            Entry e = cached;
            CreateDirectoryA(Dir(key).c_str(), NULL);
            if (NeedsIcon(games[i], e))
                FetchIcon(games[i], Dir(key), e);
            if (e.NeedsShot())
                FetchShot(Dir(key), e);
            if (e.hasIcon)
                Log("fetched %s %S: icon", key.c_str(), games[i].name.c_str());
            EnterCriticalSection(&g_lock);
            g_entries[i] = e;
            LeaveCriticalSection(&g_lock);
        }
        else
            FetchOne(i, games[i], key, cached);
        fetched++;
    }
    Log("fetcher: done (%u titles looked up)", fetched);
    return 0;
}

}  // namespace

void StartFetcher()
{
    if (!g_lockReady)
    {
        InitializeCriticalSection(&g_lock);
        g_lockReady = true;
    }
    g_stop = 0;
    const std::vector<library::Game>& games = AllGames();
    g_entries.assign(games.size(), Entry());
    g_keys.resize(games.size());
    std::vector<std::wstring> names(games.size());
    unsigned cached = 0;
    for (size_t i = 0; i < games.size(); i++)
    {
        g_keys[i] = Key(games[i]);
        if (LoadCached(g_keys[i], g_entries[i]) && g_entries[i].Complete() && !NeedsIcon(games[i], g_entries[i]))
            cached++;
        names[i] = g_entries[i].details.title;      // (Original Xbox games' official names, from xdb)
    }
    // The official names the second look found: the library shows them (and
    // sorts by them); the entries follow the games to their new places.
    std::vector<size_t> from = RenameGames(names);
    std::vector<Entry> entries(games.size());
    std::vector<std::string> keys(games.size());
    for (size_t i = 0; i < from.size(); i++)
    {
        entries[i] = g_entries[from[i]];
        keys[i] = g_keys[from[i]];
    }
    g_entries.swap(entries);
    g_keys.swap(keys);
    Log("fetcher: %u of %u titles cached", cached, (unsigned)games.size());
    if (cached < games.size())
    {
        g_worker = CreateThread(NULL, 64 * 1024, Worker, NULL, CREATE_SUSPENDED, NULL);
        if (g_worker)
        {
            XSetThreadProcessor(g_worker, 4);   // away from the UI's hardware thread
            ResumeThread(g_worker);
        }
    }
}

bool TakeLearnedNames()
{
    if (!g_learned || (g_worker && WaitForSingleObject(g_worker, 0) != WAIT_OBJECT_0))
        return false;
    InterlockedExchange(&g_learned, 0);
    return true;
}

// The library asks by its own index (Games(), the current scope).
bool GameDetails(size_t game, catalog::Details& out)
{
    game = AllIndex(game);
    EnterCriticalSection(&g_lock);
    const Entry* e = game < g_entries.size() ? &g_entries[game] : NULL;
    bool ok = e && e->hasDetails && e->details.found && e->details.source.empty();
    if (ok)
        out = e->details;
    LeaveCriticalSection(&g_lock);
    return ok;
}

std::wstring CoverLocator(size_t game)
{
    game = AllIndex(game);
    EnterCriticalSection(&g_lock);
    bool cover = game < g_entries.size() && g_entries[game].hasCover;
    bool wrap = game < g_entries.size() && g_entries[game].hasWrap;
    std::string key = game < g_keys.size() ? g_keys[game] : "";
    LeaveCriticalSection(&g_lock);
    std::wstring k(key.begin(), key.end());
    if (cover)
        return L"file://game:/cache/" + k + L"/cover.jpg";
    if (wrap)
        return kWrapPrefix + k;
    return L"";
}

bool IsWrapCoverLocator(LPCWSTR name)
{
    return name && !wcsncmp(name, kWrapPrefix, ARRAYSIZE(kWrapPrefix) - 1);
}

HRESULT WrapCoverTexture(IXuiDevice* device, LPCWSTR name, XUIImageInfo* info, IDirect3DTexture9** tex)
{
    std::string key;
    for (LPCWSTR p = name + ARRAYSIZE(kWrapPrefix) - 1; *p; p++)
        key += *p < 128 ? (char)*p : '?';
    if (!device || key.empty() || key.find_first_of("\\/:.?") != std::string::npos)
        return E_FAIL;
    std::string dir = Dir(key);
    std::vector<unsigned char> data;
    if (!ReadFileAll(dir + "\\wrap.jpg", data) || data.empty())
        return E_FAIL;
    D3DXIMAGE_INFO ii;
    HRESULT hr = D3DXGetImageInfoFromFileInMemory(&data[0], (UINT)data.size(), &ii);
    if (FAILED(hr) || ii.Width < 16 || ii.Height < 16)
    {
        Log("wrap cover %s: %08X", key.c_str(), (unsigned)hr);
        return FAILED(hr) ? hr : E_FAIL;
    }
    // The front: the wrap's right-hand side, past the spine (a case's insert:
    // back 129 mm, spine 14, front 129, so the front starts 53% across; a
    // little more, to leave the spine out).
    RECT src = { (LONG)(ii.Width * 0.535f), 0, (LONG)ii.Width, (LONG)ii.Height };
    UINT h = ii.Height < 512 ? ii.Height : 512;
    UINT w = (UINT)((src.right - src.left) * h / ii.Height);
    DWORD t0 = GetTickCount();
    hr = D3DXCreateTexture(device->GetD3DDevice(), w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, tex);
    if (SUCCEEDED(hr))
    {
        IDirect3DSurface9* surface = NULL;
        hr = (*tex)->GetSurfaceLevel(0, &surface);
        if (SUCCEEDED(hr))
        {
            hr = D3DXLoadSurfaceFromFileInMemory(surface, NULL, NULL, &data[0], (UINT)data.size(), &src,
                                                 D3DX_FILTER_LINEAR, 0, NULL);
            surface->Release();
        }
        if (FAILED(hr))
        {
            (*tex)->Release();
            *tex = NULL;
        }
    }
    if (FAILED(hr))
    {
        Log("wrap cover %s (%ux%u): crop failed %08X", key.c_str(), ii.Width, ii.Height, (unsigned)hr);
        return hr;
    }
    // Kept as the title's cover: cropped only this once.
    LPD3DXBUFFER saved = NULL;
    HRESULT hs = D3DXSaveTextureToFileInMemory(&saved, D3DXIFF_JPG, *tex, NULL);
    bool kept = SUCCEEDED(hs) && saved &&
                WriteFileAll(dir + "\\cover.jpg", saved->GetBufferPointer(), saved->GetBufferSize());
    if (saved)
        saved->Release();
    if (kept)
    {
        DeleteFileA((dir + "\\wrap.jpg").c_str());
        EnterCriticalSection(&g_lock);
        for (size_t i = 0; i < g_keys.size() && i < g_entries.size(); i++)
            if (g_keys[i] == key)
            {
                g_entries[i].hasCover = true;
                g_entries[i].hasWrap = false;
            }
        LeaveCriticalSection(&g_lock);
    }
    Log("wrap cover %s: %ux%u front of %ux%u in %u ms, %s %08X", key.c_str(), w, h, ii.Width, ii.Height,
        (unsigned)(GetTickCount() - t0), kept ? "kept" : "not kept", (unsigned)hs);
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->Width = w;
        info->Height = h;
        info->Depth = 1;
        info->MipLevels = 1;
        info->ImageFileFormat = ii.ImageFileFormat;
        info->Format = D3DFMT_A8R8G8B8;
    }
    return S_OK;
}

std::wstring ShotLocator(size_t game)
{
    game = AllIndex(game);
    EnterCriticalSection(&g_lock);
    bool has = game < g_entries.size() && g_entries[game].hasShot;
    std::string key = game < g_keys.size() ? g_keys[game] : "";
    LeaveCriticalSection(&g_lock);
    return has ? L"file://game:/cache/" + std::wstring(key.begin(), key.end()) + L"/shot.jpg" : L"";
}

std::wstring FetchedIconFile(size_t game)
{
    game = AllIndex(game);
    EnterCriticalSection(&g_lock);
    bool has = game < g_entries.size() && g_entries[game].hasIconJpg;
    std::string key = game < g_keys.size() ? g_keys[game] : "";
    LeaveCriticalSection(&g_lock);
    return has ? L"file://game:/cache/" + std::wstring(key.begin(), key.end()) + L"/icon.jpg" : L"";
}

bool FetchedIcon(size_t game, std::vector<unsigned char>& png)
{
    game = AllIndex(game);
    EnterCriticalSection(&g_lock);
    bool ok = game < g_entries.size() && g_entries[game].hasIcon;
    if (ok)
        png = g_entries[game].icon;
    LeaveCriticalSection(&g_lock);
    return ok;
}

bool StopFetcher()
{
    if (!g_worker)
        return true;
    InterlockedExchange(&g_stop, 1);
    // At most one request in flight (each times out after 15 s).
    if (WaitForSingleObject(g_worker, 40000) != WAIT_OBJECT_0)
    {
        Log("fetcher: still busy after 40 s");
        return false;
    }
    Log("fetcher: stopped");
    CloseHandle(g_worker);
    g_worker = NULL;
    return true;
}
