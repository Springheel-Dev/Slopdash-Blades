#include "platform/store.h"

#include <xtl.h>
#include <map>
#include <set>
#include <string.h>
#include <stdlib.h>

#include "core/sevenzip.h"
#include "platform/console.h"
#include "platform/https.h"
#include "platform/library_settings.h"
#include "platform/log.h"
#include "platform/settings.h"

namespace store {
namespace {

// ConsoleMods' repo, as Aurora's Homebrew Store ships it (its Repos folder):
// offered on the Manage Repos page, off until ticked there (not plumbed in).
const char kConsoleMods[] = "https://consolemods.org/wiki/images/0/0a/ConsoleModsRepo.ini";
const char kConsoleModsKey[] = "store.consolemods"; // "1": ticked
const char kRepoKey[] = "store.repo";               // settings list: the repos added
const char kRepoOffKey[] = "store.repooff";         // settings list: added repos unticked
const char kDownloads[] = "game:\\StoreDownloads";  // the downloads, while they're unpacked

CRITICAL_SECTION g_lock;
volatile LONG g_generation = 1;
bool g_started = false;

void Changed()
{
    InterlockedIncrement(&g_generation);
}

struct Lock
{
    Lock() { EnterCriticalSection(&g_lock); }
    ~Lock() { LeaveCriticalSection(&g_lock); }
};

std::wstring Widen(const std::string& s)
{
    // UTF-8 (the listings are ASCII almost everywhere).
    std::wstring w;
    for (size_t i = 0; i < s.size();)
    {
        unsigned char c = (unsigned char)s[i];
        unsigned cp = c;
        size_t n = 1;
        if (c >= 0xF0 && i + 3 < s.size())
            cp = ((c & 7) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F), n = 4;
        else if (c >= 0xE0 && i + 2 < s.size())
            cp = ((c & 15) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F), n = 3;
        else if (c >= 0xC0 && i + 1 < s.size())
            cp = ((c & 31) << 6) | (s[i + 1] & 0x3F), n = 2;
        w += cp > 0xFFFF ? L'?' : (wchar_t)cp;
        i += n;
    }
    return w;
}

std::string Lower(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = (char)(s[i] - 'A' + 'a');
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n' || (unsigned char)s[a] == 0xEF ||
                     (unsigned char)s[a] == 0xBB || (unsigned char)s[a] == 0xBF))
        a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
        b--;
    return s.substr(a, b - a);
}

// An .ini: [section] then key=value lines; # and ; start comments. Sections
// in file order, keys lower-cased.
typedef std::map<std::string, std::string> Section;
typedef std::vector<std::pair<std::string, Section> > Ini;

Ini ParseIni(const std::vector<unsigned char>& body)
{
    Ini ini;
    std::string text(body.begin(), body.end());
    size_t at = 0;
    while (at < text.size())
    {
        size_t nl = text.find('\n', at);
        std::string line = Trim(text.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
        at = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line[0] == '[')
        {
            size_t close = line.find(']');
            ini.push_back(std::make_pair(Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos
                                                                                               : close - 1))),
                                         Section()));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || ini.empty())
            continue;
        ini.back().second[Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
    }
    return ini;
}

std::string Get(const Section& s, const char* key)
{
    Section::const_iterator it = s.find(key);
    return it == s.end() ? std::string() : it->second;
}

// --- where things install ----------------------------------------------------
// As Aurora's store does with no scan paths set (Main.lua GetDestinationPath),
// on the hard drive, with one change: homebrew games go in Games, where the
// Game Library looks (Aurora: Homebrew). Absolute paths keep their drive when
// SlopDash can write there; Aurora's own folder (Game:) and the system
// partition (Hddx:) can't be.
std::string DriveFor(const std::string& path, std::wstring& blocked, bool& aurora)
{
    std::string low = Lower(path);
    if (!low.compare(0, 5, "game:"))
    {
        aurora = true;
        blocked = L"Cannot Download. Aurora Only";     // (Aurora's own folder)
        return "";
    }
    if (!low.compare(0, 5, "hddx:"))
    {
        blocked = L"Cannot Download. System Partition";
        return "";
    }
    static const char* const kDrives[][2] = { { "hdd1:", "hdd:" }, { "hdd:", "hdd:" }, { "usb0:", "usb:" },
                                              { "usb1:", "usb:" }, { "usb:", "usb:" } };
    for (int i = 0; i < 5; i++)
    {
        size_t n = strlen(kDrives[i][0]);
        if (!low.compare(0, n, kDrives[i][0]))
            return kDrives[i][1] + path.substr(n);
    }
    blocked = L"Cannot Download. Unknown Location";
    return "";
}

std::string Destination(const std::string& type, const std::string& path, std::wstring& blocked, bool& aurora)
{
    std::string t = Lower(type);
    if (path.find(':') != std::string::npos)            // a drive of its own ("Usb0:\...", "Game:\...")
        return DriveFor(path, blocked, aurora);
    if (t == "app")
        return "hdd:\\Apps\\" + path;
    if (t == "homebrew" || t == "game")
        return "hdd:\\Games\\" + path;
    if (t == "emulator")
        return "hdd:\\Emulators\\" + path;
    if (t == "publicprofile")
        return "hdd:\\Content\\0000000000000000\\" + path;
    if (t == "profile")
    {
        for (DWORD u = 0; u < 4; u++)
        {
            XUID x = 0;
            if (XUserGetSigninState(u) != eXUserSigninState_NotSignedIn && XUserGetXUID(u, &x) == ERROR_SUCCESS)
            {
                char b[40];
                _snprintf_s(b, sizeof(b), _TRUNCATE, "hdd:\\Content\\%016I64X\\", x);
                return b + path;
            }
        }
        blocked = L"Cannot Download. Sign In First";
        return "";
    }
    return DriveFor(path, blocked, aurora);
}

// The folder a path ends in: up to its last backslash (as Aurora's store).
std::string FolderOf(const std::string& path)
{
    size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? path : path.substr(0, slash + 1);
}

Shelf ShelfFor(const std::string& key, const std::string& type)
{
    std::string t = Lower(type);
    if (key == "dashboards")
        return kApps;                       // (no Dashboards shelf)
    if (t == "slopdashtheme")
        return kThemes;
    if (t == "app")
        return kApps;
    if (t == "homebrew" || t == "game")
        return kGames;
    if (t == "emulator")
        return kEmulators;
    return kOther;
}

// --- reading the repos ---------------------------------------------------------
struct Category
{
    std::string key, url, type;
    std::wstring name;
    Shelf shelf;
};

struct RepoState
{
    RepoState() : read(false), failed(false) {}
    bool read, failed;
    std::wstring name, error;
    std::vector<Category> categories;
};

std::map<std::string, RepoState> g_repos;               // by URL
std::map<std::string, std::vector<Item> > g_categoryItems;   // by category URL
std::map<std::string, std::wstring> g_categoryErrors;
bool g_loading[kShelfCount];
std::set<int> g_wanted;                                  // shelves to (re)load
HANDLE g_fetchEvent = NULL;

bool Listed(const std::vector<std::string>& list, const std::string& url)
{
    for (size_t i = 0; i < list.size(); i++)
        if (Lower(list[i]) == Lower(url))
            return true;
    return false;
}

// Every repo on the Manage Repos page, ticked or not: ConsoleMods' first, then the
// ones added.
std::vector<std::string> AllRepoUrls()
{
    std::vector<std::string> urls(1, kConsoleMods);
    std::vector<std::string> added = GetSettingList(kRepoKey);
    for (size_t i = 0; i < added.size(); i++)
        if (!added[i].empty() && !Listed(urls, added[i]))
            urls.push_back(added[i]);
    return urls;
}

bool RepoOn(const std::string& url, const std::vector<std::string>& off)
{
    if (Lower(url) == Lower(kConsoleMods))
        return GetSetting(kConsoleModsKey) == "1";
    return !Listed(off, url);
}

// The repos the shelves read: the ticked ones.
std::vector<std::string> RepoUrls()
{
    std::vector<std::string> all = AllRepoUrls(), off = GetSettingList(kRepoOffKey), urls;
    for (size_t i = 0; i < all.size(); i++)
        if (RepoOn(all[i], off))
            urls.push_back(all[i]);
    return urls;
}

std::wstring HostOf(const std::string& url)
{
    size_t a = url.find("://");
    a = a == std::string::npos ? 0 : a + 3;
    size_t b = url.find('/', a);
    return Widen(url.substr(a, b == std::string::npos ? std::string::npos : b - a));
}

bool Fetch(const std::string& url, std::vector<unsigned char>& body, std::wstring& error)
{
    int status = 0;
    std::string err;
    DWORD t0 = GetTickCount();
    bool ok = HttpsGet(url, body, status, err, 2 * 1024 * 1024);
    Log("store: %s: %s (%d, %u bytes, %u ms)%s%s", url.c_str(), ok ? "OK" : "FAILED", status, (unsigned)body.size(),
        (unsigned)(GetTickCount() - t0), err.empty() ? "" : " ", err.c_str());
    if (!ok)
    {
        wchar_t b[160];
        if (status)
            _snwprintf_s(b, 160, _TRUNCATE, L"The server answered %d.", status);
        else
            _snwprintf_s(b, 160, _TRUNCATE, L"It couldn't be reached (%S).", err.empty() ? "no network?" : err.c_str());
        error = b;
    }
    return ok;
}

void ReadRepo(const std::string& url)
{
    {
        Lock l;
        std::map<std::string, RepoState>::const_iterator it = g_repos.find(url);
        if (it != g_repos.end() && it->second.read)
            return;
    }
    std::vector<unsigned char> body;
    std::wstring error;
    RepoState r;
    r.read = true;
    r.failed = !Fetch(url, body, error);
    r.error = error;
    r.name = HostOf(url);
    if (!r.failed)
    {
        Ini ini = ParseIni(body);
        for (size_t i = 0; i < ini.size(); i++)
        {
            const Section& s = ini[i].second;
            std::string iniurl = Get(s, "iniurl");
            if (ini[i].first == "update" || iniurl.empty())
                continue;
            Category c;
            c.key = ini[i].first;
            c.url = iniurl;
            c.type = Get(s, "type");
            c.name = Widen(Get(s, "name"));
            c.shelf = ShelfFor(c.key, c.type);
            r.categories.push_back(c);
        }
        if (r.categories.empty())
        {
            r.failed = true;
            r.error = L"It isn't a repo .ini (no categories in it).";
        }
    }
    Lock l;
    g_repos[url] = r;
    Changed();
}

void ReadCategory(const Category& c, const std::wstring& repoName)
{
    {
        Lock l;
        if (g_categoryItems.count(c.url) || g_categoryErrors.count(c.url))
            return;
    }
    std::vector<unsigned char> body;
    std::wstring error;
    std::vector<Item> items;
    if (Fetch(c.url, body, error))
    {
        Ini ini = ParseIni(body);
        for (size_t i = 0; i < ini.size(); i++)
        {
            const Section& s = ini[i].second;
            Item it;
            it.title = Widen(Get(s, "itemtitle"));
            if (it.title.empty())
                continue;
            it.version = Widen(Get(s, "itemversion"));
            it.author = Widen(Get(s, "itemauthor"));
            it.size = Widen(Get(s, "itemsize"));
            it.description = Widen(Get(s, "itemdescription"));
            it.path = Get(s, "path");
            it.type = c.type;
            it.repo = c.name.empty() ? repoName : c.name;
            it.auroraOnly = false;
            std::string first = Get(s, "dataurl");
            if (!first.empty())
            {
                it.urls.push_back(first);
                it.partPaths.push_back("");
            }
            for (int n = 2; n < 100; n++)
            {
                char key[32];
                _snprintf_s(key, sizeof(key), _TRUNCATE, "dataurlpart%d", n);
                std::string u = Get(s, key);
                if (u.empty())
                    break;
                _snprintf_s(key, sizeof(key), _TRUNCATE, "part%dpath", n);
                it.urls.push_back(u);
                it.partPaths.push_back(Get(s, key));
            }
            it.destination = Destination(it.type, it.path, it.blocked, it.auroraOnly);
            for (size_t p = 0; p < it.partPaths.size() && it.blocked.empty(); p++)
                if (!it.partPaths[p].empty())
                {
                    std::wstring why;
                    bool aurora = false;
                    if (Destination(it.type, it.partPaths[p], why, aurora).empty())
                    {
                        it.blocked = why;
                        it.auroraOnly = aurora;
                    }
                }
            if (it.urls.empty() && it.blocked.empty())
                it.blocked = L"Cannot Download. No File Listed";
            if (!it.blocked.empty())
                it.destination.clear();
            items.push_back(it);
        }
    }
    Lock l;
    if (error.empty())
        g_categoryItems[c.url] = items;
    else
        g_categoryErrors[c.url] = error;
    Changed();
}

void LoadShelfNow(Shelf shelf)
{
    std::vector<std::string> urls = RepoUrls();
    for (size_t i = 0; i < urls.size(); i++)
    {
        ReadRepo(urls[i]);
        RepoState r;
        {
            Lock l;
            std::map<std::string, RepoState>::const_iterator it = g_repos.find(urls[i]);
            if (it != g_repos.end())
                r = it->second;
        }
        for (size_t c = 0; c < r.categories.size(); c++)
            if (r.categories[c].shelf == shelf)
                ReadCategory(r.categories[c], r.name);
    }
    Lock l;
    g_loading[shelf] = false;
    Changed();
}

DWORD WINAPI FetchThread(LPVOID)
{
    for (;;)
    {
        WaitForSingleObject(g_fetchEvent, INFINITE);
        for (;;)
        {
            int shelf = -1;
            {
                Lock l;
                if (g_wanted.empty())
                    break;
                shelf = *g_wanted.begin();
                g_wanted.erase(g_wanted.begin());
            }
            LoadShelfNow((Shelf)shelf);
        }
    }
}

// --- what's installed ----------------------------------------------------------
// Each install the store finishes is recorded: its folder and version
// (settings store.installed.N, "<folder>|<version>"). An item the store has
// no record of (installed before records were kept, or by hand) counts as
// installed when its own folder is there - one below the drive's Games,
// Apps... folders, as hdd:\Apps\ alone holds everyone's. Under g_lock.
std::map<std::string, std::string> g_installed;     // lower-case folder -> version (UTF-8)
std::map<std::string, bool> g_folderThere;          // folder checks, until the next install
bool g_installedRead = false;

std::string Utf8(const std::wstring& w)
{
    std::string s;
    for (size_t i = 0; i < w.size(); i++)
    {
        unsigned c = w[i];
        if (c < 0x80)
            s += (char)c;
        else if (c < 0x800)
            s += (char)(0xC0 | (c >> 6)), s += (char)(0x80 | (c & 0x3F));
        else
            s += (char)(0xE0 | (c >> 12)), s += (char)(0x80 | ((c >> 6) & 0x3F)), s += (char)(0x80 | (c & 0x3F));
    }
    return s;
}

void ReadInstalled()
{
    if (g_installedRead)
        return;
    g_installedRead = true;
    std::vector<std::string> v = GetSettingList("store.installed");
    for (size_t i = 0; i < v.size(); i++)
    {
        size_t bar = v[i].find('|');
        if (bar != std::string::npos && bar > 0)
            g_installed[Lower(v[i].substr(0, bar))] = v[i].substr(bar + 1);
    }
}

void RecordInstall(const Item& item)
{
    std::vector<std::string> list;
    {
        Lock l;
        ReadInstalled();
        g_installed[Lower(FolderOf(item.destination))] = Utf8(item.version);
        g_folderThere.clear();
        for (std::map<std::string, std::string>::const_iterator i = g_installed.begin(); i != g_installed.end(); ++i)
            list.push_back(i->first + "|" + i->second);
    }
    SetSettingList("store.installed", list);
}

// Whether a folder is an item's own: at least two below the drive
// (hdd:\Games\360sweeper\), not one of the shared ones (hdd:\Apps\).
bool OwnFolder(const std::string& folder)
{
    size_t colon = folder.find(':');
    if (colon == std::string::npos)
        return false;
    int slashes = 0;
    for (size_t i = colon; i < folder.size(); i++)
        slashes += folder[i] == '\\';
    return slashes >= 3;
}

void MarkInstalled(Item& it)
{
    it.installed = kNotInstalled;
    it.installedVersion.clear();
    if (it.destination.empty() || !it.blocked.empty())
        return;
    ReadInstalled();
    std::string folder = Lower(FolderOf(it.destination));
    std::map<std::string, std::string>::const_iterator r = g_installed.find(folder);
    if (r != g_installed.end())
    {
        it.installedVersion = Widen(r->second);
        bool differ = !r->second.empty() && !it.version.empty() && _stricmp(r->second.c_str(), Utf8(it.version).c_str());
        it.installed = differ ? kUpdateAvailable : kInstalled;
        return;
    }
    if (!OwnFolder(folder))
        return;
    std::map<std::string, bool>::const_iterator f = g_folderThere.find(folder);
    if (f == g_folderThere.end())
    {
        std::string bare = folder.substr(0, folder.size() - 1);
        f = g_folderThere.insert(std::make_pair(folder, GetFileAttributesA(bare.c_str()) != (DWORD)-1)).first;
    }
    if (f->second)
        it.installed = kInstalled;
}

// --- the download queue --------------------------------------------------------
std::vector<Job> g_jobs;
std::map<int, Item> g_jobItems;
std::set<int> g_cancel;
int g_nextJob = 1;
HANDLE g_jobEvent = NULL;

Job* FindJob(int id)
{
    for (size_t i = 0; i < g_jobs.size(); i++)
        if (g_jobs[i].id == id)
            return &g_jobs[i];
    return NULL;
}

struct ProgressContext
{
    int id;
    DWORD last;
};

bool OnProgress(void* context, unsigned long long done, unsigned long long total)
{
    ProgressContext* p = (ProgressContext*)context;
    Lock l;
    if (Job* j = FindJob(p->id))
    {
        j->done = done;
        j->total = total;
    }
    if (GetTickCount() - p->last > 400)
    {
        p->last = GetTickCount();
        Changed();
    }
    return g_cancel.count(p->id) == 0;
}

// A toast as a job moves on: downloading, unpacking, installed, or
// failed - each once a job, whatever screen is up. Short (terse-UI rule).
void JobToast(const std::wstring& title, JobState state)
{
    switch (state)
    {
    case kDownloading: console::Notify(L"Downloading " + title, false); break;
    case kUnpacking: console::Notify(L"Installing " + title, false); break;
    case kDone: console::Notify(title + L" Installed", false); break;
    case kFailed: console::Notify(title + L" Failed. See Active Downloads", true); break;
    default: break;
    }
}

void SetJob(int id, JobState state, const std::wstring& message)
{
    std::wstring title;
    bool changed = false;
    {
        Lock l;
        if (Job* j = FindJob(id))
        {
            changed = j->state != state;
            j->state = state;
            j->message = message;
            title = j->title;
        }
        Changed();
    }
    if (changed)
        JobToast(title, state);
}

std::string FileNameOf(const std::string& url)
{
    size_t slash = url.find_last_of('/');
    std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
    size_t q = name.find_first_of("?#");
    return q == std::string::npos ? name : name.substr(0, q);
}

bool IsSevenZip(const std::string& url)
{
    std::string n = Lower(FileNameOf(url));
    return n.size() > 3 && n.compare(n.size() - 3, 3, ".7z") == 0;
}

void MakeFolders(const std::string& path)
{
    for (size_t i = path.find('\\'); i != std::string::npos; i = path.find('\\', i + 1))
        CreateDirectoryA(path.substr(0, i).c_str(), NULL);
    CreateDirectoryA(path.c_str(), NULL);
}

void RunJob(int id)
{
    Item item;
    {
        Lock l;
        item = g_jobItems[id];
    }
    CreateDirectoryA(kDownloads, NULL);
    std::string dir = FolderOf(item.destination);
    for (size_t p = 0; p < item.urls.size(); p++)
    {
        std::string target = dir;
        if (!item.partPaths[p].empty())
        {
            std::wstring why;
            bool aurora = false;
            target = FolderOf(Destination(item.type, item.partPaths[p], why, aurora));
        }
        {
            bool first = false;
            std::wstring title;
            {
                Lock l;
                if (Job* j = FindJob(id))
                {
                    first = j->state != kDownloading;
                    j->state = kDownloading;
                    j->part = (int)p + 1;
                    j->done = j->total = 0;
                    title = j->title;
                }
                Changed();
            }
            if (first)
                JobToast(title, kDownloading);
        }
        char tmp[96];
        _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s\\job%d-%u%s", kDownloads, id, (unsigned)p,
                    IsSevenZip(item.urls[p]) ? ".7z" : ".bin");
        ProgressContext ctx = { id, 0 };
        int status = 0;
        std::string error;
        bool ok = HttpsDownload(item.urls[p], tmp, OnProgress, &ctx, status, error);
        Log("store job %d: %s -> %s: %s (%d)%s%s", id, item.urls[p].c_str(), tmp, ok ? "OK" : "FAILED", status,
            error.empty() ? "" : " ", error.c_str());
        bool cancelled;
        {
            Lock l;
            cancelled = g_cancel.count(id) != 0;
        }
        if (!ok)
        {
            DeleteFileA(tmp);
            wchar_t why[160];
            if (status)
                _snwprintf_s(why, 160, _TRUNCATE, L"The download failed (the server answered %d).", status);
            else
                _snwprintf_s(why, 160, _TRUNCATE, L"The download failed: %S.", error.c_str());
            SetJob(id, cancelled ? kCancelled : kFailed, cancelled ? L"Cancelled." : why);
            return;
        }
        MakeFolders(target.substr(0, target.size() && target[target.size() - 1] == '\\' ? target.size() - 1
                                                                                          : target.size()));
        if (IsSevenZip(item.urls[p]))
        {
            SetJob(id, kUnpacking, L"");
            ctx.last = 0;
            ok = sevenzip::Extract(tmp, target, OnProgress, &ctx, error);
            Log("store job %d: unpack into %s: %s%s%s", id, target.c_str(), ok ? "OK" : "FAILED",
                error.empty() ? "" : ", ", error.c_str());
            DeleteFileA(tmp);
            if (!ok)
            {
                {
                    Lock l;
                    cancelled = g_cancel.count(id) != 0;
                }
                SetJob(id, cancelled ? kCancelled : kFailed,
                       cancelled ? L"Cancelled." : L"It couldn't be unpacked: " + Widen(error) + L".");
                return;
            }
        }
        else
        {
            std::string to = target + FileNameOf(item.urls[p]);
            ok = MoveFileA(tmp, to.c_str()) || (CopyFileA(tmp, to.c_str(), FALSE) && DeleteFileA(tmp));
            Log("store job %d: %s -> %s: %s", id, tmp, to.c_str(), ok ? "OK" : "FAILED");
            if (!ok)
            {
                DeleteFileA(tmp);
                SetJob(id, kFailed, L"It couldn't be copied to " + Widen(to) + L".");
                return;
            }
        }
    }
    RecordInstall(item);
    // The libraries search again when next open: the installed game, app or
    // emulator shows (VBA360 didn't in Games > Emulators till a rescan, 2026-11-02).
    MarkLibraryChanged();
    SetJob(id, kDone, L"Installed in " + Widen(dir) + L".");
}

DWORD WINAPI JobThread(LPVOID)
{
    for (;;)
    {
        WaitForSingleObject(g_jobEvent, INFINITE);
        for (;;)
        {
            int id = -1;
            {
                Lock l;
                for (size_t i = 0; i < g_jobs.size() && id < 0; i++)
                    if (g_jobs[i].state == kQueued)
                        id = g_jobs[i].id;
            }
            if (id < 0)
                break;
            RunJob(id);
        }
    }
}

void Start()
{
    if (g_started)
        return;
    g_started = true;
    InitializeCriticalSection(&g_lock);
    g_fetchEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_jobEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    // (big stacks: BearSSL's handshake uses a few KB, the 7z reader more)
    HANDLE t = CreateThread(NULL, 256 * 1024, FetchThread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
    t = CreateThread(NULL, 512 * 1024, JobThread, NULL, 0, NULL);
    if (t)
        CloseHandle(t);
}

}  // namespace

const wchar_t* ShelfName(Shelf s)
{
    static const wchar_t* const kNames[] = { L"Homebrew Games", L"Homebrew Apps", L"Emulators", L"Dashboards",
                                             L"SlopDash Themes", L"Other" };
    return s >= 0 && s < kShelfCount ? kNames[s] : L"";
}

ShelfState GetShelf(Shelf s)
{
    Start();
    ShelfState out;
    out.updates = 0;
    Lock l;
    out.loading = g_loading[s];
    std::vector<std::string> urls = RepoUrls();
    std::wstring errors;
    for (size_t i = 0; i < urls.size(); i++)
    {
        std::map<std::string, RepoState>::const_iterator r = g_repos.find(urls[i]);
        if (r == g_repos.end())
            continue;
        if (r->second.failed)
        {
            errors += r->second.name + L": " + r->second.error + L"\r\n";
            continue;
        }
        for (size_t c = 0; c < r->second.categories.size(); c++)
        {
            const Category& cat = r->second.categories[c];
            if (cat.shelf != s)
                continue;
            std::map<std::string, std::vector<Item> >::const_iterator it = g_categoryItems.find(cat.url);
            if (it != g_categoryItems.end())
                out.items.insert(out.items.end(), it->second.begin(), it->second.end());
            std::map<std::string, std::wstring>::const_iterator e = g_categoryErrors.find(cat.url);
            if (e != g_categoryErrors.end())
                errors += cat.name + L": " + e->second + L"\r\n";
        }
    }
    if (out.items.empty())
        out.error = errors;
    for (size_t i = 0; i < out.items.size(); i++)
    {
        MarkInstalled(out.items[i]);
        out.updates += out.items[i].installed == kUpdateAvailable;
    }
    return out;
}

void LoadShelf(Shelf s, bool again)
{
    Start();
    {
        Lock l;
        if (g_loading[s])
            return;
        if (again)
        {
            g_repos.clear();
            g_categoryItems.clear();
            g_categoryErrors.clear();
        }
        else
        {
            // Everything this shelf needs already read: nothing to do.
            bool all = true;
            std::vector<std::string> urls = RepoUrls();
            for (size_t i = 0; i < urls.size() && all; i++)
            {
                std::map<std::string, RepoState>::const_iterator r = g_repos.find(urls[i]);
                if (r == g_repos.end())
                    all = false;
                else
                    for (size_t c = 0; c < r->second.categories.size() && all; c++)
                        if (r->second.categories[c].shelf == s && !g_categoryItems.count(r->second.categories[c].url) &&
                            !g_categoryErrors.count(r->second.categories[c].url))
                            all = false;
            }
            if (all)
                return;
        }
        g_loading[s] = true;
        g_wanted.insert((int)s);
    }
    Changed();
    SetEvent(g_fetchEvent);
}

std::vector<Repo> Repos()
{
    Start();
    std::vector<std::string> urls = AllRepoUrls(), off = GetSettingList(kRepoOffKey);
    std::vector<Repo> out;
    for (size_t i = 0; i < urls.size(); i++)
    {
        Repo r;
        r.url = urls[i];
        r.offered = i == 0;
        r.on = RepoOn(urls[i], off);
        out.push_back(r);
    }
    Lock l;
    for (size_t i = 0; i < out.size(); i++)
    {
        std::map<std::string, RepoState>::const_iterator it = g_repos.find(out[i].url);
        out[i].name = it != g_repos.end() ? it->second.name : HostOf(out[i].url);
    }
    return out;
}

bool AddRepo(const std::string& url)
{
    std::string low = Lower(url);
    if (low.compare(0, 7, "http://") && low.compare(0, 8, "https://"))
        return false;
    if (Listed(AllRepoUrls(), url))
        return false;
    std::vector<std::string> added = GetSettingList(kRepoKey);
    added.push_back(url);
    bool ok = SetSettingList(kRepoKey, added);
    Changed();
    return ok;
}

bool RemoveRepo(const std::string& url)
{
    std::vector<std::string> added = GetSettingList(kRepoKey), kept;
    for (size_t i = 0; i < added.size(); i++)
        if (Lower(added[i]) != Lower(url))
            kept.push_back(added[i]);
    if (kept.size() == added.size())
        return false;
    bool ok = SetSettingList(kRepoKey, kept);
    SetRepoOn(url, true);                   // (off its unticked list too)
    Changed();
    return ok;
}

bool SetRepoOn(const std::string& url, bool on)
{
    bool ok;
    if (Lower(url) == Lower(kConsoleMods))
        ok = SetSetting(kConsoleModsKey, on ? "1" : "");
    else
    {
        std::vector<std::string> off = GetSettingList(kRepoOffKey), kept;
        for (size_t i = 0; i < off.size(); i++)
            if (Lower(off[i]) != Lower(url))
                kept.push_back(off[i]);
        if (!on)
            kept.push_back(url);
        ok = SetSettingList(kRepoOffKey, kept);
    }
    Changed();
    return ok;
}

std::vector<Job> Jobs()
{
    Start();
    Lock l;
    return g_jobs;
}

int Install(const Item& item)
{
    Start();
    if (!item.blocked.empty() || item.destination.empty() || item.urls.empty())
        return -1;
    Lock l;
    for (size_t i = 0; i < g_jobs.size(); i++)
        if ((g_jobs[i].state == kQueued || g_jobs[i].state == kDownloading || g_jobs[i].state == kUnpacking) &&
            g_jobs[i].destination == item.destination && g_jobs[i].title == item.title)
            return g_jobs[i].id;            // (already on its way)
    Job j;
    j.id = g_nextJob++;
    j.title = item.title;
    j.destination = item.destination;
    j.state = kQueued;
    j.part = 0;
    j.parts = (int)item.urls.size();
    j.done = j.total = 0;
    g_jobs.push_back(j);
    g_jobItems[j.id] = item;
    Log("store: job %d: %S -> %s", j.id, item.title.c_str(), item.destination.c_str());
    Changed();
    SetEvent(g_jobEvent);
    return j.id;
}

void Cancel(int id)
{
    Lock l;
    if (Job* j = FindJob(id))
    {
        g_cancel.insert(id);
        if (j->state == kQueued)
        {
            j->state = kCancelled;
            j->message = L"Cancelled.";
        }
    }
    Changed();
}

int ActiveJobs()
{
    Start();
    Lock l;
    int n = 0;
    for (size_t i = 0; i < g_jobs.size(); i++)
        if (g_jobs[i].state == kQueued || g_jobs[i].state == kDownloading || g_jobs[i].state == kUnpacking)
            n++;
    return n;
}

unsigned Generation()
{
    return (unsigned)g_generation;
}

}  // namespace store
