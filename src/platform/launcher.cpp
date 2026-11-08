#include "platform/launcher.h"

#include <xtl.h>
#include <string.h>

#include "platform/console.h"
#include "platform/log.h"
#include "platform/settings.h"
#include "platform/trainers.h"

namespace {

bool EndsWith(const std::string& s, const char* tail)
{
    size_t n = strlen(tail);
    return s.size() >= n && !_stricmp(s.c_str() + s.size() - n, tail);
}

// A package's path as the system knows it (XContentLaunchImageFromFile). The library's paths use SlopDash's own
// drive names (main.cpp Mount: hdd:, usb:...), which only SlopDash sees: the
// system's launcher was given "hdd:\Content\..." and answered 2, file not
// found, so no game started.
std::string DevicePath(const std::string& path)
{
    static const struct { const char* drive; const char* device; } kDrives[] = {
        { "hdd:", "\\Device\\Harddisk0\\Partition1" }, { "usb:", "\\Device\\Mass0" },
        { "flash:", "\\Device\\Flash" }, { "sysext:", "\\Device\\Harddisk0\\SystemExtPartition" } };
    for (size_t i = 0; i < sizeof(kDrives) / sizeof(kDrives[0]); i++)
    {
        size_t n = strlen(kDrives[i].drive);
        if (path.size() > n && !_strnicmp(path.c_str(), kDrives[i].drive, n))
            return kDrives[i].device + path.substr(n);
    }
    return path;
}

}  // namespace

void LaunchGame(const library::Game& g)
{
    Log("launch: %08X %S from %s", g.titleId, g.name.c_str(), g.path.c_str());
    if (g.path.empty())
        return;
    // The Xbox LIVE blade shows the game played last.
    char tid[12];
    _snprintf_s(tid, sizeof(tid), _TRUNCATE, "%08X", g.titleId);
    SetSetting("recent.title", tid);
    // The trainer chosen for it (game menu > Trainers), loaded first: it waits
    // in the background for the game. If it can't be, the game starts anyway
    // (as Aurora does).
    std::wstring why;
    if (g.titleId && !trainers::LoadFor(g.titleId, why))
        console::Notify(why, true);
    if (EndsWith(g.path, ".xex") || EndsWith(g.path, ".xbe"))
    {
        // An extracted game: the loader starts the executable itself (an
        // original Xbox .xbe runs under the console's emulator). By SlopDash's
        // own path: given the device path, VBA360 and 360sweeper both fell
        // back to the console's dashboard (2026-11-04).
        Log("launch: XLaunchNewImage(%s)", g.path.c_str());
        LogFlush();
        XLaunchNewImage(g.path.c_str(), 0);
        Log("launch: XLaunchNewImage returned");
        return;
    }
    // A package (GOD install, Arcade, original Xbox as GOD): the content
    // system mounts it and starts the executable inside.
    std::string path = DevicePath(g.path);
    const char* image = g.kind == library::kXboxOriginal ? "default.xbe" : "default.xex";
    Log("launch: XContentLaunchImageFromFile(%s, %s)", path.c_str(), image);
    LogFlush();
    DWORD r = XContentLaunchImageFromFile(path.c_str(), image);
    Log("launch: XContentLaunchImageFromFile(%s) failed: %08X", image, (unsigned)r);
}
