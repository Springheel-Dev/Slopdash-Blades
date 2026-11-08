#include "platform/settings.h"

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "platform/log.h"

namespace {

// One reader or writer at a time: the file is read and written whole, and the
// store's download thread and the library's background scan use it too.
CRITICAL_SECTION g_lock;
struct LockInit
{
    LockInit() { InitializeCriticalSection(&g_lock); }
} g_lockInit;
struct Guard
{
    Guard() { EnterCriticalSection(&g_lock); }
    ~Guard() { LeaveCriticalSection(&g_lock); }
};

const char* const kSettingsFile = "game:\\SlopDash.ini";

struct Line
{
    std::string key, value;
};

// The file is written whole each time, so a write stopped part way (the
// console turned off or SlopDash restarted as it saved) left it empty: every
// setting lost, the blades' colours with them. A save goes to SlopDash.ini.new
// first; the old file
// is kept as SlopDash.ini.bak until the new one is in its place, and read
// when there's no SlopDash.ini.
const char* const kSettingsNew = "game:\\SlopDash.ini.new";
const char* const kSettingsBak = "game:\\SlopDash.ini.bak";

std::vector<Line> Load()
{
    std::vector<Line> out;
    FILE* f = NULL;
    if ((fopen_s(&f, kSettingsFile, "r") != 0 || !f) && (fopen_s(&f, kSettingsBak, "r") != 0 || !f))
        return out;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f))
    {
        std::string s = buf;
        while (!s.empty() && (s[s.size() - 1] == '\n' || s[s.size() - 1] == '\r'))
            s.erase(s.size() - 1);
        size_t eq = s.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;
        Line l;
        l.key = s.substr(0, eq);
        l.value = s.substr(eq + 1);
        out.push_back(l);
    }
    fclose(f);
    return out;
}

bool Save(const std::vector<Line>& lines)
{
    FILE* f = NULL;
    if (fopen_s(&f, kSettingsNew, "w") != 0 || !f)
    {
        Log("settings: can't write %s", kSettingsNew);
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < lines.size(); i++)
        if (fprintf(f, "%s=%s\n", lines[i].key.c_str(), lines[i].value.c_str()) < 0)
            ok = false;
    if (fclose(f) != 0)
        ok = false;
    if (!ok)
    {
        Log("settings: writing %s failed; %s kept", kSettingsNew, kSettingsFile);
        DeleteFileA(kSettingsNew);
        return false;
    }
    // The old file aside, the new one in its place, then the old one gone. At
    // any moment one of SlopDash.ini or SlopDash.ini.bak is whole.
    DeleteFileA(kSettingsBak);
    bool had = MoveFileA(kSettingsFile, kSettingsBak) != FALSE;
    if (!MoveFileA(kSettingsNew, kSettingsFile))
    {
        Log("settings: can't put %s in place", kSettingsFile);
        if (had)
            MoveFileA(kSettingsBak, kSettingsFile);
        return false;
    }
    DeleteFileA(kSettingsBak);
    return true;
}

bool IsListKey(const std::string& key, const char* prefix)
{
    size_t n = strlen(prefix);
    return key.size() > n + 1 && !key.compare(0, n, prefix) && key[n] == '.' &&
           key.find_first_not_of("0123456789", n + 1) == std::string::npos;
}

}  // namespace

std::string GetSetting(const char* key)
{
    Guard g;
    std::vector<Line> lines = Load();
    for (size_t i = 0; i < lines.size(); i++)
        if (lines[i].key == key)
            return lines[i].value;
    return "";
}

bool SetSetting(const char* key, const std::string& value)
{
    Guard g;
    std::vector<Line> lines = Load(), kept;
    // A key set as it already is: nothing written (each write is a moment the
    // file can be caught part way).
    bool found = false;
    std::string now;
    for (size_t i = 0; i < lines.size(); i++)
        if (lines[i].key == key)
            found = true, now = lines[i].value;
    if (value.empty() ? !found : found && now == value)
        return true;
    for (size_t i = 0; i < lines.size(); i++)
        if (lines[i].key != key)
            kept.push_back(lines[i]);
    if (!value.empty())
    {
        Line l;
        l.key = key;
        l.value = value;
        kept.push_back(l);
    }
    return Save(kept);
}

std::vector<std::string> GetSettingList(const char* prefix, bool* exists)
{
    Guard g;
    std::vector<Line> lines = Load();
    std::vector<std::string> out;
    bool any = false;
    std::string countKey = std::string(prefix) + ".count";
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (lines[i].key == countKey)
            any = true;
        else if (IsListKey(lines[i].key, prefix))
        {
            any = true;
            out.push_back(lines[i].value);
        }
    }
    if (exists)
        *exists = any;
    return out;
}

bool SetSettingList(const char* prefix, const std::vector<std::string>& values)
{
    Guard g;
    std::vector<Line> lines = Load(), kept;
    std::string countKey = std::string(prefix) + ".count";
    for (size_t i = 0; i < lines.size(); i++)
        if (!IsListKey(lines[i].key, prefix) && lines[i].key != countKey)
            kept.push_back(lines[i]);
    // The count marks the list as set, even when it's empty.
    Line c;
    c.key = countKey;
    char n[16];
    _snprintf_s(n, sizeof(n), _TRUNCATE, "%u", (unsigned)values.size());
    c.value = n;
    kept.push_back(c);
    for (size_t i = 0; i < values.size(); i++)
    {
        Line l;
        char k[96];
        _snprintf_s(k, sizeof(k), _TRUNCATE, "%s.%u", prefix, (unsigned)(i + 1));
        l.key = k;
        l.value = values[i];
        kept.push_back(l);
    }
    return Save(kept);
}
