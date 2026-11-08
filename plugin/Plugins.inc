// --- Plugins
// ---------------------------------------------------------------------------------
// launch.ini lists SlopGuide alone (Xbdm too, while it's being moved over);
// SlopGuide loads the rest itself at start-up, as DashLaunch does
// (XexLoadImage, flags 8: a DLL; its DllMain runs), from its own list in
// Hdd:\SlopDash\Plugins.ini:
//
//   [Plugins]                      (loaded in this order)
//   MyPlugin = on, Hdd:\SlopDash\Plugins\MyPlugin\MyPlugin.xex
//   [Settings]
//   DebugTools = off
//
// The files are on the hard drive: SlopGuide's file use on the stick has hung
// before (Guide1888.inc kPicDirs). The Guide's Personal Settings > Plugins
// switches each one: on loads it there and then; off takes a restart (a
// running plugin's hooks can't be taken out safely), so it says so.
//
// Debug Tools: SlopGuide's own diagnostics (the Guide's timing log), off
// unless switched on.
namespace plugins {

extern "C" NTSTATUS XexLoadImage(LPCSTR name, DWORD flags, DWORD minVersion, PHANDLE module);

const char kList[] = "SlopDashPics:\\Plugins.ini";
const int kMax = 8;

struct Plugin
{
    char name[32];              // as listed ("MyPlugin")
    char path[160];             // as listed ("Hdd:\SlopDash\Plugins\MyPlugin\MyPlugin.xex")
    bool on;                    // listed on
    bool loaded;                // in memory (by us or before us)
};
Plugin g_list[kMax];
int g_count = 0;
bool g_debugTools = false;
volatile LONG g_ready = 0;      // the list read (the loader thread's first job)

const char* FileName(const char* path)
{
    const char* slash = strrchr(path, '\\');
    return slash ? slash + 1 : path;
}

bool InMemory(const Plugin& p)
{
    HANDLE h = NULL;
    return NT_OK(XexGetModuleHandle((PCHAR)FileName(p.path), &h)) && h;
}

// "Hdd:\a\b.xex" -> "\Device\Harddisk0\Partition1\a\b.xex". Only the hard
// drive's: "" for anything else.
void DevicePath(const char* path, char* out, size_t len)
{
    out[0] = 0;
    if (_strnicmp(path, "Hdd:\\", 5) == 0)
        _snprintf_s(out, len, _TRUNCATE, "\\Device\\Harddisk0\\Partition1\\%s", path + 5);
}

void Trim(char* s)
{
    char* a = s;
    while (*a == ' ' || *a == '\t')
        a++;
    memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = 0;
}

void Read()
{
    g_count = 0;
    g_debugTools = false;
    HANDLE h = CreateFileA(kList, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("plugins: no %s", kList);
        return;
    }
    static char text[4096];
    DWORD got = 0;
    ReadFile(h, text, sizeof(text) - 1, &got, NULL);
    CloseHandle(h);
    text[got] = 0;
    char section[32] = "";
    for (char* line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
    {
        char l[256];
        strncpy_s(l, sizeof(l), line, _TRUNCATE);
        Trim(l);
        if (!l[0] || l[0] == ';')
            continue;
        if (l[0] == '[')
        {
            strncpy_s(section, sizeof(section), l, _TRUNCATE);
            continue;
        }
        char* eq = strchr(l, '=');
        if (!eq)
            continue;
        *eq = 0;
        char key[64], value[200];
        strncpy_s(key, sizeof(key), l, _TRUNCATE);
        strncpy_s(value, sizeof(value), eq + 1, _TRUNCATE);
        Trim(key);
        Trim(value);
        if (!_stricmp(section, "[Settings]") && !_stricmp(key, "DebugTools"))
            g_debugTools = !_stricmp(value, "on");
        else if (!_stricmp(section, "[Plugins]") && g_count < kMax)
        {
            // "on, <path>" or "off, <path>"
            char* comma = strchr(value, ',');
            if (!comma)
                continue;
            *comma = 0;
            Plugin& p = g_list[g_count++];
            strncpy_s(p.name, sizeof(p.name), key, _TRUNCATE);
            strncpy_s(p.path, sizeof(p.path), comma + 1, _TRUNCATE);
            Trim(p.path);
            Trim(value);
            p.on = !_stricmp(value, "on");
            p.loaded = false;
        }
    }
    Log("plugins: %d listed, debug tools %s", g_count, g_debugTools ? "on" : "off");
}

void Write()
{
    static char text[4096];
    int n = _snprintf_s(text, sizeof(text), _TRUNCATE,
                        "; SlopGuide's plugins, loaded in this order at start-up (on or off, then the file).\r\n"
                        "; Switched in the Guide: Personal Settings > Plugins.\r\n[Plugins]\r\n");
    for (int i = 0; i < g_count && n > 0; i++)
        n += _snprintf_s(text + n, sizeof(text) - n, _TRUNCATE, "%s = %s, %s\r\n", g_list[i].name,
                         g_list[i].on ? "on" : "off", g_list[i].path);
    if (n > 0)
        n += _snprintf_s(text + n, sizeof(text) - n, _TRUNCATE, "[Settings]\r\nDebugTools = %s\r\n",
                         g_debugTools ? "on" : "off");
    if (n <= 0)
        return;
    HANDLE h = CreateFileA(kList, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("plugins: couldn't write %s", kList);
        return;
    }
    DWORD put = 0;
    WriteFile(h, text, (DWORD)n, &put, NULL);
    CloseHandle(h);
}

bool Load(Plugin& p)
{
    if (InMemory(p))
    {
        p.loaded = true;
        return true;
    }
    char device[200];
    DevicePath(p.path, device, sizeof(device));
    if (!device[0])
    {
        Log("plugins: %s: %s isn't on the hard drive: not loaded", p.name, p.path);
        return false;
    }
    HANDLE module = NULL;
    NTSTATUS s = XexLoadImage(device, 8, 0, &module);
    p.loaded = NT_OK(s);
    Log("plugins: %s (%s): %08X", p.name, device, (unsigned)s);
    return p.loaded;
}

DWORD WINAPI Loader(LPVOID)
{
    // The hard drive is there by now as a rule; a few tries in case it isn't.
    for (int i = 0; i < 20 && !FileExists("\\Device\\Harddisk0\\Partition1\\SlopDash\\Plugins.ini"); i++)
        Sleep(500);
    Read();
    g_ready = 1;
    for (int i = 0; i < g_count; i++)
    {
        if (g_list[i].on)
            Load(g_list[i]);
        else
            g_list[i].loaded = InMemory(g_list[i]);
    }
    FlushLog();
    return 0;
}

void Start()
{
    // (Install makes SlopDashPics: on 17559; plugins load on any system.)
    KSTRING link, device;
    RtlInitAnsiString(&link, "\\System??\\SlopDashPics:");
    RtlInitAnsiString(&device, "\\Device\\Harddisk0\\Partition1\\SlopDash");
    ObCreateSymbolicLink(&link, &device);
    HANDLE t = NULL;
    if (NT_OK(StartThread(&t, Loader, NULL)) && t)
        CloseHandle(t);
}

// The Guide's switches -------------------------------------------------------------

// "MyPlugin: On", "MyPlugin: Off", or (off while still running) "MyPlugin: Off (Restart)".
void Text(int i, WCHAR* out, size_t len)
{
    const Plugin& p = g_list[i];
    bool running = p.loaded || InMemory(p);
    _snwprintf_s(out, len, _TRUNCATE, L"%S: %s", p.name, p.on ? L"On" : running ? L"Off (Restart)" : L"Off");
}

void Switch(int i)
{
    if (i < 0 || i >= g_count)
        return;
    Plugin& p = g_list[i];
    p.on = !p.on;
    Write();
    if (p.on)
        Load(p);
    Log("plugins: %s switched %s", p.name, p.on ? "on" : "off");
}

// Every plugin on (Debug) or off (Performance).
void SwitchAll(bool on)
{
    for (int i = 0; i < g_count; i++)
    {
        g_list[i].on = on;
        if (on)
            Load(g_list[i]);
    }
    Write();
    Log("plugins: all switched %s", on ? "on" : "off");
}

void SwitchDebugTools()
{
    g_debugTools = !g_debugTools;
    Write();
    Log("plugins: debug tools %s", g_debugTools ? "on" : "off");
}

}  // namespace plugins
