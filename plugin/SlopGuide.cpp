// SlopGuide: a DashLaunch plugin that gives the system Guide SlopDash's
// theme skin (for Blades, the 2.0.6717 Blades Guide's), on system 2.0.17559
// only. Copyright Jakben Imbel.
//
// Blades (launch), 0.16: with Guide\blades1888.xzp in place (tools/guide1888.py:
// 2.0.1888's Guide blade, screens, skin and art, in 17559's scene format), the
// console's Guide draws the 1888 Guide everywhere, games included, and none of
// 17559's: see Rebuild1888 and SlopSceneCreate.
//
// How the Guide gets its skin (xam.xex 17559): at start-up a work item on the
// Guide's thread loads \SystemRoot\huduiskin.xex, asks a locator builder
// (0x816c8a48) for "section://<module>,skin#skin.xur", hands that to XUI's
// skin loader, then unloads the module. SlopGuide changes the one call to the
// builder (0x816cea2c) into a call to its own: if SlopDash has put a skin at
// Usb:\Apps\SlopDash\Guide\skin.xzp, the locator becomes
// "file://SlopGuide:/skin.xzp#skin.xur"; if not (no skin chosen, or the
// stick not ready yet) it calls xam's own builder and the Guide is untouched.
//
// Safety: nothing is changed unless every word it relies on is exactly the
// 17559 one; the change is one aligned 32-bit store (a bl), so no thread can
// run half of it. Only the next skin load is affected: xam does it once, at
// start-up, so a new skin shows after a restart.
//
// What happened is kept in memory (g_log).

#include <xtl.h>
#include <xmp.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
#define NT_OK(s) ((NTSTATUS)(s) >= 0)

typedef struct { USHORT Length, MaximumLength; PCHAR Buffer; } KSTRING;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } KUNICODE_STRING;
typedef struct { HANDLE RootDirectory; KSTRING* ObjectName; ULONG Attributes; } KOBJECT_ATTRIBUTES;
typedef struct { NTSTATUS Status; ULONG_PTR Information; } KIO_STATUS_BLOCK;
typedef struct {
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER AllocationSize, EndOfFile;
    ULONG FileAttributes, Pad;
} KFILE_ATTRS;

// The start of a module's loader entry.
typedef struct {
    LIST_ENTRY InLoadOrderLinks, InClosureOrderLinks, InInitializationOrderLinks;
    PVOID NtHeadersBase, ImageBase;
    DWORD SizeOfNtImage;
    KUNICODE_STRING FullDllName;
} KLDR_ENTRY;

extern "C" {
    NTSTATUS ExCreateThread(PHANDLE, DWORD, LPDWORD, PVOID, LPTHREAD_START_ROUTINE, LPVOID, DWORD);
    // xapilib: the start-up XAPI's own threads get (CreateThread passes it).
    VOID XapiThreadStartup();
    NTSTATUS XexGetModuleHandle(PCHAR, PHANDLE);
    VOID     RtlInitAnsiString(KSTRING*, const char*);
    LONG     ObCreateSymbolicLink(KSTRING*, KSTRING*);
    NTSTATUS NtCreateFile(PHANDLE, ACCESS_MASK, KOBJECT_ATTRIBUTES*, KIO_STATUS_BLOCK*, LARGE_INTEGER*,
                          DWORD, DWORD, DWORD, DWORD);
    NTSTATUS NtWriteFile(HANDLE, HANDLE, PVOID, PVOID, KIO_STATUS_BLOCK*, PVOID, DWORD, LARGE_INTEGER*);
    NTSTATUS NtQueryFullAttributesFile(KOBJECT_ATTRIBUTES*, KFILE_ATTRS*);
    NTSTATUS NtClose(HANDLE);
    NTSTATUS NtDeleteFile(KOBJECT_ATTRIBUTES*);
}

// Every thread SlopGuide starts: a system thread (it outlives the title
// running at the time), started through XAPI's own start-up as CreateThread's
// threads are. Without that start-up the threads called Sleep and file
// functions with no XAPI thread state, and after a game had run and quit
// they stopped waking: the Guide's notification thread and another
// plugin's mailbox all stuck in their sleep at once (2026-11-05).
NTSTATUS StartThread(PHANDLE handle, LPTHREAD_START_ROUTINE start, LPVOID parameter)
{
    return ExCreateThread(handle, 0, NULL, (PVOID)XapiThreadStartup, start, parameter, 2);
}

#define K_FILE_OVERWRITE_IF            5
#define K_FILE_SYNCHRONOUS_IO_NONALERT 0x20
#define K_FILE_NON_DIRECTORY_FILE      0x40

// --- xam.xex 2.0.17559 ------------------------------------------------------
const DWORD kXamBase = 0x815F0000, kXamSize = 0x610000;
const DWORD kCallSite = 0x816CEA2C;         // bl Builder, in the Guide's start-up work item
const DWORD kCallWord = 0x4BFFA01D;         //   = bl 0x816C8A48
const DWORD kBuilder = 0x816C8A48;          // HRESULT (HANDLE, LPCWSTR name, LPCWSTR file, LPWSTR out, DWORD len)
const DWORD kBuilderFirst = 0x7D8802A6;     //   mflr r12
const DWORD kSkinPath = 0x815FA410;         // "\SystemRoot\huduiskin.xex"
const DWORD kSkinStarted = 0x81AA1B58;      // set by the start-up work item near its end
// xam's task API, as the Guide's start-up (0x816CEBD8) uses it to run on the Guide's side.
const DWORD kTaskCreate = 0x816BA668;       // HRESULT (DWORD* task)
const DWORD kTaskSet = 0x816BA6B0;          // (DWORD task, DWORD what, fn, context, const DWORD* params)
const DWORD kTaskStart = 0x816B7B68;        // (DWORD task)
const DWORD kSkinLoad = 0x817C62C0;         // HRESULT (LPCWSTR url, LPCWSTR prefix): XuiLoadVisualFromBinary
const DWORD kFreeVisuals = 0x817C6170;      // void (LPCWSTR prefix): XuiFreeVisuals (xam ordinal 808h); NULL = all
// Scene creation (xam ordinal 357h, what hud.xex creates GuideMain.xur with): a
// wrapper - mr r8,r6 / li r7,0 / li r6,0 / b 817C47D8 - around the full version.
const DWORD kSceneCreate = 0x817C4A58;      // HRESULT (LPCWSTR base, LPCWSTR path, PVOID init, HXUIOBJ*)
const DWORD kSceneCreateEx = 0x817C47D8;    // the same with two more (0, 0) before the out pointer

// The Guide's frame (xam#hudbkgnd.xur, class HUDBkgndScene). The start-up work
// item creates it once (816929B8, right after the skin), before DashLaunch has
// started SlopGuide; Rebuild1888 makes it again.
const DWORD kFrameCreate = 0x816929B8;      // HRESULT (frame object, unused)
const DWORD kFrameInstance = 0x81A9DE68;    // the HUDBkgndScene instance (its init stores it)
const DWORD kXuiDestroy = 0x817B40D0;       // HRESULT XuiDestroyObject(HXUIOBJ)
const DWORD kXuiChildById = 0x817BD708;     // HRESULT XuiElementGetChildById(HXUIOBJ, LPCWSTR, HXUIOBJ*)

// The words around the call, as 17559 has them: the version check.
struct Word { DWORD at, value; };
const Word kExpect[] = {
    { 0x816CEA14, 0x80610050 },             // lwz r3, 80(r1)       the module
    { 0x816CEA1C, 0x38ABA6C0 },             // addi r5, r11, "skin.xur"
    { 0x816CEA20, 0x388AA684 },             // addi r4, r10, "skin"
    { 0x816CEA24, 0x38E00080 },             // li r7, 128
    { 0x816CEA28, 0x38C10060 },             // addi r6, r1, 96
    { kCallSite, kCallWord },
    { 0x816CEA30, 0x38800000 },             // li r4, 0
    { 0x816CEA34, 0x38610060 },             // addi r3, r1, 96      the locator, to the skin loader
    { kBuilder, kBuilderFirst },
    { kTaskCreate, 0x7D8802A6 },            // mflr r12
    { kTaskSet, 0x5463003C },               // rlwinm r3, r3, 0, 0, 30
    { kTaskStart, 0x3D608160 },             // lis r11, 0x8160
    { kSkinLoad, 0x7D8802A6 },              // mflr r12
    { kFreeVisuals, 0x7D8802A6 },           // mflr r12
    { kFreeVisuals + 0xC, 0x3D6081AD },     // lis r11, 0x81AD (its lock)
    { kSceneCreate, 0x7CC83378 },           // mr r8, r6
    { kSceneCreate + 4, 0x38E00000 },       // li r7, 0
    { kSceneCreate + 8, 0x38C00000 },       // li r6, 0
    { kSceneCreate + 12, 0x4BFFFD74 },      // b 817C47D8
    { kFrameCreate, 0x7D8802A6 },           // mflr r12
    { kFrameCreate + 0x24, 0x806B1544 },    // lwz r3, 81A81544 (L"hudbkgnd.xur")
    { 0x81A81544, 0x815F45BC },             //   the table entry: the file name
    { 0x816929F4, 0x48132065 },             // bl 817C4A58: made through the wrapper we watch
    { 0x81692A24, 0x4812ACE5 },             // bl XuiElementGetChildById (AppHostElementId)
    { 0x81692A5C, 0x48121675 },             // bl XuiDestroyObject (its failure path)
    { 0x81694944, 0x815D0000 },             // the class init: lwz r10, 0(r29) (the init data)
    { 0x8169494C, 0x915F000C },             //   stw r10, 12(r31): the frame object at +12
    { 0x81694950, 0x93EBDE68 },             //   stw r31, 81A9DE68: the instance
    { kXuiDestroy, 0x7D8802A6 },            // mflr r12
    { kXuiChildById, 0x7D8802A6 },          // mflr r12
};

typedef HRESULT (*BuilderFn)(HANDLE, LPCWSTR, LPCWSTR, LPWSTR, DWORD);
typedef HRESULT (*TaskCreateFn)(DWORD*);
typedef void (*TaskSetFn)(DWORD, DWORD, PVOID, PVOID, const DWORD*);
typedef void (*TaskStartFn)(DWORD);
typedef HRESULT (*SkinLoadFn)(LPCWSTR, LPCWSTR);
typedef void (*FreeVisualsFn)(LPCWSTR);
typedef HRESULT (*SceneCreateExFn)(LPCWSTR, LPCWSTR, PVOID, DWORD, DWORD, PVOID*);
typedef HRESULT (*FrameCreateFn)(DWORD, DWORD);
typedef HRESULT (*XuiDestroyFn)(PVOID);
typedef HRESULT (*XuiChildByIdFn)(PVOID, LPCWSTR, PVOID*);
const WCHAR kSkinUrl[] = L"file://SlopGuide:/skin.xzp#skin.xur";
// The system's own skin (17559's), in the same package: loaded after ours, it
// brings back the visuals only it has, while the names both have stay ours.

// The skin SlopDash chose: its folder becomes the SlopGuide: drive. On the
// hard drive with SlopDash (Hdd:\SlopDash\Guide), as SlopDash moved there on
// 2026-11-03: left on the stick, the notification thread's log writes hung it
// (the stick hangs SlopGuide's threads) and the Guide read SlopDash's old
// cache (2026-11-04).
const char kGuideDevice[] = "\\Device\\Harddisk0\\Partition1\\SlopDash\\Guide";
const char kGuideDrive[] = "\\System??\\SlopGuide:";
const char kSkinFile[] = "\\Device\\Harddisk0\\Partition1\\SlopDash\\Guide\\skin.xzp";
// Made by SlopDash: the skin is loaded again.
const char kTrigger[] = "\\Device\\Harddisk0\\Partition1\\SlopDash\\Guide\\reload";
// Blades (launch): one package, the frame, the screens, the skin and the art.
const char k1888File[] = "\\Device\\Harddisk0\\Partition1\\SlopDash\\Guide\\blades1888.xzp";
// A new package, put in place at start-up (TakeNextPackage).
const char k1888Next[] = "\\Device\\Harddisk0\\Partition1\\SlopDash\\Guide\\blades1888.next.xzp";
const WCHAR k1888Base[] = L"file://SlopGuide:/blades1888.xzp#";
const WCHAR k1888Skin[] = L"file://SlopGuide:/blades1888.xzp#skin.xur";
volatile LONG g_1888 = 0;           // the package is there: the 1888 Guide is on

// --- the log: kept in memory, written out by a thread once the stick is there
char g_log[32768];
volatile LONG g_logLen = 0;

void Log(const char* fmt, ...)
{
    char line[256];
    va_list a;
    va_start(a, fmt);
    int n = _vsnprintf_s(line, sizeof(line) - 2, _TRUNCATE, fmt, a);
    va_end(a);
    if (n < 0)
        n = (int)strlen(line);
    line[n++] = '\r';
    line[n++] = '\n';
    LONG at = InterlockedExchangeAdd(&g_logLen, n);
    if (at + n <= (LONG)sizeof(g_log))
        memcpy(g_log + at, line, n);
}

// The log so far, written to SlopGuide:\SlopGuide.log (whole, each time): at
// the steps a crash could follow (sign-in), so what led up to it survives.
// Asked for (FlushLog) on the Guide's own thread, written by the notification
// thread (Guide1888.inc NotifyThread, every 30 ms): written there, each one held
// the Guide up for tens of milliseconds - a USB write of the whole log, on
// frame notifications too (the Guide's timing, 2026-10-17: up to 99 ms).
// Now not written at all: from the notification thread (0.86-0.96) the write got
// stuck in the stick's file system, as 0.4-0.8's log thread did, and the
// notification thread with it - the Guide's open and closed, Done's close
// (2026-10-18). The log stays in memory (g_log); WriteLogNow stays for when
// the file is wanted.
volatile LONG g_flushWanted = 0;

void FlushLog()
{
}

void WriteLogNow()
{
    HANDLE h = CreateFileA("SlopGuide:\\SlopGuide.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    LONG n = g_logLen;
    DWORD put = 0;
    WriteFile(h, g_log, (DWORD)(n < (LONG)sizeof(g_log) ? n : (LONG)sizeof(g_log)), &put, NULL);
    CloseHandle(h);
}

// The file's size and last write time (0 if it isn't there).
ULONGLONG FileStamp(const char* path)
{
    KSTRING name;
    RtlInitAnsiString(&name, path);
    KOBJECT_ATTRIBUTES attr = { NULL, &name, 0x40 };
    KFILE_ATTRS info;
    if (!NT_OK(NtQueryFullAttributesFile(&attr, &info)))
        return 0;
    return (ULONGLONG)info.LastWriteTime.QuadPart ^ ((ULONGLONG)info.EndOfFile.QuadPart << 40);
}

bool FileExists(const char* path, bool emptyToo = false)
{
    KSTRING name;
    RtlInitAnsiString(&name, path);
    KOBJECT_ATTRIBUTES attr = { NULL, &name, 0x40 /* case-insensitive */ };
    KFILE_ATTRS info;
    return NT_OK(NtQueryFullAttributesFile(&attr, &info)) && (emptyToo || info.EndOfFile.QuadPart > 0);
}


// The theme SlopDash starts in (written by it: game:\Guide\theme.txt, see
// src/platform/themes.cpp): the Guide follows it. Blades (launch) has its own
// package; with no theme.txt yet (an older SlopDash), the package decides.
bool Want1888()
{
    HANDLE h = CreateFileA("SlopGuide:\\theme.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FileExists(k1888File) || FileExists(k1888Next);
    char id[32] = { 0 };
    DWORD got = 0;
    ReadFile(h, id, sizeof(id) - 1, &got, NULL);
    CloseHandle(h);
    return !strcmp(id, "blades1888");
}


// --- the builder xam now calls ------------------------------------------------
volatile LONG g_calls = 0;

HRESULT SlopBuilder(HANDLE module, LPCWSTR name, LPCWSTR file, LPWSTR out, DWORD len)
{
    InterlockedIncrement(&g_calls);
    if (out && len && !g_1888 && !Want1888() && FileExists(kSkinFile))
    {
        _snwprintf_s(out, len, _TRUNCATE, L"file://SlopGuide:/skin.xzp#%s", file ? file : L"skin.xur");
        Log("Guide skin: %S", out);
        return S_OK;
    }
    Log("Guide skin: none chosen (or the stick isn't ready), the system's");
    return ((BuilderFn)kBuilder)(module, name, file, out, len);
}

// --- install ------------------------------------------------------------------
// --- loading the skin after start-up -----------------------------------------
// When DashLaunch starts SlopGuide after xam has loaded the Guide's skin (it does
// that once, at start-up), the skin is loaded again here, on the Guide's side: a
// xam task set up as the start-up's own (what 5 = function + parameters, params
// {10h, 0}), which loads ours over the system's.
#ifdef SLOPDASH_LOCAL
#include "../local/slopguide_switches.inc"
#endif

DWORD ReloadTask(PVOID)
{
    Log("task running (%u ms after start-up, thread %08X)", (unsigned)GetTickCount(), (unsigned)GetCurrentThreadId());
    // The Blades skin's visuals have names of their own (RD_..., tools/guidens.py):
    // they're added next to the system's, which stay as they are for every other
    // screen. (0.10/0.11 loaded same-named visuals over the system's, which then
    // wouldn't load again: system screens came up in 6717's look.) Loaded once per
    // start-up: a second load finds the names taken.
    HRESULT hr = ((SkinLoadFn)kSkinLoad)(kSkinUrl, NULL);
    Log("Blades skin added: %08X (%u ms)", (unsigned)hr, (unsigned)GetTickCount());
    return 0;
}

// --- Blades (launch): the frame made again -------------------------------------
// On the Guide's side (a xam task, as the start-up's own work items run):
// 1888's skin is added (its visuals named B8_..., beside 17559's), the frame
// xam made at start-up is destroyed, and xam's own frame function makes it
// again - through SlopSceneCreate, so from blades1888.xzp. xam then finds
// AppHostElementId and the rest in it itself, and the class's init records the
// new instance. Once per start-up, with the Guide closed (it is, this early).
volatile LONG g_rebuilt = 0;

// A plausible pointer: word-aligned, in xam's heap or images.
bool Pointer(DWORD p)
{
    return !(p & 3) && p >= 0x40000000 && p < 0xC0000000;
}

volatile LONG g_vkFull = 0;         // the blade is Full for the keyboard (BladeFull)
#include "Plugins.inc"
#include "Recorder.inc"
#include "Trainers.inc"
// SlopDash's reader for a profile's settings files (the achievements' unlock
// dates: Guide1888.inc AchDateFill).
#include "core/xdbf.cpp"
#include "Guide1888.inc"

DWORD Rebuild1888(PVOID)
{
    // Both live on xam's heap (0.16's first test: the instance at 781DE500).
    DWORD inst = *(volatile DWORD*)kFrameInstance;
    DWORD frame = Pointer(inst) ? *(volatile DWORD*)(inst + 12) : 0;
    Log("1888 rebuild: instance %08X, frame object %08X", (unsigned)inst, (unsigned)frame);
    // +8 is the frame's parent element: an XUI handle (small, opaque), not a pointer.
    if (!Pointer(frame) || !*(volatile DWORD*)(frame + 8))
        return 0;
    HRESULT hr = ((SkinLoadFn)kSkinLoad)(k1888Skin, NULL);
    Log("1888 skin added: %08X", (unsigned)hr);
    if (FAILED(hr))
        return 0;
    InterlockedExchange(&g_1888, 1);        // from now on xam gets 1888's scenes
    // The old frame: under the frame object's parent element (+8).
    PVOID parent = *(PVOID volatile*)(frame + 8), old = NULL;
    hr = ((XuiChildByIdFn)kXuiChildById)(parent, L"HUDRootScene", &old);
    Log("1888 old frame %p under %p: %08X", old, parent, (unsigned)hr);
    if (FAILED(hr) || !old)
        return 0;
    hr = ((XuiDestroyFn)kXuiDestroy)(old);
    Log("1888 old frame destroyed: %08X", (unsigned)hr);
    hr = ((FrameCreateFn)kFrameCreate)(frame, 0);
    Log("1888 frame made again: %08X (instance now %08X)", (unsigned)hr,
        (unsigned)*(volatile DWORD*)kFrameInstance);
    InterlockedExchange(&g_rebuilt, SUCCEEDED(hr) ? 1 : -1);
    if (SUCCEEDED(hr))
        g1888::Install();               // 1888's Guide behaviour (Guide1888.inc)
    return 0;
}

void RunTask(PVOID fn)
{
    DWORD task = 0;
    HRESULT hr = ((TaskCreateFn)kTaskCreate)(&task);
    Log("task: %08X (%08X) at %u ms", (unsigned)hr, (unsigned)task, (unsigned)GetTickCount());
    if (FAILED(hr) || !task)
        return;
    static const DWORD params[4] = { 0x10, 0, 0, 0 };
    ((TaskSetFn)kTaskSet)(task, 5, fn, NULL, params);
    ((TaskStartFn)kTaskStart)(task);
}

void RunReloadTask()
{
    DWORD task = 0;
    HRESULT hr = ((TaskCreateFn)kTaskCreate)(&task);
    Log("task: %08X (%08X) at %u ms", (unsigned)hr, (unsigned)task, (unsigned)GetTickCount());
    if (FAILED(hr) || !task)
        return;
    static const DWORD params[4] = { 0x10, 0, 0, 0 };
    ((TaskSetFn)kTaskSet)(task, 5, (PVOID)ReloadTask, NULL, params);
    ((TaskStartFn)kTaskStart)(task);
}

// A new package can't replace the one in use (xam keeps it open): it's sent as
// blades1888.next.xzp and put in place here, at start-up, before it's opened.

void TakeNextPackage()
{
    if (!FileExists(k1888Next))
        return;
    BOOL gone = DeleteFileA("SlopGuide:\\blades1888.xzp");
    BOOL moved = MoveFileA("SlopGuide:\\blades1888.next.xzp", "SlopGuide:\\blades1888.xzp");
    Log("new package: old %s, new %s (%u)", gone ? "removed" : "not removed", moved ? "in place" : "NOT MOVED",
        (unsigned)GetLastError());
}

DWORD WINAPI Starter1888(LPVOID)
{
    // The stick can take a moment to appear after start-up.
    for (int i = 0; i < 30 && !FileExists(k1888File) && !FileExists(k1888Next); i++)
        Sleep(1000);
    TakeNextPackage();
    if (!FileExists(k1888File) || !Want1888())
    {
        Log("the 1888 Guide isn't the chosen theme's (or no blades1888.xzp): the Guide stays as it is");
        return 0;
    }
    Log("Blades (launch) Guide: starting (log at %08X)", (unsigned)(DWORD)g_log);
    RunTask((PVOID)Rebuild1888);
    return 0;
}

#ifdef SLOPDASH_LOCAL
#include "../local/slopguide_reloader.inc"
#endif

// --- the Guide's screens -------------------------------------------------------
// hud.xex opens the Guide by creating "GuideMain.xur" from "section://@0,hud#"
// (its tabs follow, created by that scene's code).
volatile LONG g_scenes = 0;
const WCHAR kGuideBase[] = L"file://SlopGuide:/guide.xzp#";

// Whether this base | path pair hasn't been logged yet (a small table of
// hashes: the plugin keeps no heap of its own).
bool FirstTimeSeen(LPCWSTR base, LPCWSTR path)
{
    static DWORD seen[512];
    static LONG count = 0;
    DWORD h = 2166136261u;
    for (LPCWSTR s = base ? base : L""; *s; s++)
        h = (h ^ (DWORD)*s) * 16777619u;
    h = (h ^ '|') * 16777619u;
    for (LPCWSTR s = path ? path : L""; *s; s++)
        h = (h ^ (DWORD)*s) * 16777619u;
    LONG n = count < 512 ? count : 512;
    for (LONG i = 0; i < n; i++)
        if (seen[i] == h)
            return false;
    if (count < 512)
        seen[count++] = h;
    return true;
}

// --- The keyboard's pages (17559 vk.xex) -------------------------------------
// The console's keyboard (Create Gamer Profile's gamertag...) draws 1888's
// (VK_Keyboard*.xur, tools/guide1888.py), its keys Key.<row>_<column>. Their
// characters come from vk.xex's own pages, 50 to a page (5 rows of 10): for
// English (vk.xex 917D9F24) 917D1A88 its QWERTY letters, 917D1C20 its accents,
// 917D1B10 its symbols. They get 1888's (1888 vk.xex's key table at 917D1A38,
// as SlopDash's kVkKeys): its letters 7 wide then the digits, 4 rows, the fifth
// empty (as 17559's own QWERTY page leaves it). Changed only when every page
// is 17559's (or already 1888's), in place, each time a keyboard opens (the
// module may have been loaded again).
const DWORD kVkBase = 0x917D0000;
const DWORD kVkPages[3] = { 0x917D1A88, 0x917D1C20, 0x917D1B10 };
const WCHAR kVk17559[3][21] = {     // each page's first two rows, as shipped
    L"1234567890qwertyuiop",
    L"1234567890\x00e0\x00e1\x00e2\x00e3\x00e4\x00e5\x00f1\x00e8\x00e9\x00ea",
    L"1234567890,;:'\"!?\x00a1\x00bf%",
};
const WCHAR kVk1888[3][41] = {
    L"\x0061\x0062\x0063\x0064\x0065\x0066\x0067\x0031\x0032\x0033"
    L"\x0068\x0069\x006a\x006b\x006c\x006d\x006e\x0034\x0035\x0036"
    L"\x006f\x0070\x0071\x0072\x0073\x0074\x0075\x0037\x0038\x0039"
    L"\x0076\x0077\x0078\x0079\x007a\x002d\x0040\x005f\x0030\x002e",
    L"\x00e0\x00e1\x00e2\x00e3\x00e4\x00e5\x00f1\x0153\x00e6\x00df"
    L"\x00e8\x00e9\x00ea\x00eb\x00fe\x00e7\x00fd\x00ff\x00ba\x00aa"
    L"\x00ec\x00ed\x00ee\x00ef\x00f9\x00fa\x00fb\x00fc\x00b5\x0161"
    L"\x00f2\x00f3\x00f4\x00f5\x00f0\x00f6\x00f8\x005f\x00d7\x002e",
    L"\x002c\x003b\x003a\x0027\x0022\x0021\x003f\x00a1\x00bf\x0025"
    L"\x005b\x005d\x007b\x007d\x0060\x0024\x00a3\x00ab\x00bb\x0023"
    L"\x003c\x003e\x0028\x0029\x20ac\x00a5\x00a4\x007e\x005e\x005c"
    L"\x007c\x003d\x002a\x002f\x002b\x002d\x0040\x005f\x0026\x002e",
};

extern "C" DWORD MmQueryAddressProtect(PVOID address);

// 17559 sizes Space and Backspace to its own key grid (vk.xex 917D71E0, from
// the page refresh at 917D87A8 on every page change): Space moved to a key
// column's x, both made as wide as its first columns (measured from its grid
// row, which 1888's layout starts further right), so Space landed on
// Backspace. It keeps its one other job (the mode flag at +244h) and returns:
// the two keys stay where 1888's keyboard has them.
const DWORD kVkSizeKeys = 0x917D71E0;
const DWORD kVkSizeKeysWas[2] = { 0x7D8802A6, 0x9181FFF8 };     // mflr r12; stw r12, -8(r1)
const DWORD kVkSizeKeysNow[2] = { 0x90830244, 0x4E800020 };     // stw r4, 244h(r3); blr

void VkKeepKeys()
{
    volatile DWORD* at = (volatile DWORD*)kVkSizeKeys;
    if (at[0] == kVkSizeKeysNow[0] && at[1] == kVkSizeKeysNow[1])
        return;
    if (at[0] != kVkSizeKeysWas[0] || at[1] != kVkSizeKeysWas[1])
    {
        Log("keyboard keys: %08X isn't 17559's: left as it is", (unsigned)kVkSizeKeys);
        return;
    }
    at[1] = kVkSizeKeysNow[1];          // (the return first: never half of it)
    __dcbst(0, (void*)&at[1]);
    __sync();
    at[0] = kVkSizeKeysNow[0];
    __dcbst(0, (void*)at);
    __sync();
    __emit(0x4C00012C);                 // isync
    Log("keyboard keys: Space and Backspace stay 1888's");
}

// --- No avatar picker at sign-in (17559 xam) ------------------------------------
// Signing in a profile with no avatar opened 17559's "Express yourself with an
// avatar" (Guide.AvatarMiniCreator.xex): 1888 had no
// avatars. xam 816C45F8 asks the Guide for it (request 21018h, the case at
// 816C7A58 that loads the module) and returns 5 when the Guide can't take UI
// (816C4628): it now always does that, before asking.
const DWORD kAvatarPicker = 0x816C45F8;
const DWORD kAvatarPickerWas[2] = { 0x7D8802A6, 0x9181FFF8 };   // mflr r12; stw r12, -8(r1)
const DWORD kAvatarPickerNow[2] = { 0x38600005, 0x4E800020 };   // li r3, 5; blr

void NoAvatarPicker()
{
    volatile DWORD* at = (volatile DWORD*)kAvatarPicker;
    if (at[0] == kAvatarPickerNow[0] && at[1] == kAvatarPickerNow[1])
        return;
    if (at[0] != kAvatarPickerWas[0] || at[1] != kAvatarPickerWas[1])
    {
        Log("avatar picker: %08X isn't 17559's: left as it is", (unsigned)kAvatarPicker);
        return;
    }
    at[1] = kAvatarPickerNow[1];        // (the return first: never half of it)
    __dcbst(0, (void*)&at[1]);
    __sync();
    at[0] = kAvatarPickerNow[0];
    __dcbst(0, (void*)at);
    __sync();
    __emit(0x4C00012C);                 // isync
    Log("avatar picker: never shown (%08X)", (unsigned)kAvatarPicker);
}

// --- Create Profile without an avatar (17559 createprofile.xex) ---------------
// 1888 had no avatars: name, "Creating profile", then Save Gamer Profile
// (Congratulations.xur: Done / Customize Profile / Join Xbox Live). 17559's
// Create Profile, after the gamertag, loads the avatar creator
// (Guide.AvatarMiniCreator.xex, step 5 at 90106688: its load, 901066B0); when
// that isn't there, 17559 itself calls XamShowCreateProfileUI(user, 6)
// (901066EC), which opens Create Profile again at page 6, Save Gamer Profile.
// The load reads as "not there" (li r3, 0): the console's own way to the
// screen 1888 had. (0.57-0.60 changed the step and the ending instead: no
// avatar, but no Save Gamer Profile either.)
// (The module's own address: gamerprofile.xex has the same preferred base.)
const DWORD kCpLoadAt = 0x66B0;                                  // 901066B0 - 90100000
const DWORD kCpLoadWas[3] = { 0x386B0A58, 0x480061B9, 0x28030000 };   // addi r3, "Guide.AvatarMiniCreator.xex"; bl load; cmplwi r3, 0
const DWORD kCpLoadNow = 0x38600000;                             // li r3, 0

void CpNoAvatar()
{
    HANDLE h = NULL;
    if (!NT_OK(XexGetModuleHandle("createprofile.xex", &h)) || !h)
    {
        Log("create profile: module not found");
        return;
    }
    volatile DWORD* at = (volatile DWORD*)((DWORD)((const KLDR_ENTRY*)h)->ImageBase + kCpLoadAt);
    if (at[-1] != kCpLoadWas[0] || at[1] != kCpLoadWas[2] || (at[0] != kCpLoadWas[1] && at[0] != kCpLoadNow))
    {
        Log("create profile: %08X isn't 17559's: the avatar step stays", (unsigned)(DWORD)at);
        return;
    }
    if (at[0] == kCpLoadNow)
        return;
    at[0] = kCpLoadNow;
    __dcbst(0, (void*)at);
    __sync();
    __emit(0x4C00012C);                 // isync
    Log("create profile: no avatar creator: Save Gamer Profile next (%08X)", (unsigned)(DWORD)at);
}

// --- Motto and Gamer Zone for every profile (17559 gamerprofile.xex) ----------
// 1888's Edit Gamer Profile had Motto and Gamer Zone; 17559 shows them only to
// Live profiles, an offline one getting Gamer Picture and Profile Name (the
// user wants them anyway, for nostalgia: 2026-10-17). In its scene set-up
// (EditProfileScene, 9010CB80 on):
//   9010CBE4 bt cr0.eq (the profile's flag 4 clear: offline rows) - nop: every
//            profile gets the online rows;
//   9010CBE8, 9010CC18 bl xam 2886 (nonzero: the datacenter rows, a stand-in in
//            1888's screen) - li r3, 0: 1888's online rows;
//   9010CC54 addi r5, r31, 49Ch (the Gamertag row's slot, for Live's gamertag
//            change, 9010D030) - 4A0h, Profile Name's slot (9010B308, the
//            offline path's 9010CE5C): that row, relabelled Profile Name
//            (tools/guide1888.py), renames the profile as before.
struct GpWord { DWORD at, was, now; };
const GpWord kGpOnline[] = {
    { 0xCBE4, 0x418201F0, 0x60000000 },
    { 0xCBE8, 0x48019449, 0x38600000 },
    { 0xCC18, 0x48019419, 0x38600000 },
    { 0xCC54, 0x38BF049C, 0x38BF04A0 },
    // Motto (9010C268) and Gamer Zone (9010C540) went on only with the
    // user's Live bits (90121EC8 & Ch == Ch) and otherwise showed an error
    // box: their edits, for every profile.
    { 0xC2A0, 0x409A0240, 0x60000000 },
    { 0xC568, 0x409A0070, 0x60000000 },
    // A typed motto (9010D518, the keyboard's answer) went to Xbox Live's
    // string check first when its setting's entry asks for one (90127378 +
    // 44 x setting + 8): with no Live its start failed (9010C758) and the
    // edit ended there, unsaved. It's written as a
    // setting without a check is (9010D5C8: XamUserWriteProfileSettings,
    // 9010D634).
    { 0xD548, 0x419A0080, 0x48000080 },
    // Motto with no motto yet (a new profile's; none at all at 9010C424, or
    // "" at 9010C448: compared with the empty string at 90100CA4) went to
    // Xbox Live for the profile's details first (9010C464: 9011DD30, an
    // answer it waits for) and, with no Live, nothing happened - no keyboard
    //. Both go to the keyboard (9010C44C), as a motto
    // already set does.
    { 0xC424, 0x419A0044, 0x48000028 },
    { 0xC448, 0x4182001C, 0x60000000 },
    // A profile with no motto showed 17559's "(No motto)" (gamerprofile's
    // string at 9012D864; 1888 left it blank): its
    // first character ended, an empty string.
    { 0x2D864, 0x284E6F20, 0x004E6F20 },
};

void GpOnlineRows()
{
    HANDLE h = NULL;
    if (!NT_OK(XexGetModuleHandle("gamerprofile.xex", &h)) || !h)
    {
        Log("gamer profile: module not found");
        return;
    }
    DWORD base = (DWORD)((const KLDR_ENTRY*)h)->ImageBase;
    // All four or none: each word as 17559 has it, or as patched.
    for (int i = 0; i < ARRAYSIZE(kGpOnline); i++)
    {
        DWORD w = *(volatile DWORD*)(base + kGpOnline[i].at);
        if (w != kGpOnline[i].was && w != kGpOnline[i].now)
        {
            Log("gamer profile: %08X isn't 17559's: offline rows stay", (unsigned)(base + kGpOnline[i].at));
            return;
        }
    }
    bool changed = false;
    for (int i = 0; i < ARRAYSIZE(kGpOnline); i++)
    {
        volatile DWORD* at = (volatile DWORD*)(base + kGpOnline[i].at);
        if (*at == kGpOnline[i].now)
            continue;
        *at = kGpOnline[i].now;
        __dcbst(0, (void*)at);
        changed = true;
    }
    __sync();
    __emit(0x4C00012C);                 // isync
    if (changed)
        Log("gamer profile: Motto and Gamer Zone for every profile (%08X)", (unsigned)base);
    // The zone list's pictures: its table (90103240: per zone the name's and
    // the description's string, then the picture's path; Recreation, Pro,
    // Family, Underground) pointed at 17559's redrawn ones
    // (sharedres://ico_32x_Zone_*.png); now at 1888's, in our package
    // (g1888::kZones, same order). The list puts them in itself when its
    // screen starts (9011C008), asking no one.
    const DWORD kIconAt[] = { 0x3254, 0x3260, 0x326C, 0x3278 };
    const DWORD kIconWas[] = { 0x3200, 0x31B8, 0x3170, 0x3118 };
    int icons = 0;
    for (int i = 0; i < ARRAYSIZE(kIconAt); i++)
    {
        volatile DWORD* at = (volatile DWORD*)(base + kIconAt[i]);
        // No picture at all: the 2005 Gamer Zones list was text only (the
        // user's reference, IGN's screenshot of the launch dashboard,
        // 2026-10-17); an empty path, so the rows show none.
        static const WCHAR kNoPicture[] = L"";
        DWORD now = (DWORD)kNoPicture;
        if (*at == now)
            continue;
        if (*at != base + kIconWas[i])
        {
            Log("gamer profile: zone picture %d at %08X isn't 17559's (%08X)", i, (unsigned)(DWORD)at, (unsigned)*at);
            continue;
        }
        *at = now;
        __dcbst(0, (void*)at);
        icons++;
    }
    __sync();
    if (icons)
        Log("gamer profile: %d zone pictures taken out (text only, as 2005's)", icons);
}

void Vk1888Pages()
{
    HANDLE h = NULL;
    if (!NT_OK(XexGetModuleHandle("vk.xex", &h)) || !h || (DWORD)((const KLDR_ENTRY*)h)->ImageBase != kVkBase)
    {
        Log("keyboard pages: vk.xex isn't 17559's at %08X", (unsigned)kVkBase);
        return;
    }
    VkKeepKeys();
    bool change = false;
    for (int p = 0; p < 3; p++)
    {
        const WCHAR* page = (const WCHAR*)kVkPages[p];
        if (!memcmp(page, kVk17559[p], 20 * sizeof(WCHAR)))
            change = true;
        else if (memcmp(page, kVk1888[p], 40 * sizeof(WCHAR)))
        {
            Log("keyboard pages: page %d isn't 17559's: left as it is", p);
            return;
        }
    }
    if (!change)
        return;
    // Read-only pages take the change as xam's code does (its pages read-only
    // too, protection 20h: SlopGuide's own change at kCallSite): 0.54 saw 2
    // (PAGE_READONLY) here. Anything else is left alone.
    DWORD prot = MmQueryAddressProtect((PVOID)kVkPages[0]), code = MmQueryAddressProtect((PVOID)kCallSite);
    if (!(prot & 0xCC) && prot != code && prot != PAGE_READONLY)
    {
        Log("keyboard pages: protection %X (xam's code %X): left as they are", (unsigned)prot, (unsigned)code);
        return;
    }
    for (int p = 0; p < 3; p++)
    {
        WCHAR* page = (WCHAR*)kVkPages[p];
        memcpy(page, kVk1888[p], 40 * sizeof(WCHAR));
        memset(page + 40, 0, 10 * sizeof(WCHAR));
    }
    Log("keyboard pages: 1888's (protection %X)", (unsigned)prot);
}

bool AnyoneSignedIn()
{
    for (DWORD i = 0; i < 4; i++)
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn)
            return true;
    return false;
}

// A scene of one of the console's system apps: its locator names the module
// ("...<module>#<file>"), in the base or the path.
bool FromModule(LPCWSTR base, LPCWSTR path, LPCWSTR module)
{
    size_t n = wcslen(module);
    LPCWSTR in[2] = { base, path };
    for (int i = 0; i < 2; i++)
        if (in[i] && !wcsstr(in[i], L"SlopGuide:"))
            for (LPCWSTR c = in[i]; *c; c++)
                if (!_wcsnicmp(c, module, n) && c[n] == L'#')
                    return true;
    return false;
}

bool SigninModule(LPCWSTR base, LPCWSTR path)
{
    // (Create Profile's module is "cp": section://@E,cp#. 0.56 and before
    // looked for "createprofile" and its 1888 screens never showed.)
    return FromModule(base, path, L"signin") || FromModule(base, path, L"cp");
}

// The keyboard is 1888's one Full screen (855 wide): the blade opens to Full
// for it (the frame's RG_HalfToFull: 1888's own, tools/guide1888.py) and back
// to Half for the next screen (or when the Guide closes).

void BladeFull(bool full)
{
    if ((g_vkFull != 0) == full)
        return;
    g_vkFull = full ? 1 : 0;
    if (full)
        g1888::PlayNamed(g1888::FrameRoot(), L"RG_HalfToFull", L"RG_EndHalfToFull");
    else
        g1888::PlayNamed(g1888::FrameRoot(), L"RG_FullToHalf", L"RG_EndFullToHalf");
    Log("1888 Guide: blade %s", full ? "Full" : "Half");
}

// A screen the console's code opens by name next to one of ours (its base our
// package: "file://SlopGuide:/blades1888.xzp#CP_CreateGamerProfile.xur",
// "AddXboxLive.xur") is ours by our name too: 0.57 asked the package for
// AddXboxLive.xur, 80300013, and the Guide stayed blank.
struct OurName { LPCWSTR theirs, ours; bool screen; };
const OurName kOurNames[] = {
    { L"CreateGamerProfile.xur", L"CP_CreateGamerProfile.xur", true },
    { L"AddXboxLive.xur", L"CP_AddXboxLive.xur", true },
    { L"Congratulations.xur", L"CP_Congratulations.xur", true },
    { L"signin1.xur", L"SI_signin1.xur", true },
    { L"UserList.xur", L"SI_UserList.xur", true },
    { L"UserListSinglePane.xur", L"SI_UserList.xur", true },
    { L"status.xur", L"SI_status.xur", true },
    { L"DeviceSelector.xur", L"DS_DeviceSelector.xur", true },
    { L"DeviceFull.xur", L"DS_DeviceFull.xur", true },
    { L"808_editProfile.xur", L"GP_808_editProfile.xur", true },
    { L"807_EditGamerTile.xur", L"GP_807_EditGamerTile.xur", true },
    { L"809_ChangeGamerTile.xur", L"GP_809_ChangeGamerTile.xur", true },
    { L"SelectGamerzone.xur", L"GP_SelectGamerzone.xur", true },
    { L"GamerZoneInfo.xur", L"GP_GamerZoneInfo.xur", true },
    // (the achievements' scenes open each other by these names, next to ours:
    // 804 asked for 802_Achievements.xur and got 80300013, 2026-11-05)
    { L"GamerCardScene.xur", L"GP_GamerCardScene.xur", true },
    { L"820_GameShowcaseMe.xur", L"GP_820_GameShowcaseMe.xur", true },
    { L"802_Achievements.xur", L"GP_802_Achievements.xur", true },
    { L"804_LoadAchievements.xur", L"GP_804_LoadAchievements.xur", true },
    { L"828_AchievDetails.xur", L"GP_828_AchievDetails.xur", true },
    { L"KeyboardBase.xur", L"VK_KeyboardBase.xur", false },
};

const OurName* FromOurs(LPCWSTR base, LPCWSTR path)
{
    if (!base || !path || !wcsstr(base, L"SlopGuide:") || wcschr(path, L'#'))
        return NULL;
    for (size_t i = 0; i < ARRAYSIZE(kOurNames); i++)
        if (!_wcsicmp(path, kOurNames[i].theirs))
            return &kOurNames[i];
    return NULL;
}

HRESULT SlopSceneCreate(LPCWSTR base, LPCWSTR path, PVOID init, PVOID* scene)
{
    SceneCreateExFn create = (SceneCreateExFn)kSceneCreateEx;
    if (g_1888 && path)
    {
        // xam's Guide frame: 1888's blade. Its main screen: 1888's.
        LPCWSTR mine = NULL;
        bool keyboard = false, screen = false;     // the keyboard; a screen the blade hosts
        LPCWSTR file = wcsrchr(path, L'#');
        bool own = !base && file && !wcsstr(path, L"SlopGuide:");     // xam's own section://
        const OurName* ours = FromOurs(base, path);
        if (ours)
        {
            mine = ours->ours;
            screen = ours->screen;
        }
        else if (own && !_wcsicmp(file, L"#hudbkgnd.xur"))
            mine = L"hudbkgnd.xur";
        else if (own && !_wcsicmp(file, L"#notify.xur"))
            mine = L"notify.xur";           // every notification, games included
        else if (own && !_wcsicmp(file, L"#GamerCard.xur"))
            mine = L"GamerCard.xur";        // 17559's code fills 1888's card
        else if (base && !wcscmp(base, L"section://@0,hud#") && !wcscmp(path, L"GuideMain.xur"))
        {
            mine = AnyoneSignedIn() ? L"MainMenuSignedIn.xur" : L"MainMenuSignedOut.xur";
            screen = true;
        }
        else if (SigninModule(base, path))
        {
            screen = true;
            // The console's sign-in (signin.xex, same scene names as 1888's).
            LPCWSTR name = file ? file + 1 : path;
            if (!_wcsicmp(name, L"signin1.xur"))
                mine = L"SI_signin1.xur";
            else if (!_wcsicmp(name, L"UserList.xur") || !_wcsicmp(name, L"UserListSinglePane.xur"))
                mine = L"SI_UserList.xur";
            else if (!_wcsicmp(name, L"status.xur"))
                mine = L"SI_status.xur";
            // The console's Create Profile (createprofile.xex, same scenes as 1888's).
            else if (!_wcsicmp(name, L"CreateGamerProfile.xur"))
            {
                CpNoAvatar();
                mine = L"CP_CreateGamerProfile.xur";
            }
            else if (!_wcsicmp(name, L"AddXboxLive.xur"))
                mine = L"CP_AddXboxLive.xur";
            else if (!_wcsicmp(name, L"Congratulations.xur"))
                mine = L"CP_Congratulations.xur";
        }
        // The console's device selector (deviceselector.xex: section://@2,media#),
        // 1888's (its own media#: the same classes) - 17559's drew small in the
        // frame's corner, its title over the clock.
        // (Its module is in the base, the file name alone in the path: 0.42
        // looked for "#" in the path and never matched.)
        else if (FromModule(base, path, L"media"))
        {
            screen = true;
            LPCWSTR name = file ? file + 1 : path;
            if (!_wcsicmp(name, L"DeviceSelector.xur"))
                mine = L"DS_DeviceSelector.xur";
            else if (!_wcsicmp(name, L"DeviceFull.xur"))
                mine = L"DS_DeviceFull.xur";
        }
        // The console's keyboard (vk.xex: section://@B,vk#), 1888's (above).
        // The console's gamer profile screens (gamerprofile.xex: section://@D,gp#),
        // 1888's with 17559's extra parts hidden (tools/guide1888.py GAMERPROFILE).
        else if (FromModule(base, path, L"gp"))
        {
            LPCWSTR name = file ? file + 1 : path;
            screen = true;
            if (!_wcsicmp(name, L"808_editProfile.xur"))
            {
                GpOnlineRows();
                mine = L"GP_808_editProfile.xur";
            }
            else if (!_wcsicmp(name, L"807_EditGamerTile.xur"))
                mine = L"GP_807_EditGamerTile.xur";
            else if (!_wcsicmp(name, L"809_ChangeGamerTile.xur"))
                mine = L"GP_809_ChangeGamerTile.xur";
            else if (!_wcsicmp(name, L"SelectGamerzone.xur"))
                mine = L"GP_SelectGamerzone.xur";
            else if (!_wcsicmp(name, L"GamerZoneInfo.xur"))
                mine = L"GP_GamerZoneInfo.xur";
            // Achievements: 1888's games list, a game's
            // achievements, one achievement, and a loading screen, for 17559's
            // (tools/guide1888.py ACHIEVEMENTS; their parts checked against
            // 17559's by tools/gp_check.py).
            else if (!_wcsicmp(name, L"GamerCardScene.xur"))
                mine = L"GP_GamerCardScene.xur";
            else if (!_wcsicmp(name, L"820_GameShowcaseMe.xur"))
                mine = L"GP_820_GameShowcaseMe.xur";
            else if (!_wcsicmp(name, L"802_Achievements.xur"))
                mine = L"GP_802_Achievements.xur";
            else if (!_wcsicmp(name, L"804_LoadAchievements.xur"))
                mine = L"GP_804_LoadAchievements.xur";
            else if (!_wcsicmp(name, L"828_AchievDetails.xur"))
                mine = L"GP_828_AchievDetails.xur";
        }
        else if (FromModule(base, path, L"vk"))
        {
            LPCWSTR name = file ? file + 1 : path;
            if (!_wcsicmp(name, L"KeyboardMain.xur"))
            {
                Vk1888Pages();
                mine = L"VK_KeyboardMain.xur";
                keyboard = true;
            }
            else if (!_wcsicmp(name, L"KeyboardBase.xur"))
                mine = L"VK_KeyboardBase.xur";
        }
        // 1888's Full screens (their HUDScene P0 2, 855 wide): the keyboard, Save
        // Gamer Profile, Edit Gamer Profile and its gamer picture screens. In
        // the Half blade their gamer card was cut off.
        bool full = keyboard || (mine && (!wcscmp(mine, L"CP_Congratulations.xur") ||
                                          !wcsncmp(mine, L"GP_", 3)));
        // Create Profile's AddXboxLive comes and goes between two Full screens
        // (the keyboard and Save Gamer Profile; Customize and Edit Gamer
        // Profile): the blade stays as it is, not Half for a moment.
        bool passing = mine && !wcscmp(mine, L"CP_AddXboxLive.xur");
        if (screen && !full && !passing && g_vkFull)
            BladeFull(false);
        if (mine)
        {
            if (!wcscmp(mine, L"CP_CreateGamerProfile.xur"))
                g1888::CreateProfileStarted();
            else if (!wcsncmp(mine, L"MainMenuSigned", 14))
                g1888::CreateProfileOver("the Guide's main menu");
            HRESULT hr = create(k1888Base, mine, init, 0, 0, scene);
            if (SUCCEEDED(hr) && scene && *scene && !wcscmp(mine, L"notify.xur"))
                g1888::HoldToast((HXUIOBJ)*scene);
            if (SUCCEEDED(hr) && scene && *scene &&
                (!wcscmp(mine, L"GamerCard.xur") || !wcscmp(mine, L"CP_Congratulations.xur")))
                g1888::CardZoneInto((HXUIOBJ)*scene);
            if (SUCCEEDED(hr) && screen)
                g1888::KeyTimerOn((HXUIOBJ)*scene);        // (keys on any of our screens)
            if (SUCCEEDED(hr) && !wcscmp(mine, L"GP_828_AchievDetails.xur"))
                g1888::AchDetailsMade((HXUIOBJ)*scene);     // (the game's title in its heading)
            if (full && SUCCEEDED(hr) && g_vkFull)
            {
                // Full already, as far as SlopGuide knows - but the console's
                // own frame code goes Half when its keyboard closes (Save
                // Gamer Profile came up in a Half blade: the user,
                // 2026-10-17). The Full pose again: nothing if it is Full.
                g1888::PlayNamed(g1888::FrameRoot(), L"RG_EndHalfToFull", L"RG_EndHalfToFull");
                Log("1888 Guide: blade Full (again)");
            }
            else if (full && SUCCEEDED(hr))
                BladeFull(true);
            if (InterlockedIncrement(&g_scenes) <= 400 || SigninModule(base, path))
                Log("1888 Guide: %S -> %08X", mine, (unsigned)hr);
            if (SigninModule(base, path))
                FlushLog();
            if (SUCCEEDED(hr))
                return hr;
        }
    }
#ifdef SLOPDASH_LOCAL
#include "../local/slopguide_scenes.inc"
#endif
    HRESULT hr = create(base, path, init, 0, 0, scene);
    // Every scene the console makes, each name once (a count cap filled up
    // over a long session and the exit message box went unseen, 0.43).
    if (FirstTimeSeen(base, path) || SigninModule(base, path))
        Log("scene %S | %S -> %08X", base ? base : L"-", path ? path : L"-", (unsigned)hr);
    if (SigninModule(base, path))
        FlushLog();
    return hr;
}

bool Branch(DWORD at, PVOID to, bool link)
{
    LONG offset = (LONG)((DWORD)to - at);
    if (offset < -0x2000000 || offset >= 0x2000000)
        return false;
    *(volatile DWORD*)at = (link ? 0x48000001 : 0x48000000) | ((DWORD)offset & 0x03FFFFFC);
    __dcbst(0, (void*)at);
    __sync();
    __emit(0x4C00012C);                     // isync
    return true;
}

bool Is17559Xam()
{
    HANDLE h = NULL;
    if (!NT_OK(XexGetModuleHandle("xam.xex", &h)) || !h)
        return false;
    const KLDR_ENTRY* e = (const KLDR_ENTRY*)h;
    if ((DWORD)e->ImageBase != kXamBase || e->SizeOfNtImage != kXamSize)
    {
        Log("xam at %08X size %08X: not 17559's", (unsigned)(DWORD)e->ImageBase, (unsigned)e->SizeOfNtImage);
        return false;
    }
    for (size_t i = 0; i < ARRAYSIZE(kExpect); i++)
        if (*(const DWORD*)kExpect[i].at != kExpect[i].value)
        {
            Log("xam %08X is %08X, not %08X: not 17559's", (unsigned)kExpect[i].at,
                (unsigned)*(const DWORD*)kExpect[i].at, (unsigned)kExpect[i].value);
            return false;
        }
    if (memcmp((const void*)kSkinPath, "\\SystemRoot\\huduiskin.xex", 26) != 0)
    {
        Log("xam's skin path isn't 17559's");
        return false;
    }
    return true;
}

void Install()
{
    if (!Is17559Xam())
    {
        Log("not system 17559: the Guide is left as it is");
        return;
    }
    KSTRING link, device;
    RtlInitAnsiString(&link, kGuideDrive);
    RtlInitAnsiString(&device, kGuideDevice);
    LONG s = ObCreateSymbolicLink(&link, &device);
    Log("SlopGuide: -> %s: %08X", kGuideDevice, (unsigned)s);
    if (!NT_OK(s))
        return;
    // SlopDash's cache (the profiles' gamerscore and pictures, for the Guide's gamer card).
    KSTRING cacheLink, cacheDevice;
    RtlInitAnsiString(&cacheLink, "\\System??\\SlopDashCache:");
    RtlInitAnsiString(&cacheDevice, "\\Device\\Harddisk0\\Partition1\\SlopDash\\cache");
    Log("SlopDashCache: %08X", (unsigned)ObCreateSymbolicLink(&cacheLink, &cacheDevice));
    // SlopDash's folder on the hard drive, for the user's own gamer pictures
    // (SlopDashPics:\GamerPictures: PNG or JPG files put there over FTP or
    // with SlopDash's File Manager, listed in Change Gamer Picture).
    KSTRING picsLink, picsDevice;
    RtlInitAnsiString(&picsLink, "\\System??\\SlopDashPics:");
    RtlInitAnsiString(&picsDevice, "\\Device\\Harddisk0\\Partition1\\SlopDash");
    Log("SlopDashPics: %08X", (unsigned)ObCreateSymbolicLink(&picsLink, &picsDevice));
    CreateDirectoryA("SlopDashPics:\\GamerPictures", NULL);
    // (0.88-0.98 linked the USB stick's root too, for its GamerPictures: listed
    // from the notification thread, the stick hung it. Guide1888.inc kPicDirs.)

    LONG offset = (LONG)((DWORD)SlopBuilder - kCallSite);
    if (offset < -0x2000000 || offset >= 0x2000000)
    {
        Log("SlopBuilder at %08X is out of branch range of %08X: not installed", (unsigned)(DWORD)SlopBuilder,
            (unsigned)kCallSite);
        return;
    }
    bool late = *(volatile DWORD*)kSkinStarted != 0;
    if (late)
        Log("the Guide's skin was already loaded before SlopGuide: it loads again on request");
    DWORD word = 0x48000001 | ((DWORD)offset & 0x03FFFFFC);     // bl SlopBuilder
    *(volatile DWORD*)kCallSite = word;
    __dcbst(0, (void*)kCallSite);
    __sync();
    __emit(0x4C00012C);                     // isync
    Log("installed: %08X = %08X (bl %08X)", (unsigned)kCallSite, (unsigned)word, (unsigned)(DWORD)SlopBuilder);
    // One word: the wrapper's first instruction becomes a branch to ours, which
    // calls the full version itself (so nothing of the original is lost).
    bool watched = Branch(kSceneCreate, (PVOID)SlopSceneCreate, false);
    Log("scene creation watched: %s", watched ? "yes" : "NO (out of range)");
    if (watched)
    {
        HANDLE t = NULL;
        if (NT_OK(StartThread(&t, Starter1888, NULL)) && t)
            CloseHandle(t);
    }
#ifdef SLOPDASH_LOCAL
#include "../local/slopguide_start.inc"
#endif
}


extern "C" BOOL WINAPI DllMain(HANDLE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    Log("SlopGuide 1.07 (for system 2.0.17559)");
    // Synchronously: the change has to be in before the Guide's start-up runs.
    Install();
    NoAvatarPicker();
    plugins::Start();               // the other plugins (Plugins.inc), on a thread of their own
    // No log file: a thread writing it to the stick got stuck in the file
    // system (0.4-0.8) holding its lock, and froze the Guide once it read the
    // signed-in profile. The log stays in memory (g_log).
    return TRUE;
}
