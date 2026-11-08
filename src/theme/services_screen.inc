// Join a Service (System > Network Settings): online
// services the console can use through SlopDash. XLink Kai (system link over
// the internet: its engine runs on a PC, Mac or Raspberry Pi on the same wired
// network; the console needs a wired connection and, for players over 30 ms
// away, DashLaunch's pingpatch: teamxlink.co.uk's Xbox 360 setup guides). For
// now pressing it says it's in progress.
//
// Included by system_settings.inc, after the list screens.

// --- DashLaunch's launch.ini: its pingpatch -------------------------------------------
// DashLaunch reads launch.ini from the USB drive, then the hard drive; its
// [Settings] pingpatch = true removes the 30 ms system link ping limit. Read at
// the console's start: a change needs a restart.
namespace services {

const char* const kIniPaths[] = { "usb:\\launch.ini", "hdd:\\launch.ini" };

bool ReadText(const char* path, std::string& out)
{
    out.clear();
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    if (size != INVALID_FILE_SIZE && size < 256 * 1024)
    {
        out.resize(size);
        if (size && (!ReadFile(h, &out[0], size, &got, NULL) || got != size))
            out.clear();
    }
    CloseHandle(h);
    return !out.empty();
}

bool WriteText(const char* path, const std::string& text)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD put = 0;
    BOOL ok = WriteFile(h, text.data(), (DWORD)text.size(), &put, NULL) && put == text.size();
    CloseHandle(h);
    return ok != FALSE;
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string Lower(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] += 'a' - 'A';
    return s;
}

// The launch.ini DashLaunch uses (the first found), or -1.
int IniFile(std::string& text)
{
    for (int i = 0; i < 2; i++)
        if (ReadText(kIniPaths[i], text))
            return i;
    return -1;
}

// pingpatch in [Settings]: 1 on, 0 off (or not there: DashLaunch's default
// is off), -1 no launch.ini.
int PingPatch()
{
    std::string text;
    if (IniFile(text) < 0)
        return -1;
    bool settings = false;
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = Trim(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line[0] == ';')
            continue;
        if (line[0] == '[')
        {
            settings = Lower(line) == "[settings]";
            continue;
        }
        size_t eq = line.find('=');
        if (settings && eq != std::string::npos && Lower(Trim(line.substr(0, eq))) == "pingpatch")
            return Lower(Trim(line.substr(eq + 1))) == "true" ? 1 : 0;
    }
    return 0;
}

// Sets pingpatch in [Settings] (the line changed in place, or added after
// [Settings]); the file as it was kept once as launch.ini.slopdash.bak.
bool SetPingPatch(bool on)
{
    std::string text;
    int which = IniFile(text);
    if (which < 0)
        return false;
    std::string backup = std::string(kIniPaths[which]) + ".slopdash.bak";
    if (GetFileAttributesA(backup.c_str()) == (DWORD)-1)
        WriteText(backup.c_str(), text);
    const char* value = on ? "true" : "false";
    std::string out;
    bool settings = false, done = false;
    size_t at = 0;
    size_t settingsEnd = std::string::npos;     // just after the [Settings] line
    while (at < text.size())
    {
        size_t end = text.find('\n', at);
        size_t next = end == std::string::npos ? text.size() : end + 1;
        std::string raw = text.substr(at, next - at);
        std::string line = Trim(raw);
        at = next;
        if (!line.empty() && line[0] == '[')
        {
            settings = Lower(line) == "[settings]";
            out += raw;
            if (settings)
                settingsEnd = out.size();
            continue;
        }
        size_t eq = line.find('=');
        if (!done && settings && !line.empty() && line[0] != ';' && eq != std::string::npos &&
            Lower(Trim(line.substr(0, eq))) == "pingpatch")
        {
            out += std::string("pingpatch = ") + value + "\r\n";
            done = true;
            continue;
        }
        out += raw;
    }
    if (!done)
    {
        std::string line = std::string("pingpatch = ") + value + "\r\n";
        if (settingsEnd != std::string::npos)
            out.insert(settingsEnd, line);
        else
            out += std::string("\r\n[Settings]\r\n") + line;
    }
    bool ok = WriteText(kIniPaths[which], out);
    Log("services: pingpatch = %s in %s: %s", value, kIniPaths[which], ok ? "written" : "FAILED");
    return ok;
}

}  // namespace services

// --- XLink Kai ------------------------------------------------------------------------
class XLinkKaiScreen : public ListScreen
{
public:
    XLinkKaiScreen() : m_confirmAt(0) {}
    const wchar_t* Header() { return L"XLink Kai"; }
    const wchar_t* CurrentHeading() { return L"Status"; }
    std::wstring Current() { return Wired() && services::PingPatch() == 1 ? L"Ready" : L"Not Ready"; }
    void Rebuild()
    {
        items.clear();
        DWORD link = XNetGetEthernetLinkStatus();
        bool up = (link & XNET_ETHERNET_LINK_ACTIVE) != 0, wireless = (link & XNET_ETHERNET_LINK_WIRELESS) != 0;
        items.push_back(!up ? L"Connection: Not Connected" : wireless ? L"Connection: Wireless" : L"Connection: Wired");
        int ping = services::PingPatch();
        items.push_back(ping < 0 ? L"Ping Patch: No launch.ini" : ping ? L"Ping Patch: On" : L"Ping Patch: Off");
        items.push_back(L"How to Play");
    }
    std::wstring About(int i)
    {
        if (i == 0)
            return L"System link needs a network cable: the wireless adapter isn't supported.";
        if (i == 1)
            return L"DashLaunch's ping patch lets you play people over 30 ms away. Takes effect when the console "
                   L"restarts.\r\n\r\nA: switch.";
        return L"1. Install XLink Kai on a PC, Mac or Raspberry Pi wired to the same network (teamxlink.co.uk).\r\n"
               L"2. Sign in to its web page and open the game's arena.\r\n"
               L"3. In the game: System Link.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i != 1)
            return;
        int ping = services::PingPatch();
        if (ping < 0)
        {
            SetStatus(scene, L"No launch.ini. Is DashLaunch installed?");
            return;
        }
        // (DashLaunch's setting, for every game: a second A within 5 s)
        if (GetTickCount() - m_confirmAt > 5000)
        {
            m_confirmAt = GetTickCount();
            SetStatus(scene, ping ? L"Press A again to switch the ping patch off." : L"Press A again to switch the ping patch on.");
            return;
        }
        m_confirmAt = 0;
        bool ok = services::SetPingPatch(!ping);
        g_generation++;
        SetStatus(scene, !ok ? L"Cannot Save. launch.ini Not Writable" : !ping ? L"Ping Patch On. Restart the Console."
                                                                              : L"Ping Patch Off. Restart the Console.");
    }

private:
    static bool Wired()
    {
        DWORD link = XNetGetEthernetLinkStatus();
        return (link & XNET_ETHERNET_LINK_ACTIVE) && !(link & XNET_ETHERNET_LINK_WIRELESS);
    }
    DWORD m_confirmAt;
};

// --- Join a Service -------------------------------------------------------------------
class ServicesScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Join a Service"; }
    const wchar_t* CurrentHeading() { return L"Services"; }
    std::wstring Current() { return L"Online Play"; }
    void Rebuild()
    {
        // (Insignia taken out: original Xbox only.)
        items.clear();
        items.push_back(L"XLink Kai");
    }
    std::wstring About(int)
    {
        return L"System link games over the internet, through a PC, Mac or Raspberry Pi on your network.";
    }
    void Press(HXUIOBJ scene, int)
    {
        // (XLinkKaiScreen, the readiness checks, is held back until Kai works
        // end to end.)
        SetStatus(scene, L"XLink Kai: In Progress");
    }
};

