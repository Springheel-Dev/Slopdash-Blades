// The game menu: A on a game in the Games Library opens it, whatever the view.
// Like Aurora's: launch it, see its achievements, screenshots, saved games,
// downloadable content and trainers, browse its files, rename it or delete it.
// List screens (system_settings.inc) on the Language screen's layout.
//
// Included by system_settings.inc, after the list screens.

std::wstring Widen(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

std::string Narrow(const std::wstring& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
        out += s[i] < 128 ? (char)s[i] : '?';
    return out;
}

std::string TitleHex(unsigned tid)
{
    char b[12];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%08X", tid);
    return b;
}

std::wstring SizeText(ULONGLONG bytes)
{
    wchar_t b[32];
    if (bytes >= 1024ull * 1024 * 1024)
        _snwprintf_s(b, 32, _TRUNCATE, L"%.2f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        _snwprintf_s(b, 32, _TRUNCATE, L"%.1f MB", bytes / (1024.0 * 1024));
    else
        _snwprintf_s(b, 32, _TRUNCATE, L"%u KB", (unsigned)((bytes + 1023) / 1024));
    return b;
}

// Files (or folders) in dir matching pattern, sorted by name.
struct FoundFile
{
    std::string name, path;
    ULONGLONG size;
    bool dir;
};

std::vector<FoundFile> FindFiles(const std::string& dir, const char* pattern = "*")
{
    std::vector<FoundFile> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return out;
    do
    {
        if (fd.cFileName[0] == '.')
            continue;
        FoundFile f;
        f.name = fd.cFileName;
        f.path = dir + "\\" + fd.cFileName;
        f.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        f.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.push_back(f);
    } while (FindNextFileA(h, &fd) && out.size() < 1000);
    FindClose(h);
    struct ByName
    {
        bool operator()(const FoundFile& a, const FoundFile& b) const { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; }
    };
    std::sort(out.begin(), out.end(), ByName());
    return out;
}

// A package's display name (STFS header, 0x411, UTF-16BE), or "" if it isn't one.
std::wstring PackageName(const std::string& path)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return L"";
    unsigned char head[0x500];
    DWORD got = 0;
    BOOL ok = ReadFile(h, head, sizeof(head), &got, NULL);
    CloseHandle(h);
    if (!ok || got < sizeof(head) || (memcmp(head, "CON ", 4) && memcmp(head, "LIVE", 4) && memcmp(head, "PIRS", 4)))
        return L"";
    std::wstring name;
    for (int i = 0x411; i + 1 < 0x491; i += 2)
    {
        wchar_t c = (wchar_t)((head[i] << 8) | head[i + 1]);
        if (!c)
            break;
        name += c;
    }
    return name;
}

// Whose content folder a path is in: "Content\<XUID>\..." -> the gamertag.
std::wstring OwnerOf(const std::string& xuidHex)
{
    if (xuidHex == "0000000000000000")
        return L"all profiles";
    XUID x = (XUID)_strtoui64(xuidHex.c_str(), NULL, 16);
    const std::vector<ProfileEntry>& ps = Profiles();
    for (size_t i = 0; i < ps.size(); i++)
        if (ps[i].xuid == x)
            return ps[i].gamertag;
    return Widen(xuidHex);
}

// Content of one type for a title, on both drives: <drive>:\Content\<XUID>\<TID>\<type>\*.
// Switched off (downloadable content, title updates), a package waits in
// SlopDash's own folder on the same drive, under the same path:
// <drive>:\SlopDash\Disabled\Content\<XUID>\<TID>\<type>\* (moving it there
// and back is a rename, however big it is).
const char* const kContentDrives[] = { "hdd:", "usb:" };
const char kOffContent[] = "\\SlopDash\\Disabled\\Content";

struct ContentItem
{
    std::wstring name, owner;
    std::string path;
    std::string drive, rel;     // "hdd:"; "<XUID>\<TID>\<type>\<file>"
    ULONGLONG size;
    bool off;
};

void FindContentIn(unsigned tid, const char* type, bool off, std::vector<ContentItem>& out)
{
    for (int d = 0; d < 2; d++)
    {
        std::string root = std::string(kContentDrives[d]) + (off ? kOffContent : "\\Content");
        std::vector<FoundFile> owners = FindFiles(root);
        for (size_t o = 0; o < owners.size(); o++)
        {
            if (!owners[o].dir || owners[o].name.size() != 16)
                continue;
            std::string sub = owners[o].name + "\\" + TitleHex(tid) + "\\" + type;
            std::vector<FoundFile> files = FindFiles(root + "\\" + sub);
            for (size_t f = 0; f < files.size(); f++)
            {
                if (files[f].dir)
                    continue;
                ContentItem c;
                c.path = files[f].path;
                c.drive = kContentDrives[d];
                c.rel = sub + "\\" + files[f].name;
                c.size = files[f].size;
                c.off = off;
                c.name = PackageName(c.path);
                if (c.name.empty())
                    c.name = Widen(files[f].name);
                c.owner = OwnerOf(owners[o].name);
                out.push_back(c);
            }
        }
    }
}

std::vector<ContentItem> FindContent(unsigned tid, const char* type, bool withOff = false)
{
    std::vector<ContentItem> out;
    FindContentIn(tid, type, false, out);
    if (withOff)
        FindContentIn(tid, type, true, out);
    return out;
}

// A path's folders made, down to its last backslash.
void MakeFoldersFor(const std::string& path)
{
    for (size_t i = path.find('\\'); i != std::string::npos; i = path.find('\\', i + 1))
        CreateDirectoryA(path.substr(0, i).c_str(), NULL);
}

// A package switched on (back into Content) or off (into SlopDash\Disabled).
bool SwitchContent(const ContentItem& c)
{
    std::string on = c.drive + "\\Content\\" + c.rel, off = c.drive + kOffContent + "\\" + c.rel;
    const std::string& from = c.off ? off : on;
    const std::string& to = c.off ? on : off;
    MakeFoldersFor(to);
    bool ok = MoveFileA(from.c_str(), to.c_str()) != FALSE;
    Log("content %s: %s -> %s: %s", c.off ? "on" : "off", from.c_str(), to.c_str(), ok ? "ok" : "FAILED");
    return ok;
}

bool DeleteTree(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == (DWORD)-1)
        return true;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
        return DeleteFileA(path.c_str()) != FALSE;
    std::vector<FoundFile> inside = FindFiles(path);
    bool ok = true;
    for (size_t i = 0; i < inside.size(); i++)
        ok = DeleteTree(inside[i].path) && ok;
    return RemoveDirectoryA(path.c_str()) && ok;
}

// SlopDash's own per-game folders, next to SlopDash.xex (it relies on no other
// dashboard): game:\Screenshots\<TID>\ and game:\Trainers\<TID>\.
std::string GameDataDir(const char* kind, unsigned tid)
{
    std::string base = std::string("game:\\") + kind;
    CreateDirectoryA(base.c_str(), NULL);
    std::string dir = base + "\\" + TitleHex(tid);
    CreateDirectoryA(dir.c_str(), NULL);
    return dir;
}

// A two-press confirmation for deleting: the first press arms it, a second on
// the same item within 5 seconds goes ahead.
struct Confirm
{
    int item;
    DWORD at;
    Confirm() : item(-1), at(0) {}
    // (limit 0: no time limit - the caller calls Forget when the highlight moves)
    bool Ask(int i, DWORD limit = 5000)
    {
        DWORD now = GetTickCount();
        if (item == i && (!limit || now - at < limit))
        {
            item = -1;
            return true;
        }
        item = i;
        at = now;
        return false;
    }
    void Forget() { item = -1; }
};

// --- Achievements ------------------------------------------------------------------
// The signed-in profile's achievements for the title (XUserCreateAchievementEnumerator,
// read on a worker thread with a time limit: a stuck system call mustn't hold the
// dashboard up, as the profile reads did).
struct Achievement
{
    std::wstring label, description, howTo;
    DWORD cred;
    bool earned;
};

struct AchievementRead
{
    DWORD tid;
    int user;
    std::vector<Achievement> list;
    DWORD result;
};

DWORD WINAPI ReadAchievementsThread(LPVOID p)
{
    AchievementRead* r = (AchievementRead*)p;
    HANDLE h = NULL;
    DWORD size = 0;
    r->result = XUserCreateAchievementEnumerator(r->tid, r->user, INVALID_XUID, XACHIEVEMENT_DETAILS_ALL, 0, 100, &size,
                                                 &h);
    if (r->result != ERROR_SUCCESS)
        return 0;
    std::vector<BYTE> buf(size ? size : 1);
    DWORD count = 0;
    r->result = XEnumerate(h, &buf[0], size, &count, NULL);
    const XACHIEVEMENT_DETAILS* a = (const XACHIEVEMENT_DETAILS*)&buf[0];
    for (DWORD i = 0; r->result == ERROR_SUCCESS && i < count; i++)
    {
        Achievement x;
        x.label = a[i].pwszLabel ? a[i].pwszLabel : L"";
        x.description = a[i].pwszDescription ? a[i].pwszDescription : L"";
        x.howTo = a[i].pwszUnachieved ? a[i].pwszUnachieved : L"";
        x.cred = a[i].dwCred;
        x.earned = AchievementEarned(a[i].dwFlags) != FALSE;
        r->list.push_back(x);
    }
    CloseHandle(h);
    return 0;
}

class AchievementsScreen : public ListScreen
{
public:
    AchievementsScreen(unsigned tid) : m_tid(tid), m_read(false) {}
    const wchar_t* Header() { return L"Achievements"; }
    const wchar_t* CurrentHeading() { return L"Unlocked"; }
    std::wstring Current()
    {
        if (m_list.empty())
            return m_note;
        unsigned got = 0, cred = 0, total = 0;
        for (size_t i = 0; i < m_list.size(); i++)
        {
            total += m_list[i].cred;
            if (m_list[i].earned)
            {
                got++;
                cred += m_list[i].cred;
            }
        }
        wchar_t b[64];
        _snwprintf_s(b, 64, _TRUNCATE, L"%u of %u, %u of %u G", got, (unsigned)m_list.size(), cred, total);
        return b;
    }
    void Rebuild()
    {
        if (!m_read)
            Read();
        items.clear();
        for (size_t i = 0; i < m_list.size(); i++)
            items.push_back(m_list[i].label + (m_list[i].earned ? L"" : L"  (locked)"));
        if (items.empty())
            items.push_back(m_note);
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_list.size())
            return L"";
        const Achievement& a = m_list[i];
        wchar_t g[16];
        _snwprintf_s(g, 16, _TRUNCATE, L"%u G", a.cred);
        return std::wstring(g) + L"\r\n\r\n" + (a.earned || a.howTo.empty() ? a.description : a.howTo);
    }
    void Press(HXUIOBJ, int) {}

private:
    void Read()
    {
        m_read = true;
        static bool stuck = false;
        int user = SignedInUser();
        if (user < 0)
        {
            m_note = L"Sign in to see achievements";
            return;
        }
        if (stuck)
        {
            m_note = L"Achievements can't be read right now";
            return;
        }
        AchievementRead* r = new AchievementRead;
        r->tid = m_tid;
        r->user = user;
        r->result = 0;
        HANDLE t = CreateThread(NULL, 0, ReadAchievementsThread, r, 0, NULL);
        if (!t)
        {
            delete r;
            m_note = L"Achievements can't be read right now";
            return;
        }
        if (WaitForSingleObject(t, 5000) != WAIT_OBJECT_0)
        {
            stuck = true;               // the thread still owns r: left to it
            CloseHandle(t);
            Log("achievements %08X: still reading after 5 s", m_tid);
            m_note = L"Achievements can't be read right now";
            return;
        }
        CloseHandle(t);
        Log("achievements %08X: %u (%u)", m_tid, (unsigned)r->list.size(), (unsigned)r->result);
        m_list = r->list;
        delete r;
        m_note = m_list.empty() ? L"No achievements for this game" : L"";
    }
    unsigned m_tid;
    bool m_read;
    std::vector<Achievement> m_list;
    std::wstring m_note;
};

// --- Screenshots (SlopGuide's: Hdd:\SlopDash\Screenshots\<TID>\*.jpg) ---------------
// Taken in the game from the Guide: the recorder panel's Screenshot tile
// (plugin/Recorder.inc). Named <TID>YYYYMMDDhhmmssfff.
class ScreenshotsScreen : public ListScreen
{
public:
    ScreenshotsScreen(unsigned tid) : m_tid(tid) {}
    const wchar_t* Header() { return L"Screenshots"; }
    const wchar_t* CurrentHeading() { return L"Screenshots"; }
    std::wstring Current()
    {
        wchar_t b[32];
        _snwprintf_s(b, 32, _TRUNCATE, L"%u taken", (unsigned)m_files.size());
        return b;
    }
    void Rebuild()
    {
        std::string dir = "hdd:\\SlopDash\\Screenshots\\" + TitleHex(m_tid);
        m_files = FindFiles(dir, "*.jpg");
        std::vector<FoundFile> bmp = FindFiles(dir, "*.bmp");
        m_files.insert(m_files.end(), bmp.begin(), bmp.end());
        items.clear();
        for (size_t i = 0; i < m_files.size(); i++)
            items.push_back(When(m_files[i].name));
        if (items.empty())
            items.push_back(L"No screenshots of this game");
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_files.size())
            return L"To take one in the game: open the Guide, then Screenshot on the recorder panel. It saves "
                   L"the game as it was when the Guide opened.";
        return std::wstring(L"X: delete this screenshot.");
    }
    void Preview(HXUIOBJ scene, int i)
    {
        DestroyAll(m_preview);
        if (i < 0 || i >= (int)m_files.size())
            return;
        // "file://game:/..." like the covers' locators.
        std::string p = m_files[i].path;
        for (size_t k = 0; k < p.size(); k++)
            if (p[k] == '\\')
                p[k] = '/';
        HXUIOBJ img = MakeElementIn(scene, L"XuiImage", NULL, m_preview);
        if (!img)
            return;
        SetUintProperty(img, L"SizeMode", 0x10);
        XuiImageElementSetImagePath(img, (L"file://" + Widen(p)).c_str());
        Box b = { 595, 300, 388, 280, 1.0f };
        Apply(img, b);
    }
    void Press(HXUIOBJ, int) {}
    const wchar_t* XLabel() { return m_files.empty() ? NULL : L"Delete"; }
    void X(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_files.size())
            return;
        if (!m_confirm.Ask(i))
        {
            SetStatus(scene, L"Press X again to delete this screenshot.");
            return;
        }
        std::string path = m_files[i].path;
        DeleteFileA(path.c_str());
        DeleteFileA((path.substr(0, path.size() - 4) + ".meta").c_str());
        g_generation++;
        SetStatus(scene, L"Screenshot deleted.");
    }

private:
    // "<TID>YYYYMMDDhhmmssfff.bmp" -> "DD/MM/YYYY hh:mm:ss".
    static std::wstring When(const std::string& name)
    {
        if (name.size() < 8 + 14)
            return Widen(name);
        const char* s = name.c_str() + 8;
        wchar_t b[32];
        _snwprintf_s(b, 32, _TRUNCATE, L"%.2S/%.2S/%.4S %.2S:%.2S:%.2S", s + 6, s + 4, s, s + 8, s + 10, s + 12);
        return b;
    }
    unsigned m_tid;
    std::vector<FoundFile> m_files;
    std::vector<HXUIOBJ> m_preview;
    Confirm m_confirm;
};

// --- Saved games, downloadable content, title updates ---------------------------------
// (Content\<XUID>\<TID>\00000001 / 00000002 / 000B0000.) Saves: X deletes, Y
// copies to the other drive (a backup on the USB stick, or back). Content and
// title updates: A switches one on or off (the system only finds the ones in
// Content), X deletes.
enum ContentKind { kSavesKind, kDlcKind, kUpdatesKind };

class ContentScreen : public ListScreen
{
public:
    ContentScreen(unsigned tid, ContentKind kind) : m_tid(tid), m_kind(kind) {}
    const wchar_t* Header()
    {
        return m_kind == kSavesKind ? L"Saved Games" : m_kind == kDlcKind ? L"Downloadable Content" : L"Title Updates";
    }
    const wchar_t* CurrentHeading() { return m_kind == kSavesKind ? L"Saves" : m_kind == kDlcKind ? L"Add-ons" : L"Updates"; }
    std::wstring Current()
    {
        ULONGLONG total = 0;
        for (size_t i = 0; i < m_items.size(); i++)
            total += m_items[i].size;
        wchar_t b[64];
        _snwprintf_s(b, 64, _TRUNCATE, L"%u, %s", (unsigned)m_items.size(), SizeText(total).c_str());
        return b;
    }
    void Rebuild()
    {
        static const char* const kTypes[] = { "00000001", "00000002", "000B0000" };
        m_items = FindContent(m_tid, kTypes[m_kind], m_kind != kSavesKind);
        items.clear();
        for (size_t i = 0; i < m_items.size(); i++)
            items.push_back(m_kind == kSavesKind
                                ? m_items[i].name + (m_items[i].drive == "usb:" ? L"  (USB)" : L"  (HDD)")
                                : m_items[i].name + (m_items[i].off ? L"  (Off)" : L"  (On)"));
        if (items.empty())
            items.push_back(m_kind == kSavesKind ? L"No saved games for this game"
                            : m_kind == kDlcKind ? L"No downloadable content for this game"
                                                 : L"No title updates for this game");
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_items.size())
            return m_kind == kUpdatesKind ? L"Title updates go in Content\\0000000000000000\\" + Widen(TitleHex(m_tid)) +
                                                L"\\000B0000 on the hard drive or USB stick."
                                          : L"";
        const ContentItem& c = m_items[i];
        std::wstring s = c.name + L"\r\n" + SizeText(c.size) + L", for " + c.owner + L"\r\n";
        if (m_kind != kSavesKind)
            s += c.off ? L"Off: the game doesn't see it. A: switch it on.\r\n" : L"On. A: switch it off.\r\n";
        return s + L"\r\n" + Widen(c.path);
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (m_kind == kSavesKind || i < 0 || i >= (int)m_items.size())
            return;
        bool ok = SwitchContent(m_items[i]);
        g_generation++;
        SetStatus(scene, !ok ? L"It couldn't be moved (in use?)."
                         : m_items[i].off ? m_items[i].name + L" is on." : m_items[i].name + L" is off.");
    }
    const wchar_t* YLabel()
    {
        return m_kind != kSavesKind || m_items.empty() ? NULL : L"Copy to Other Drive";
    }
    void Y(HXUIOBJ scene, int i)
    {
        if (m_kind != kSavesKind || i < 0 || i >= (int)m_items.size())
            return;
        const ContentItem& c = m_items[i];
        std::string other = c.drive == "hdd:" ? "usb:" : "hdd:";
        if (GetFileAttributesA((other + "\\").c_str()) == (DWORD)-1)
        {
            SetStatus(scene, other == "usb:" ? L"No USB Storage" : L"No Hard Drive");
            return;
        }
        std::string to = other + "\\Content\\" + c.rel;
        if (GetFileAttributesA(to.c_str()) != (DWORD)-1 && !m_replace.Ask(i))
        {
            SetStatus(scene, std::wstring(L"The ") + (other == "usb:" ? L"USB stick" : L"hard drive") +
                                 L" has this save already. Press Y again to replace it.");
            return;
        }
        MakeFoldersFor(to);
        bool ok = CopyFileA(c.path.c_str(), to.c_str(), FALSE) != FALSE;
        Log("save copied %s -> %s: %s", c.path.c_str(), to.c_str(), ok ? "ok" : "FAILED");
        g_generation++;
        SetStatus(scene, !ok ? L"It couldn't be copied."
                             : c.name + (other == "usb:" ? L" copied to the USB stick." : L" copied to the hard drive."));
    }
    const wchar_t* XLabel() { return m_items.empty() ? NULL : L"Delete"; }
    void X(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_items.size())
            return;
        if (!m_confirm.Ask(i))
        {
            SetStatus(scene, L"Press X again to delete " + m_items[i].name + L" for good.");
            return;
        }
        bool ok = DeleteTree(m_items[i].path);
        Log("deleted %s: %s", m_items[i].path.c_str(), ok ? "ok" : "FAILED");
        g_generation++;
        SetStatus(scene, ok ? m_items[i].name + L" deleted." : L"It couldn't be deleted (in use?).");
    }

private:
    unsigned m_tid;
    ContentKind m_kind;
    Confirm m_replace;          // (Y over a save the other drive has)
    std::vector<ContentItem> m_items;
    Confirm m_confirm;
};

// --- Trainers (Aurora's kind: platform/trainers.h; in game:\Trainers\<TID>\) ------------
// The game's trainers: A shows one's cheats, Y switches it On or Off (On: it
// starts with the game; one at a time). Every row says On or Off, and so does
// the Y legend for the highlighted one. Each is made for one build of the
// game (its title
// update shows). The cheats screen: A switches a cheat on or off, X all off;
// "button" cheats are used in the game with the trainer's own buttons.
class CheatsScreen : public ListScreen
{
public:
    CheatsScreen(unsigned tid, const trainers::Found& t) : m_tid(tid), m_t(t) {}
    const wchar_t* Header() { return m_t.info.name.c_str(); }
    const wchar_t* CurrentHeading() { return L"Cheats"; }
    std::wstring Current()
    {
        unsigned mask = trainers::Cheats(m_tid, m_t);
        int on = 0;
        for (size_t i = 0; i < m_t.info.cheats.size() && i < 32; i++)
            on += !m_t.info.cheats[i].button && (mask >> i & 1);
        wchar_t b[48];
        _snwprintf_s(b, 48, _TRUNCATE, L"%d of %d On", on, m_t.info.Toggles());
        return b;
    }
    void Rebuild()
    {
        unsigned mask = trainers::Cheats(m_tid, m_t);
        items.clear();
        for (size_t i = 0; i < m_t.info.cheats.size(); i++)
        {
            const trainer::Cheat& c = m_t.info.cheats[i];
            items.push_back(c.text + (c.button ? L"  (In Game)" : (i < 32 && (mask >> i & 1)) ? L"  (On)" : L"  (Off)"));
        }
        if (items.empty())
            items.push_back(L"No cheats listed");
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_t.info.cheats.size())
            return L"";
        if (m_t.info.cheats[i].button)
            return L"Used in the game, with the trainer's own buttons.";
        return L"A: switch it on or off. It takes effect the next time the game starts with this trainer.";
    }
    void Press(HXUIOBJ, int i)
    {
        if (i < 0 || i >= (int)m_t.info.cheats.size() || i >= 32 || m_t.info.cheats[i].button)
            return;
        trainers::SetCheats(m_tid, m_t, trainers::Cheats(m_tid, m_t) ^ (1u << i));
        g_generation++;
    }
    const wchar_t* XLabel() { return m_t.info.Toggles() ? L"Clear All" : NULL; }
    void X(HXUIOBJ scene, int)
    {
        trainers::SetCheats(m_tid, m_t, 0);
        g_generation++;
        SetStatus(scene, L"All cheats off.");
    }

private:
    unsigned m_tid;
    trainers::Found m_t;
};

class TrainersScreen : public ListScreen
{
public:
    TrainersScreen(unsigned tid) : m_tid(tid), m_shown(0) {}
    const wchar_t* Header() { return L"Trainers"; }
    const wchar_t* CurrentHeading() { return L"Trainer On"; }
    std::wstring Current()
    {
        for (size_t i = 0; i < m_found.size(); i++)
            if (m_found[i].file == m_chosen && m_found[i].valid)
                return m_found[i].info.name;
        return L"None";
    }
    void Rebuild()
    {
        m_found = trainers::Find(m_tid);
        m_chosen = trainers::Chosen(m_tid);
        items.clear();
        for (size_t i = 0; i < m_found.size(); i++)
        {
            const trainers::Found& t = m_found[i];
            std::wstring row = t.valid ? t.info.name + L"  " + t.info.version : Widen(t.file);
            if (!t.valid)
                row += L"  (Not a Trainer)";
            else if (t.fit == trainer::kWrongUpdate)
                row += L"  (Needs " + t.info.UpdatesText() + L")";
            else
                row += t.file == m_chosen ? L"  (On)" : L"  (Off)";
            items.push_back(row);
        }
        if (items.empty())
            items.push_back(L"No trainers for this game");
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_found.size())
            return L"Put this game's trainers in SlopDash's Trainers\\" + Widen(TitleHex(m_tid)) +
                   L" folder. Trainers made for Aurora work. Use them offline.";
        const trainers::Found& t = m_found[i];
        if (!t.valid)
            return Widen(t.file) + L"\r\n\r\nNot a trainer SlopDash can load (" + Widen(t.error) + L").";
        wchar_t b[64];
        _snwprintf_s(b, 64, _TRUNCATE, L"%u cheats, %d to switch\r\n", (unsigned)t.info.cheats.size(),
                     t.info.Toggles());
        std::wstring s = L"By " + t.info.author + L"\r\nFor " + t.info.UpdatesText() + L"\r\n" + b;
        // (The game's own title update: Aurora's rule, trainer::Check.)
        if (t.fit == trainer::kWrongUpdate)
            s += L"Doesn't match this game's title update: it can't start with it.\r\n";
        else if (t.fit == trainer::kUnknownUpdate)
            s += L"This game's title update can't be told: it may not work.\r\n";
        s += t.file == m_chosen ? L"On: it starts with the game. Y: Off.\r\n" : L"Off. Y: On.\r\n";
        if (trainers::AnyLoaded())
            s += L"A trainer is loaded until the console restarts.\r\n";
        std::wstring d = t.info.description;
        if (d.size() > 220)
            d = d.substr(0, 220) + L"...";
        return s + L"\r\n" + d;
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_found.size())
            return;
        if (!m_found[i].valid)
        {
            SetStatus(scene, L"Not a trainer SlopDash can load.");
            return;
        }
        OpenListScreen(scene, new CheatsScreen(m_tid, m_found[i]));
    }
    const wchar_t* YLabel()
    {
        if (m_found.empty())
            return NULL;
        int i = m_shown >= 0 && m_shown < (int)m_found.size() ? m_shown : 0;
        return m_found[i].valid && m_found[i].file == m_chosen ? L"Turn Off" : L"Turn On";
    }
    // The Y legend follows the highlighted trainer.
    void Preview(HXUIOBJ scene, int i)
    {
        m_shown = i;
        HXUIOBJ y = Child(scene, L"legend_y");
        bool any = i >= 0 && i < (int)m_found.size() && m_found[i].valid && m_found[i].fit != trainer::kWrongUpdate;
        bool on = any && m_found[i].file == m_chosen;
        if (y)
        {
            XuiControlSetText(y, on ? L"Turn Off" : L"Turn On");
            XuiControlSetEnable(y, any || on);
            XuiElementSetShow(y, any || on);
        }
    }
    void Y(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_found.size() || !m_found[i].valid)
            return;
        bool off = m_found[i].file == m_chosen;
        if (!off && m_found[i].fit == trainer::kWrongUpdate)
        {
            SetStatus(scene, L"Needs " + m_found[i].info.UpdatesText() + L".");
            return;
        }
        trainers::Choose(m_tid, off ? "" : m_found[i].file);
        g_generation++;
        SetStatus(scene, m_found[i].info.name + (off ? L": Off" : L": On"));
    }

private:
    unsigned m_tid;
    std::vector<trainers::Found> m_found;
    std::string m_chosen;
    int m_shown;                    // the highlighted row (the Y legend's)
};

// --- File manager: copy, move, rename, delete -------------------
// A folder's list: A opens a folder, Y the highlighted entry's options (Copy,
// Move, Rename, Delete), X pastes what Copy or Move picked up into the folder
// on screen. Copies, moves and deletes run on a thread of their own; the
// screen shows how far along they are.
//
// What the console needs to start is never moved, renamed or deleted: the
// softmod's BadUpdatePayload, launch.ini, SlopDash's own folder and files,
// and the BadAvatar exploit's profile content (hidden everywhere else too).
namespace fileops {

enum Op { kNone, kCopy, kMove, kDelete };

struct Job
{
    Op op;
    std::string from, to;
    ULONGLONG total, done;
    bool ok;
};
Job g_job = { kNone, "", "", 0, 0, false };
volatile LONG g_busy = 0;
volatile LONG g_finished = 0;       // a job ended since the screen last looked

// What Copy or Move picked up.
std::string g_clip;
bool g_clipMove = false;

std::string Lower(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
        s[i] = (char)tolower((unsigned char)s[i]);
    return s;
}

std::string NameOf(const std::string& path)
{
    size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string ParentOf(const std::string& path)
{
    size_t slash = path.find_last_of('\\');
    return slash == std::string::npos ? path : path.substr(0, slash);
}

// "usb:\a\b" or "game:\b" -> the drive it's on, lower case ("usb:").
std::string DriveOf(const std::string& path)
{
    size_t colon = path.find(':');
    return colon == std::string::npos ? "" : Lower(path.substr(0, colon + 1));
}

bool Inside(const std::string& path, const std::string& folder)
{
    std::string p = Lower(path), f = Lower(folder);
    return p == f || (p.size() > f.size() && !p.compare(0, f.size(), f) && p[f.size()] == '\\');
}

// SlopDash's own folder, as a drive path ("usb:\Apps\SlopDash").
std::string OwnFolder()
{
    static std::string folder;
    if (folder.empty())
    {
        // (game: is SlopDash's folder; the file manager shows drive paths)
        if (GetFileAttributesA("usb:\\Apps\\SlopDash\\SlopDash.xex") != (DWORD)-1)
            folder = "usb:\\Apps\\SlopDash";
        else if (GetFileAttributesA("hdd:\\Apps\\SlopDash\\SlopDash.xex") != (DWORD)-1)
            folder = "hdd:\\Apps\\SlopDash";
    }
    return folder;
}

// Why a path can't be changed ("" if it can).
std::wstring Protected(const std::string& path)
{
    std::string p = Lower(path);
    if (p.find("\\badupdatepayload") != std::string::npos)
        return L"Protected. The Softmod Needs It";
    if (NameOf(p) == "launch.ini" && Lower(ParentOf(p)).find('\\') == std::string::npos)
        return L"Protected. DashLaunch Needs It";
    std::string own = OwnFolder();
    if (!own.empty() && (Lower(own) == p || Inside(own, p) ||
                         (Inside(p, own) && p.size() > 4 && !p.compare(p.size() - 4, 4, ".xex"))))
        return L"Protected. SlopDash Needs It";
    if (!_stricmp(p.substr(0, 5).c_str(), "game:"))
        return L"Protected. SlopDash Needs It";
    XUID hidden = HiddenProfile();
    if (hidden)
    {
        char owner[32];
        _snprintf_s(owner, sizeof(owner), _TRUNCATE, "\\content\\%016I64x", hidden);
        if (p.find(Lower(owner)) != std::string::npos)
            return L"Protected";
    }
    if (p.find('\\') == std::string::npos)
        return L"Protected";            // (a drive itself)
    return L"";
}

ULONGLONG TreeSize(const std::string& path)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a))
        return 0;
    if (!(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    ULONGLONG total = 0;
    std::vector<FoundFile> inside = FindFiles(path);
    for (size_t i = 0; i < inside.size(); i++)
        total += inside[i].dir ? TreeSize(inside[i].path) : inside[i].size;
    return total;
}

bool CopyFileTracked(const std::string& from, const std::string& to)
{
    HANDLE in = CreateFileA(from.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE)
        return false;
    HANDLE out = CreateFileA(to.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (out == INVALID_HANDLE_VALUE)
    {
        CloseHandle(in);
        return false;
    }
    std::vector<unsigned char> buf(1024 * 1024);
    bool ok = true;
    for (;;)
    {
        DWORD got = 0, put = 0;
        if (!ReadFile(in, &buf[0], (DWORD)buf.size(), &got, NULL))
        {
            ok = false;
            break;
        }
        if (!got)
            break;
        if (!WriteFile(out, &buf[0], got, &put, NULL) || put != got)
        {
            ok = false;
            break;
        }
        g_job.done += got;
    }
    CloseHandle(out);
    CloseHandle(in);
    if (!ok)
        DeleteFileA(to.c_str());
    return ok;
}

bool CopyTreeTracked(const std::string& from, const std::string& to)
{
    DWORD attr = GetFileAttributesA(from.c_str());
    if (attr == (DWORD)-1)
        return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY))
        return CopyFileTracked(from, to);
    if (!CreateDirectoryA(to.c_str(), NULL))
        return false;
    std::vector<FoundFile> inside = FindFiles(from);
    for (size_t i = 0; i < inside.size(); i++)
        if (!CopyTreeTracked(inside[i].path, to + "\\" + inside[i].name))
            return false;
    return true;
}

DWORD WINAPI Work(LPVOID)
{
    Job& j = g_job;
    bool ok = true;
    if (j.op == kMove && DriveOf(j.from) == DriveOf(j.to))
        ok = MoveFileA(j.from.c_str(), j.to.c_str()) != FALSE;      // (same drive: one rename)
    else
    {
        if (j.op == kCopy || j.op == kMove)
        {
            ok = CopyTreeTracked(j.from, j.to);
            if (!ok)
                DeleteTree(j.to);                                   // (no half copy left behind)
        }
        if (ok && (j.op == kMove || j.op == kDelete))
            ok = DeleteTree(j.from);
    }
    j.ok = ok;
    Log("file manager: %s %s%s%s: %s", j.op == kCopy ? "copy" : j.op == kMove ? "move" : "delete", j.from.c_str(),
        j.op == kDelete ? "" : " to ", j.op == kDelete ? "" : j.to.c_str(), ok ? "done" : "FAILED");
    InterlockedExchange(&g_finished, 1);
    InterlockedExchange(&g_busy, 0);
    return 0;
}

bool Start(Op op, const std::string& from, const std::string& to)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0))
        return false;
    g_job.op = op;
    g_job.from = from;
    g_job.to = to;
    g_job.done = 0;
    g_job.total = op == kDelete ? 0 : TreeSize(from);
    g_job.ok = false;
    HANDLE t = CreateThread(NULL, 128 * 1024, Work, NULL, CREATE_SUSPENDED, NULL);
    if (!t)
    {
        InterlockedExchange(&g_busy, 0);
        return false;
    }
    XSetThreadProcessor(t, 4);
    ResumeThread(t);
    CloseHandle(t);
    return true;
}

// "Copying 45%", "Moving", "Deleting".
std::wstring Progress()
{
    const wchar_t* verb = g_job.op == kCopy ? L"Copying" : g_job.op == kMove ? L"Moving" : L"Deleting";
    if (!g_job.total)
        return verb;
    wchar_t b[48];
    _snwprintf_s(b, ARRAYSIZE(b), _TRUNCATE, L"%s %u%%", verb, (unsigned)(g_job.done * 100 / g_job.total));
    return b;
}

std::wstring Result()
{
    if (g_job.op == kCopy)
        return g_job.ok ? L"Copied" : L"Cannot Copy";
    if (g_job.op == kMove)
        return g_job.ok ? L"Moved" : L"Cannot Move";
    return g_job.ok ? L"Deleted" : L"Cannot Delete";
}

// A name the drives take (FATX: 1-42 characters, none of these).
bool GoodName(const std::string& name)
{
    if (name.empty() || name.size() > 42 || name == "." || name == "..")
        return false;
    for (size_t i = 0; i < name.size(); i++)
    {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20 || c > 0x7E || strchr("<>:\"/\\|?*", c))
            return false;
    }
    return true;
}

}  // namespace fileops

// Y on an entry: what can be done with it.
class FileOptionsScreen : public ListScreen
{
public:
    FileOptionsScreen(const FoundFile& f) : m_f(f), m_typing(false), m_gone(false)
    {
        memset(&m_ov, 0, sizeof(m_ov));
        m_text[0] = 0;
    }
    const wchar_t* Header() { return L"File Options"; }
    const wchar_t* CurrentHeading() { return m_f.dir ? L"Folder" : L"File"; }
    std::wstring Current() { return Widen(m_f.name); }
    void Rebuild()
    {
        items.clear();
        items.push_back(L"Copy");
        items.push_back(L"Move");
        items.push_back(L"Rename");
        items.push_back(L"Delete");
    }
    std::wstring About(int i)
    {
        std::wstring path = Widen(m_f.path) + (m_f.dir ? L"" : L"\r\n" + SizeText(m_f.size));
        if (i == 0)
            return path + L"\r\n\r\nPick it up to copy: then open a folder and press X to paste.";
        if (i == 1)
            return path + L"\r\n\r\nPick it up to move: then open a folder and press X to paste.";
        if (i == 2)
            return path + L"\r\n\r\nGive it a new name.";
        return path + L"\r\n\r\nDelete it for good. Press A twice.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (m_gone)
            return;
        if (fileops::g_busy)
        {
            SetStatus(scene, fileops::Progress());
            return;
        }
        std::wstring why = fileops::Protected(m_f.path);
        if (i == 0)
        {
            fileops::g_clip = m_f.path;
            fileops::g_clipMove = false;
            SetStatus(scene, L"Ready to Copy. Open a Folder, Press X");
            return;
        }
        if (!why.empty())
        {
            SetStatus(scene, why);
            return;
        }
        if (i == 1)
        {
            fileops::g_clip = m_f.path;
            fileops::g_clipMove = true;
            SetStatus(scene, L"Ready to Move. Open a Folder, Press X");
        }
        else if (i == 2 && !m_typing)
        {
            memset(&m_ov, 0, sizeof(m_ov));
            std::wstring now = Widen(m_f.name);
            int user = SignedInUser();
            DWORD r = ShowKeyboard(user < 0 ? 0 : user, VKBD_DEFAULT, now.c_str(), L"Rename",
                                   L"Up to 42 letters, numbers and symbols.", m_text, 43, &m_ov);
            m_typing = r == ERROR_IO_PENDING || r == ERROR_SUCCESS;
        }
        else if (i == 3)
        {
            if (!m_confirm.Ask(i))
            {
                SetStatus(scene, L"Press A Again to Delete");
                return;
            }
            if (fileops::Start(fileops::kDelete, m_f.path, ""))
            {
                m_gone = true;
                SetStatus(scene, fileops::Progress());
            }
        }
    }
    void Tick(HXUIOBJ scene)
    {
        if (m_gone && InterlockedExchange(&fileops::g_finished, 0))
        {
            g_generation++;
            SetStatus(scene, fileops::Result());
        }
        else if (m_gone && fileops::g_busy)
            SetStatus(scene, fileops::Progress());
        if (!m_typing || !XHasOverlappedIoCompleted(&m_ov))
            return;
        m_typing = false;
        if (XGetOverlappedExtendedError(&m_ov) != ERROR_SUCCESS || !m_text[0])
            return;
        char name[64];
        WideCharToMultiByte(CP_ACP, 0, m_text, -1, name, sizeof(name), NULL, NULL);
        if (!fileops::GoodName(name))
        {
            SetStatus(scene, L"Cannot Use That Name");
            return;
        }
        std::string to = fileops::ParentOf(m_f.path) + "\\" + name;
        if (GetFileAttributesA(to.c_str()) != (DWORD)-1)
        {
            SetStatus(scene, L"That Name Is Taken");
            return;
        }
        bool ok = MoveFileA(m_f.path.c_str(), to.c_str()) != FALSE;
        Log("file manager: rename %s to %s: %s", m_f.path.c_str(), to.c_str(), ok ? "done" : "FAILED");
        if (ok)
        {
            m_f.path = to;
            m_f.name = name;
            g_generation++;
        }
        SetStatus(scene, ok ? L"Renamed" : L"Cannot Rename");
    }

private:
    FoundFile m_f;
    bool m_typing, m_gone;
    XOVERLAPPED m_ov;
    wchar_t m_text[64];
    Confirm m_confirm;
};

// A folder's list (a game's own folder, or a whole drive's).
// --- Folders named by a title ID: the game's name -------------------------------------
// So you know which folder you're going into. A folder whose name is a title ID (8
// hex digits: Content\<profile>\<TID>, Cache, Trainers\<TID>, SlopDash's
// cache\<TID>...) is named after the game: from the library first (its own or
// renamed name), then the dashboard's own, then the title name in the
// header of the first package inside it (a save or DLC knows its game's name
// even when the game isn't installed). Looked up once a title.
bool IsTitleIdName(const std::string& name, unsigned& tid)
{
    if (name.size() != 8)
        return false;
    for (size_t i = 0; i < 8; i++)
        if (!isxdigit((unsigned char)name[i]))
            return false;
    tid = strtoul(name.c_str(), NULL, 16);
    return tid != 0 && tid != 0xFFFFFFFF;
}

// The first file within a folder and its subfolders, at most `depth` down ("" none).
std::string FirstFileIn(const std::string& dir, int depth)
{
    std::vector<FoundFile> all = FindFiles(dir);
    for (size_t i = 0; i < all.size(); i++)
        if (!all[i].dir && all[i].size >= library::kStfsHeaderBytes)
            return all[i].path;
    for (size_t i = 0; depth > 0 && i < all.size(); i++)
        if (all[i].dir)
        {
            std::string f = FirstFileIn(all[i].path, depth - 1);
            if (!f.empty())
                return f;
        }
    return "";
}

std::wstring TitleNameOf(unsigned tid, const std::string& folder)
{
    static std::map<unsigned, std::wstring> known;
    std::map<unsigned, std::wstring>::iterator it = known.find(tid);
    if (it != known.end())
        return it->second;
    std::wstring name;
    const std::vector<library::Game>& games = AllGames();
    for (size_t i = 0; i < games.size() && name.empty(); i++)
        if (games[i].titleId == tid)
        {
            name = CustomGameName(tid);
            if (name.empty())
                name = games[i].name;
        }
    if (name.empty() && tid == 0xFFFE07D1)
        name = L"Xbox 360 Dashboard";           // (the dashboard's own title ID, as achievements.cpp reads it)
    if (name.empty())
    {
        std::string file = FirstFileIn(folder, 2);
        HANDLE h = file.empty() ? INVALID_HANDLE_VALUE
                                : CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            std::vector<unsigned char> head(library::kStfsHeaderBytes);
            DWORD got = 0;
            library::ContentHeader c;
            if (ReadFile(h, &head[0], (DWORD)head.size(), &got, NULL) && got == head.size() &&
                library::ReadContentHeader(&head[0], head.size(), c) && c.titleId == tid)
                name = c.titleName;
            CloseHandle(h);
        }
    }
    known[tid] = name;
    return name;
}

class FilesScreen : public ListScreen
{
public:
    FilesScreen(const std::string& root) : m_root(root), m_dir(root), m_jump(false), m_watching(false) {}
    const wchar_t* Header() { return L"File Manager"; }
    const wchar_t* CurrentHeading() { return L"Looking In"; }
    std::wstring Current() { return Widen(m_dir); }
    void Rebuild()
    {
        m_entries.clear();
        items.clear();
        if (_stricmp(m_dir.c_str(), m_root.c_str()))
        {
            FoundFile up;
            up.name = "..";
            up.dir = true;
            up.size = 0;
            m_entries.push_back(up);
            items.push_back(L".. (up a folder)");
        }
        std::vector<FoundFile> all = FindFiles(m_dir);
        for (int pass = 0; pass < 2; pass++)          // folders first
            for (size_t i = 0; i < all.size(); i++)
                if (all[i].dir == (pass == 0))
                {
                    m_entries.push_back(all[i]);
                    std::wstring row = all[i].dir ? L"[" + Widen(all[i].name) + L"]"
                                                  : Widen(all[i].name) + L"  " + SizeText(all[i].size);
                    unsigned tid = 0;
                    if (all[i].dir && IsTitleIdName(all[i].name, tid))
                    {
                        std::wstring game = TitleNameOf(tid, all[i].path);
                        if (!game.empty())
                            row += L"  " + game;
                    }
                    items.push_back(row);
                }
        if (items.empty())
            items.push_back(L"(empty)");
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_entries.size())
            return L"";
        if (m_entries[i].name == "..")
            return L"A: go up a folder.";
        unsigned tid = 0;
        std::wstring game = m_entries[i].dir && IsTitleIdName(m_entries[i].name, tid)
                                ? TitleNameOf(tid, m_entries[i].path) : L"";
        return (game.empty() ? L"" : game + L"\r\n") + Widen(m_entries[i].path) +
               (m_entries[i].dir ? L"\r\n\r\nA: open this folder." : L"");
    }
    void Press(HXUIOBJ, int i)
    {
        if (i < 0 || i >= (int)m_entries.size() || !m_entries[i].dir)
            return;
        if (m_entries[i].name == "..")
        {
            size_t slash = m_dir.find_last_of('\\');
            m_dir = slash == std::string::npos ? m_root : m_dir.substr(0, slash);
        }
        else
            m_dir = m_entries[i].path;
        m_jump = true;
        g_generation++;
    }
    const wchar_t* YLabel() { return L"Options"; }
    void Y(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_entries.size() || m_entries[i].name == "..")
            return;
        OpenListScreen(scene, new FileOptionsScreen(m_entries[i]));
    }
    const wchar_t* XLabel() { return fileops::g_clip.empty() ? NULL : L"Paste"; }
    void X(HXUIOBJ scene, int)
    {
        const std::string& from = fileops::g_clip;
        if (from.empty())
            return;
        if (fileops::g_busy)
        {
            SetStatus(scene, fileops::Progress());
            return;
        }
        std::string to = m_dir + "\\" + fileops::NameOf(from);
        if (fileops::Inside(m_dir, from))
        {
            SetStatus(scene, L"Cannot Paste a Folder Into Itself");
            return;
        }
        if (GetFileAttributesA(to.c_str()) != (DWORD)-1)
        {
            SetStatus(scene, L"Already Here");
            return;
        }
        std::wstring why = fileops::Protected(to);
        if (!why.empty())
        {
            SetStatus(scene, why);
            return;
        }
        if (fileops::Start(fileops::g_clipMove ? fileops::kMove : fileops::kCopy, from, to))
        {
            if (fileops::g_clipMove)
                fileops::g_clip.clear();        // (it's moving: nothing left to paste)
            m_watching = true;
            SetStatus(scene, fileops::Progress());
        }
    }
    void Tick(HXUIOBJ scene)
    {
        if (!m_watching)
            return;
        if (InterlockedExchange(&fileops::g_finished, 0))
        {
            m_watching = false;
            g_generation++;
            SetStatus(scene, fileops::Result());
        }
        else if (fileops::g_busy)
            SetStatus(scene, fileops::Progress());
    }
    int SelectAfter(int i)
    {
        bool jump = m_jump;
        m_jump = false;
        return jump ? 0 : i;
    }

private:
    std::string m_root, m_dir;
    std::vector<FoundFile> m_entries;
    bool m_jump, m_watching;
};

// The folder a game lives in: a package's title folder (Content\...\<TID>), an
// extracted game's own folder.
std::string GameFolder(const library::Game& g)
{
    std::string p = g.path;
    size_t slash = p.find_last_of('\\');
    std::string dir = slash == std::string::npos ? p : p.substr(0, slash);
    bool executable = p.size() > 4 && (!_stricmp(p.c_str() + p.size() - 4, ".xex") || !_stricmp(p.c_str() + p.size() - 4, ".xbe"));
    if (executable)
        return dir;
    slash = dir.find_last_of('\\');                     // ...\<TID>\<type>: up to <TID>
    return slash == std::string::npos ? dir : dir.substr(0, slash);
}

// --- Game Settings: rename, delete -------------------------------------------------------
class GameSettingsScreen : public ListScreen
{
public:
    GameSettingsScreen(unsigned tid, const std::wstring& name, const std::string& path, const std::string& folder,
                       bool executable)
        : m_tid(tid), m_name(name), m_path(path), m_folder(folder), m_executable(executable), m_typing(false),
          m_deleted(false)
    {
        memset(&m_ov, 0, sizeof(m_ov));
        m_text[0] = 0;
    }
    const wchar_t* Header() { return L"Game Settings"; }
    const wchar_t* CurrentHeading() { return L"Name"; }
    std::wstring Current() { return m_deleted ? L"(deleted)" : m_name; }
    void Rebuild()
    {
        items.clear();
        items.push_back(L"Rename");
        items.push_back(L"Restore Original Name");
        items.push_back(L"Delete Game");
    }
    std::wstring About(int i)
    {
        if (i == 0)
            return L"Give the game the name the library shows. Its files aren't changed.";
        if (i == 1)
            return L"Show the game's own name again.";
        return L"Delete the game from the drive, for good:\r\n" + Widen(m_executable ? m_folder : m_path) +
               L"\r\n\r\nPress A twice to delete.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (m_deleted)
            return;
        if (i == 0 && !m_typing)
        {
            memset(&m_ov, 0, sizeof(m_ov));
            int user = SignedInUser();
            DWORD r = ShowKeyboard(user < 0 ? 0 : user, VKBD_DEFAULT, m_name.c_str(), L"Rename",
                                      L"The name the library shows for this game.", m_text, 64, &m_ov);
            m_typing = r == ERROR_IO_PENDING || r == ERROR_SUCCESS;
            Log("rename %08X: keyboard %u", m_tid, (unsigned)r);
        }
        else if (i == 1)
        {
            SetCustomGameName(m_tid, L"");
            SetStatus(scene, L"Its own name is back. The library will update.");
        }
        else if (i == 2)
        {
            if (!m_confirm.Ask(i))
            {
                SetStatus(scene, L"Press A again to delete " + m_name + L" for good.");
                return;
            }
            bool ok = Delete();
            Log("delete game %08X: %s", m_tid, ok ? "ok" : "FAILED");
            if (ok)
            {
                m_deleted = true;
                MarkLibraryChanged();
                g_generation++;
            }
            SetStatus(scene, ok ? m_name + L" deleted. The library will update." : L"It couldn't all be deleted.");
        }
    }
    void Tick(HXUIOBJ scene)
    {
        if (!m_typing || !XHasOverlappedIoCompleted(&m_ov))
            return;
        m_typing = false;
        if (XGetOverlappedExtendedError(&m_ov) == ERROR_SUCCESS && m_text[0])
        {
            m_name = m_text;
            SetCustomGameName(m_tid, m_name);
            g_generation++;
            SetStatus(scene, L"Renamed. The library will update.");
        }
    }

private:
    bool Delete()
    {
        if (m_executable)
        {
            // An extracted game's folder, never a drive or a searched folder itself.
            std::vector<std::string> roots = ScanPaths();
            for (size_t i = 0; i < roots.size(); i++)
                if (!_stricmp(roots[i].c_str(), m_folder.c_str()))
                    return false;
            if (std::count(m_folder.begin(), m_folder.end(), '\\') < 2)
                return false;
            return DeleteTree(m_folder);
        }
        // A package: the file, and the .data folder beside it (GOD).
        bool ok = DeleteFileA(m_path.c_str()) != FALSE;
        DeleteTree(m_path + ".data");
        return ok;
    }
    unsigned m_tid;
    std::wstring m_name;
    std::string m_path, m_folder;
    bool m_executable, m_typing, m_deleted;
    XOVERLAPPED m_ov;
    wchar_t m_text[64];
    Confirm m_confirm;
};

// --- The menu ----------------------------------------------------------------------
class GameMenuScreen : public ListScreen
{
public:
    // The menu's entries (an app or emulator has no achievements, saves...).
    enum Action { kLaunch, kAchievements, kScreenshots, kSaves, kContent, kTrainers, kFiles, kSettings, kUpdates };

    GameMenuScreen(int game) : m_game(game)
    {
        const library::Game& g = Games()[game];
        m_tid = g.titleId;
        m_name = g.name;
        m_kind = KindText(g.kind);
        m_path = g.path;
        m_folder = GameFolder(g);
        m_place = g.place;
        m_executable = m_path.size() > 4 && (!_stricmp(m_path.c_str() + m_path.size() - 4, ".xex") ||
                                             !_stricmp(m_path.c_str() + m_path.size() - 4, ".xbe"));
        if (m_place == kPlaceGames)
        {
            static const Action kGame[] = { kLaunch, kAchievements, kScreenshots, kSaves, kContent, kUpdates, kTrainers,
                                            kFiles, kSettings };
            m_actions.assign(kGame, kGame + 9);
        }
        else
        {
            // (renaming goes by title ID, which homebrew shares: only with one of its own)
            m_actions.push_back(kLaunch);
            m_actions.push_back(kFiles);
            if (m_tid)
                m_actions.push_back(kSettings);
        }
    }
    const wchar_t* Header() { return m_name.c_str(); }
    const wchar_t* CurrentHeading()
    {
        return m_place == kPlaceApps ? L"Application" : m_place == kPlaceEmulators ? L"Emulator" : L"Game";
    }
    std::wstring Current() { return m_place == kPlaceGames ? m_kind : Widen(m_folder); }
    void Rebuild()
    {
        items.clear();
        static const wchar_t* const kItems[] = { L"Launch Game", L"Achievements", L"Screenshots", L"Saved Games",
                                                 L"Downloadable Content", L"Trainers", L"File Manager",
                                                 L"Game Settings", L"Title Updates" };
        for (size_t i = 0; i < m_actions.size(); i++)
            items.push_back(m_place != kPlaceGames && m_actions[i] == kLaunch     ? L"Launch"
                            : m_place != kPlaceGames && m_actions[i] == kSettings ? L"Settings"
                                                                                   : kItems[m_actions[i]]);
    }
    std::wstring About(int i)
    {
        static const wchar_t* const kAbout[] = {
            L"Start the game.", L"The signed-in profile's achievements for this game.",
            L"Screenshots taken in this game.", L"This game's saved games, for each profile.",
            L"Add-ons for this game: switch them on or off.", L"Trainers for this game: the one that starts with it, and its cheats.",
            L"Browse the game's files.", L"Rename the game, or delete it.",
            L"This game's title updates: switch them on or off." };
        if (i < 0 || i >= (int)m_actions.size())
            return L"";
        if (m_place != kPlaceGames)
        {
            const wchar_t* what = m_place == kPlaceApps ? L"application" : L"emulator";
            if (m_actions[i] == kLaunch)
                return std::wstring(L"Start the ") + what + L".";
            if (m_actions[i] == kFiles)
                return std::wstring(L"Browse the ") + what + L"'s files.";
            return std::wstring(L"Rename the ") + what + L", or delete it.";
        }
        return kAbout[m_actions[i]];
    }
    void Preview(HXUIOBJ scene, int)
    {
        if (!m_preview.empty())
            return;
        HXUIOBJ img = MakeElementIn(scene, L"XuiImage", NULL, m_preview);
        if (!img || m_game >= (int)Games().size())
            return;
        SetUintProperty(img, L"SizeMode", 0x10);
        XuiImageElementSetImagePath(img, CoverOrIcon(m_game).c_str());
        Box b = { 595, 300, 388, 280, 1.0f };
        Apply(img, b);
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_actions.size())
            return;
        // The library may have been rescanned since: find the entry again, by
        // its path (homebrew shares title IDs), else its title ID.
        const std::vector<library::Game>& games = Games();
        int at = -1;
        for (size_t k = 0; k < games.size() && at < 0; k++)
            if (!_stricmp(games[k].path.c_str(), m_path.c_str()))
                at = (int)k;
        for (size_t k = 0; k < games.size() && at < 0 && m_tid; k++)
            if (games[k].titleId == m_tid)
                at = (int)k;
        if (m_actions[i] == kLaunch)
        {
            if (at < 0)
                SetStatus(scene, L"This isn't in the library any more.");
            else
                LaunchGame(games[at]);
            return;
        }
        ListScreen* next = NULL;
        switch (m_actions[i])
        {
        case kAchievements: next = new AchievementsScreen(m_tid); break;
        case kScreenshots: next = new ScreenshotsScreen(m_tid); break;
        case kSaves: next = new ContentScreen(m_tid, kSavesKind); break;
        case kContent: next = new ContentScreen(m_tid, kDlcKind); break;
        case kUpdates: next = new ContentScreen(m_tid, kUpdatesKind); break;
        case kTrainers: next = new TrainersScreen(m_tid); break;
        case kFiles: next = new FilesScreen(m_folder); break;
        case kSettings: next = new GameSettingsScreen(m_tid, m_name, m_path, m_folder, m_executable); break;
        default: break;
        }
        if (next)
            OpenListScreen(scene, next);
    }

private:
    int m_game;
    unsigned m_tid;
    int m_place;
    std::wstring m_name, m_kind;
    std::string m_path, m_folder;
    bool m_executable;
    std::vector<Action> m_actions;
    std::vector<HXUIOBJ> m_preview;
};

HRESULT OpenGameMenu(HXUIOBJ from, int game)
{
    if (game < 0 || game >= (int)Games().size())
        return E_INVALIDARG;
    return OpenListScreen(from, new GameMenuScreen(game));
}

// --- A game's screen asked for by the Xbox theme ----------------------------------------
// Its per-game menu (X on a game) hands Achievements, Saved Games, Title
// Updates, Downloadable Content and Trainers to Blades: SlopDash.ini's
// blades.opengame = <title ID, 8 hex digits>:<achievements|saves|updates|dlc|
// trainers>, then a switch to Blades. Read once, as Blades starts (the main
// scene's timer: blades1888.cpp), and removed; that game's Game Menu opens,
// then the screen over it (B goes back to the menu). A title not in the
// library: nothing. Returns false while the library isn't ready yet.
bool OpenGameFromSetting(HXUIOBJ from)
{
    std::string v = GetSetting("blades.opengame");
    if (v.empty())
        return true;
    const std::vector<library::Game>& games = Games();
    if (games.empty())
        return false;                           // (the library's still loading: asked again)
    SetSetting("blades.opengame", "");
    size_t colon = v.find(':');
    unsigned tid = (unsigned)strtoul(v.substr(0, colon).c_str(), NULL, 16);
    std::string what = colon == std::string::npos ? "" : v.substr(colon + 1);
    int at = -1;
    for (size_t i = 0; i < games.size() && at < 0 && tid; i++)
        if (games[i].titleId == tid)
            at = (int)i;
    if (at < 0)
    {
        Log("blades.opengame %s: not in the library", v.c_str());
        return true;
    }
    // The Game Menu, then (over it) the screen: its scene is the one opened now.
    HXUIOBJ focusBefore = XuiElementGetFocus();     // (on the blade they open over)
    std::set<HXUIOBJ> before;
    for (std::map<HXUIOBJ, OpenScreen>::iterator it = g_screens.begin(); it != g_screens.end(); ++it)
        before.insert(it->first);
    HRESULT hr = OpenGameMenu(from, at);
    HXUIOBJ menu = NULL;
    for (std::map<HXUIOBJ, OpenScreen>::iterator it = g_screens.begin(); it != g_screens.end() && !menu; ++it)
        if (!before.count(it->first))
            menu = it->first;
    ListScreen* next = NULL;
    if (what == "achievements")
        next = new AchievementsScreen(tid);
    else if (what == "saves")
        next = new ContentScreen(tid, kSavesKind);
    else if (what == "updates")
        next = new ContentScreen(tid, kUpdatesKind);
    else if (what == "dlc")
        next = new ContentScreen(tid, kDlcKind);
    else if (what == "trainers")
        next = new TrainersScreen(tid);
    before.clear();
    for (std::map<HXUIOBJ, OpenScreen>::iterator it = g_screens.begin(); it != g_screens.end(); ++it)
        before.insert(it->first);
    HRESULT hr2 = next ? OpenListScreen(menu ? menu : from, next) : S_FALSE;
    HXUIOBJ screen = NULL;
    for (std::map<HXUIOBJ, OpenScreen>::iterator it = g_screens.begin(); it != g_screens.end() && !screen; ++it)
        if (!before.count(it->first))
            screen = it->first;
    // Both counted, so the blades go away as when they're opened in turn.
    Blades1888ScreensOpened(menu, screen, focusBefore);
    Log("blades.opengame %s: %S's menu %08X, %s %08X", v.c_str(), games[at].name.c_str(), (unsigned)hr, what.c_str(),
        (unsigned)hr2);
    return true;
}

// --- The game played last (Xbox LIVE blade's billboard: dashcomm#TitleMetadata.xur) ---
// SlopDash remembers the last game it started (launcher.cpp: recent.title).
struct RecentGame
{
    HXUIOBJ meta;
    AchievementRead* read;
    HANDLE thread;
    DWORD started;
};
std::map<HXUIOBJ, RecentGame> g_recent;

bool StartRecentGame(HXUIOBJ container)
{
    std::string v = GetSetting("recent.title");
    unsigned tid = v.empty() ? 0 : (unsigned)strtoul(v.c_str(), NULL, 16);
    const std::vector<library::Game>& games = Games();
    int at = -1;
    for (size_t i = 0; i < games.size() && tid; i++)
        if (games[i].titleId == tid)
            at = (int)i;
    if (at < 0)
        return false;
    HXUIOBJ meta = NULL;
    if (FAILED(AddScene(container, L"dashcomm", L"TitleMetadata.xur", 0, 0, &meta)) || !meta)
        return false;
    SetLabel(meta, L"labGameTitle", games[at].name);
    HXUIOBJ tile = Descendant(meta, L"imgGameTile");
    if (tile)
        XuiImageElementSetImagePath(tile, IconLocator((size_t)at).c_str());
    SetLabel(meta, L"labGamerPointsNum", L"");
    // The card shows the gamerscore line; the achievements count isn't kept.
    HXUIOBJ hide[] = { Descendant(meta, L"labAchievementsText"), Descendant(meta, L"labAchievementsNum") };
    for (int i = 0; i < 2; i++)
        if (hide[i])
            XuiElementSetShow(hide[i], FALSE);
    RecentGame r = { meta, NULL, NULL, GetTickCount() };
    int user = SignedInUser();
    if (user >= 0)
    {
        r.read = new AchievementRead;
        r.read->tid = tid;
        r.read->user = user;
        r.read->result = 0;
        r.thread = CreateThread(NULL, 0, ReadAchievementsThread, r.read, 0, NULL);
        if (!r.thread)
        {
            delete r.read;
            r.read = NULL;
        }
    }
    g_recent[container] = r;
    Log("recent game: %08X %S", tid, games[at].name.c_str());
    return true;
}

// The gamerscore earned in it, once the achievements are read (or 10 s pass).
bool PollRecentGame(HXUIOBJ container)
{
    std::map<HXUIOBJ, RecentGame>::iterator it = g_recent.find(container);
    if (it == g_recent.end())
        return true;
    RecentGame& r = it->second;
    if (!r.thread)
    {
        g_recent.erase(it);
        return true;
    }
    if (WaitForSingleObject(r.thread, 0) != WAIT_OBJECT_0)
    {
        if (GetTickCount() - r.started < 10000)
            return false;
        CloseHandle(r.thread);          // still reading: left to the thread
        g_recent.erase(it);
        return true;
    }
    CloseHandle(r.thread);
    unsigned cred = 0;
    for (size_t i = 0; i < r.read->list.size(); i++)
        if (r.read->list[i].earned)
            cred += r.read->list[i].cred;
    wchar_t b[16];
    _snwprintf_s(b, 16, _TRUNCATE, L"%u", cred);
    SetLabel(r.meta, L"labGamerPointsNum", b);
    delete r.read;
    g_recent.erase(it);
    return true;
}
