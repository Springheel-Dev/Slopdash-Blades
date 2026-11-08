// The homebrew store's screens (the Marketplace blade's tiles and links),
// on the list screens' layout (ListScreen above): a shelf's items with each
// one's details beside them, the download queue, and the repos. The data and
// the downloads are platform/store.cpp's, on threads of their own; the
// screens show snapshots and redraw when store::Generation() moves on.
// Included by system_settings.inc.

namespace storeui {

std::wstring Bytes(unsigned long long n)
{
    wchar_t b[32];
    if (n >= 1024ull * 1024 * 1024)
        _snwprintf_s(b, 32, _TRUNCATE, L"%.1f GB", n / 1073741824.0);
    else if (n >= 1024 * 1024)
        _snwprintf_s(b, 32, _TRUNCATE, L"%.1f MB", n / 1048576.0);
    else
        _snwprintf_s(b, 32, _TRUNCATE, L"%u KB", (unsigned)((n + 1023) / 1024));
    return b;
}

bool SetColorProperty(HXUIOBJ obj, const wchar_t* name, unsigned argb)
{
    DWORD id = 0;
    if (!obj || FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetColorFromUint(&v, argb);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

// The red warning over the details (an Aurora-only item, or one SlopDash
// can't install): listed with a warning in red, not hidden.
// The details move down under it while it shows.
void ShowWarning(HXUIOBJ scene, const std::wstring& text)
{
    HXUIOBJ warn = Child(scene, L"rdStoreWarning");
    if (!warn && !text.empty() && SUCCEEDED(XuiCreateObject(L"XuiText", &warn)) && warn)
    {
        SetStringProperty(warn, L"Id", L"rdStoreWarning");
        XuiElementSetBounds(warn, 400, 34);
        D3DXVECTOR3 pos(589, 234, 0);
        XuiElementSetPosition(warn, &pos);
        SetColorProperty(warn, L"TextColor", 0xFFFF3B30);
        SetFloatProperty(warn, L"PointSize", 15.0f);
        SetUintProperty(warn, L"TextStyle", XUI_FONT_STYLE_NORMAL);     // (wrapped: made in code it didn't)
        XuiElementAddChild(scene, warn);
    }
    if (warn)
    {
        XuiTextElementSetText(warn, text.c_str());
        XuiElementSetShow(warn, !text.empty());
    }
    if (HXUIOBJ body = Child(scene, L"labMetaBody"))
    {
        D3DXVECTOR3 pos(579, text.empty() ? 231.0f : 268.0f, 0);
        XuiElementSetPosition(body, &pos);
        XuiElementSetBounds(body, 415, text.empty() ? 367.0f : 330.0f);
    }
}

HRESULT OpenDownloads(HXUIOBJ from);
HRESULT OpenRepos(HXUIOBJ from);

// --- a shelf (one of the hub's tiles) -------------------------------------------
class ShelfScreen : public ListScreen
{
public:
    ShelfScreen(store::Shelf shelf) : m_shelf(shelf), m_seen(0), m_previewed(-2)
    {
        store::LoadShelf(shelf);
    }
    const wchar_t* Header() { return store::ShelfName(m_shelf); }
    const wchar_t* CurrentHeading() { return L"Available"; }
    std::wstring Current()
    {
        if (m_state.loading && m_state.items.empty())
            return L"Reading the repos...";
        wchar_t b[64];
        _snwprintf_s(b, 64, _TRUNCATE, m_state.items.size() == 1 ? L"%u item%s" : L"%u items%s",
                     (unsigned)m_state.items.size(), m_state.loading ? L", more coming" : L"");
        std::wstring s = b;
        if (m_state.updates)
        {
            _snwprintf_s(b, 64, _TRUNCATE, m_state.updates == 1 ? L"%d update" : L"%d updates", m_state.updates);
            s += L", " + std::wstring(b);
        }
        return s;
    }
    void Rebuild()
    {
        m_seen = store::Generation();
        m_state = store::GetShelf(m_shelf);
        items.clear();
        for (size_t i = 0; i < m_state.items.size(); i++)
        {
            // (what's on the console marked: Update, or Installed)
            const store::Item& it = m_state.items[i];
            std::wstring row = it.version.empty() ? it.title : it.title + L"  " + it.version;
            if (it.installed == store::kUpdateAvailable)
                row += L"  (Update)";
            else if (it.installed == store::kInstalled)
                row += L"  (Installed)";
            items.push_back(row);
        }
        if (items.empty())
            items.push_back(m_state.loading ? L"Loading..."
                            : m_shelf == store::kThemes ? L"No SlopDash themes yet"
                                                        : L"Nothing found");
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_state.items.size())
        {
            if (m_state.loading)
                return L"Reading the store's repos over the internet.";
            if (m_shelf == store::kThemes)
                return L"Themes made for SlopDash will be listed here, from SlopDash's own repo and any you add "
                       L"(their category type SlopDashTheme). None are published yet.";
            std::vector<store::Repo> repos = store::Repos();
            bool any = false;
            for (size_t r = 0; r < repos.size(); r++)
                any = any || repos[r].on;
            if (!any)
                return L"No Repos Ticked. Tick One in Manage Repos";
            return m_state.error.empty() ? L"The repos have nothing for this shelf." : m_state.error;
        }
        const store::Item& it = m_state.items[i];
        std::wstring s;
        if (it.installed == store::kUpdateAvailable)
            s += L"Update Available. Installed: " + it.installedVersion + L"\r\n";
        else if (it.installed == store::kInstalled)
            s += it.installedVersion.empty() ? std::wstring(L"Installed\r\n")
                                             : L"Installed: " + it.installedVersion + L"\r\n";
        if (!it.author.empty())
            s += L"By " + it.author + L"\r\n";
        if (!it.size.empty())
            s += L"Size: " + it.size + L"\r\n";
        if (it.destination.size())
            s += L"Installs to: " + Widen(it.destination) + L"\r\n";
        s += L"From: " + it.repo + L"\r\n\r\n";
        // (the panel holds so much: a long description is cut at a word, the
        // red warning taking the panel's top when there is one)
        size_t room = it.blocked.empty() ? 300 : 220;
        std::wstring d = it.description;
        if (d.size() > room)
        {
            size_t cut = d.find_last_of(L' ', room);
            d = d.substr(0, cut == std::wstring::npos || cut < room / 2 ? room : cut) + L"...";
        }
        return s + d;
    }
    void Preview(HXUIOBJ scene, int i)
    {
        // A first A on another item doesn't carry over (and the "Press A again"
        // the panel showed is gone: About redraws it). Only a move: a refresh
        // of the same item keeps it.
        if (i != m_previewed)
            m_confirm.Forget();
        m_previewed = i;
        ShowWarning(scene, i >= 0 && i < (int)m_state.items.size() ? m_state.items[i].blocked : L"");
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_state.items.size())
            return;
        const store::Item& it = m_state.items[i];
        if (!it.blocked.empty())
            return;                         // (the red warning says why)
        // (no time limit: the second A counts for as long as "Press A again"
        // shows - after 5 s it didn't, the message stayed, and the user's
        // VBA360 install never started, 2026-11-02)
        if (!m_confirm.Ask(i, 0))
        {
            if (it.installed == store::kUpdateAvailable)
                SetStatus(scene, L"Press A again to update " + it.title + L" to " + it.version + L".");
            else if (it.installed == store::kInstalled)
                SetStatus(scene, L"Press A again to install " + it.title + L" again.");
            else
                SetStatus(scene, L"Press A again to download " + it.title + L" and install it to " +
                                     Widen(it.destination) + L".");
            return;
        }
        int job = store::Install(it);
        Log("store: install %S: job %d", it.title.c_str(), job);
        SetStatus(scene, job >= 0 ? it.title + L" is downloading. Y shows the downloads."
                                  : it.title + L" can't be installed.");
    }
    const wchar_t* XLabel() { return L"Refresh"; }
    void X(HXUIOBJ scene, int)
    {
        store::LoadShelf(m_shelf, true);
        SetStatus(scene, L"Reading the repos again...");
    }
    const wchar_t* YLabel() { return L"Active Downloads"; }
    void Y(HXUIOBJ scene, int) { OpenDownloads(scene); }
    void Tick(HXUIOBJ)
    {
        if (store::Generation() != m_seen)
            g_generation++;                 // (the list screen redraws)
    }

private:
    store::Shelf m_shelf;
    store::ShelfState m_state;
    unsigned m_seen;
    Confirm m_confirm;
    int m_previewed;                // the item last shown (-2 none yet)
};

// --- Active Downloads ------------------------------------------------------------
class DownloadsScreen : public ListScreen
{
public:
    DownloadsScreen() : m_seen(0) {}
    const wchar_t* Header() { return L"Active Downloads"; }
    const wchar_t* CurrentHeading() { return L"Downloads"; }
    std::wstring Current()
    {
        wchar_t b[48];
        int active = store::ActiveJobs();
        _snwprintf_s(b, 48, _TRUNCATE, active == 1 ? L"%d in progress" : L"%d in progress", active);
        return b;
    }
    void Rebuild()
    {
        m_seen = store::Generation();
        m_jobs = store::Jobs();
        std::reverse(m_jobs.begin(), m_jobs.end());     // the newest first
        items.clear();
        for (size_t i = 0; i < m_jobs.size(); i++)
            items.push_back(m_jobs[i].title + L"  " + StateText(m_jobs[i]));
        if (items.empty())
            items.push_back(L"No downloads");
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_jobs.size())
            return L"Items you install from the Marketplace's shelves are downloaded and unpacked here, one at a "
                   L"time, while you carry on.";
        const store::Job& j = m_jobs[i];
        std::wstring s = j.title + L"\r\n" + StateText(j) + L"\r\n\r\nTo: " + Widen(j.destination);
        if (j.parts > 1)
        {
            wchar_t b[32];
            _snwprintf_s(b, 32, _TRUNCATE, L"\r\nPart %d of %d", j.part, j.parts);
            s += b;
        }
        if (!j.message.empty())
            s += L"\r\n\r\n" + j.message;
        return s;
    }
    void Press(HXUIOBJ, int) {}
    const wchar_t* XLabel()
    {
        for (size_t i = 0; i < m_jobs.size(); i++)
            if (Running(m_jobs[i]))
                return L"Cancel";
        return NULL;
    }
    void X(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_jobs.size() || !Running(m_jobs[i]))
            return;
        store::Cancel(m_jobs[i].id);
        SetStatus(scene, m_jobs[i].title + L": cancelling.");
    }
    void Tick(HXUIOBJ)
    {
        if (store::Generation() != m_seen)
            g_generation++;
    }

private:
    static bool Running(const store::Job& j)
    {
        return j.state == store::kQueued || j.state == store::kDownloading || j.state == store::kUnpacking;
    }
    static std::wstring StateText(const store::Job& j)
    {
        switch (j.state)
        {
        case store::kQueued: return L"Waiting";
        case store::kDownloading:
        {
            if (!j.total)
                return L"Downloading, " + Bytes(j.done);
            wchar_t b[64];
            _snwprintf_s(b, 64, _TRUNCATE, L"%u%% (%s of %s)", (unsigned)(j.done * 100 / j.total),
                         Bytes(j.done).c_str(), Bytes(j.total).c_str());
            return b;
        }
        case store::kUnpacking: return L"Unpacking";
        case store::kDone: return L"Installed";
        case store::kFailed: return L"Failed";
        default: return L"Cancelled";
        }
    }
    std::vector<store::Job> m_jobs;
    unsigned m_seen;
};

// --- Manage Repos: the repos the shelves read ---------------------------------------
class ReposScreen : public ListScreen
{
public:
    ReposScreen() : m_typing(false)
    {
        memset(&m_ov, 0, sizeof(m_ov));
        memset(m_text, 0, sizeof(m_text));
    }
    const wchar_t* Header() { return L"Manage Repos"; }
    const wchar_t* CurrentHeading() { return L"Repos"; }
    std::wstring Current()
    {
        unsigned on = 0;
        for (size_t i = 0; i < m_repos.size(); i++)
            on += m_repos[i].on;
        wchar_t b[48];
        _snwprintf_s(b, 48, _TRUNCATE, m_repos.size() == 1 ? L"%u repo, %u ticked" : L"%u repos, %u ticked",
                     (unsigned)m_repos.size(), on);
        return b;
    }
    // The repos, each with its tickbox (every row of 1888's checked list has
    // one, so adding a repo is Y's, not a row's).
    void Rebuild()
    {
        m_repos = store::Repos();
        items.clear();
        for (size_t i = 0; i < m_repos.size(); i++)
            items.push_back(m_repos[i].name);
    }
    bool Tickboxes() { return true; }
    int Ticked(int i) { return i < 0 || i >= (int)m_repos.size() ? -1 : m_repos[i].on ? 1 : 0; }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_repos.size())
            return L"";
        const store::Repo& r = m_repos[i];
        return Widen(r.url) + L"\r\n\r\nTicked repos fill the shelves. Y adds one: the address of its .ini "
                              L"file (the files Aurora's Homebrew Store reads).";
    }
    void Press(HXUIOBJ, int i)
    {
        if (i < 0 || i >= (int)m_repos.size())
            return;
        const store::Repo& r = m_repos[i];
        store::SetRepoOn(r.url, !r.on);
        Log("store: repo %s %s", r.url.c_str(), r.on ? "unticked" : "ticked");
        g_generation++;
    }
    const wchar_t* YLabel() { return L"Add Repo"; }
    void Y(HXUIOBJ scene, int)
    {
        if (m_typing)
            return;
        memset(&m_ov, 0, sizeof(m_ov));
        wcscpy_s(m_text, L"https://");
        int user = SignedInUser();
        DWORD r = ShowKeyboard(user < 0 ? 0 : user, VKBD_DEFAULT, L"https://", L"Add Repo",
                                  L"The full address of the repo's .ini file.", m_text, ARRAYSIZE(m_text), &m_ov);
        m_typing = r == ERROR_IO_PENDING || r == ERROR_SUCCESS;
        if (!m_typing)
            SetStatus(scene, L"The keyboard couldn't be opened.");
    }
    const wchar_t* XLabel() { return L"Remove"; }
    void X(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_repos.size())
            return;
        const store::Repo& r = m_repos[i];
        if (r.offered)
        {
            SetStatus(scene, L"Cannot Remove. Untick It Instead");
            return;
        }
        if (!m_confirm.Ask(i))
        {
            SetStatus(scene, L"Press X again to remove " + r.name + L".");
            return;
        }
        store::RemoveRepo(r.url);
        g_generation++;
        SetStatus(scene, r.name + L" removed.");
    }
    void Tick(HXUIOBJ scene)
    {
        if (!m_typing || !XHasOverlappedIoCompleted(&m_ov))
            return;
        m_typing = false;
        if (XGetOverlappedExtendedError(&m_ov) != ERROR_SUCCESS || !m_text[0])
            return;
        std::string url;
        for (const wchar_t* c = m_text; *c; c++)
            url += *c < 128 ? (char)*c : '?';
        bool ok = store::AddRepo(url);
        Log("store: add repo %s: %s", url.c_str(), ok ? "added" : "refused");
        g_generation++;
        SetStatus(scene, ok ? L"Added. Its items show on the shelves next time they're opened."
                            : L"That isn't an http(s) address, or the repo is already there.");
    }

private:
    std::vector<store::Repo> m_repos;
    bool m_typing;
    XOVERLAPPED m_ov;
    wchar_t m_text[512];
    Confirm m_confirm;
};

HRESULT OpenDownloads(HXUIOBJ from)
{
    return OpenListScreen(from, new DownloadsScreen());
}

HRESULT OpenRepos(HXUIOBJ from)
{
    return OpenListScreen(from, new ReposScreen());
}

}  // namespace storeui
