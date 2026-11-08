#include "platform/xconfig.h"

#include <xconfig.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "platform/log.h"

extern "C" {
LONG ExGetXConfigSetting(WORD category, WORD setting, PVOID buffer, WORD size, PWORD required);
LONG ExSetXConfigSetting(WORD category, WORD setting, PVOID buffer, DWORD size);
LONG NtSetSystemTime(PLARGE_INTEGER newTime, PLARGE_INTEGER oldTime);
}

namespace xconfig {

namespace {

struct Zone
{
    int bias;
    const char* stdName;
    const char* dltName;
    BYTE stdDate[4], dltDate[4];
    int dltBias;
    const wchar_t* name;
};
const Zone kZones[] = {
#include "platform/timezones.inc"
};

std::wstring Format(const wchar_t* fmt, ...)
{
    wchar_t buf[256];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, 256, _TRUNCATE, fmt, args);
    va_end(args);
    return buf;
}

}  // namespace

bool Read(WORD category, WORD setting, void* buffer, WORD size)
{
    WORD required = 0;
    LONG r = ExGetXConfigSetting(category, setting, buffer, size, &required);
    if (r < 0)
        Log("xconfig: read %u/%u failed %08X (needs %u bytes)", category, setting, (unsigned)r, required);
    return r >= 0;
}

bool Write(WORD category, WORD setting, const void* buffer, WORD size)
{
    LONG r = ExSetXConfigSetting(category, setting, (PVOID)buffer, size);
    Log("xconfig: write %u/%u (%u bytes): %08X", category, setting, size, (unsigned)r);
    return r >= 0;
}

DWORD ReadU32(WORD category, WORD setting, DWORD fallback)
{
    DWORD v = 0;
    return Read(category, setting, &v, sizeof(v)) ? v : fallback;
}

short ReadS16(WORD category, WORD setting, short fallback)
{
    short v = 0;
    return Read(category, setting, &v, sizeof(v)) ? v : fallback;
}

std::wstring DisplayText()
{
    XVIDEO_MODE m;
    memset(&m, 0, sizeof(m));
    XGetVideoMode(&m);
    return Format(L"%ux%u%s, %s%s", m.dwDisplayWidth, m.dwDisplayHeight, m.fIsInterlaced ? L"i" : L"p",
                  m.fIsWideScreen ? L"widescreen" : L"standard", m.fIsHiDef ? L", high definition" : L"");
}

std::wstring AudioText()
{
    DWORD f = ReadU32(kUser, kAudioFlags);
    const wchar_t* analog = (f & 0xFFFF) == kAudioDolbyProLogic ? L"Dolby Pro Logic II"
                          : (f & 0xFFFF) == kAudioAnalogMono ? L"Mono" : L"Stereo";
    DWORD d = f & 0x7FFF0000;
    const wchar_t* digital = d == kAudioDolbyDigitalWmaPro ? L"Dolby Digital with WMA Pro"
                           : d == kAudioDolbyDigital ? L"Dolby Digital 5.1" : L"Digital Stereo";
    return Format(L"Analog: %s\r\nDigital: %s", analog, digital);
}

std::wstring LanguageText()
{
    static const wchar_t* const kNames[] = { L"Auto", L"English", L"Japanese", L"German", L"French", L"Spanish",
                                             L"Italian", L"Korean", L"Chinese (Traditional)", L"Portuguese",
                                             L"Chinese (Simplified)", L"Polish", L"Russian", L"Swedish", L"Turkish",
                                             L"Norwegian", L"Dutch", L"Chinese (Simplified)" };
    DWORD l = XGetLanguage();
    return l < sizeof(kNames) / sizeof(kNames[0]) ? kNames[l] : Format(L"Language %u", l);
}

namespace {
const wchar_t* const kLocaleNames[] = {
        L"Unknown", L"Australia", L"Austria", L"Belgium", L"Brazil", L"Canada", L"Chile", L"China", L"Colombia",
        L"Czech Republic", L"Denmark", L"Finland", L"France", L"Germany", L"Greece", L"Hong Kong", L"Hungary",
        L"India", L"Ireland", L"Italy", L"Japan", L"Korea", L"Mexico", L"Netherlands", L"New Zealand", L"Norway",
        L"Poland", L"Portugal", L"Singapore", L"Slovak Republic", L"South Africa", L"Spain", L"Sweden",
        L"Switzerland", L"Taiwan", L"United Kingdom", L"United States", L"Russia", L"Worldwide", L"Turkey",
        L"Argentina", L"Saudi Arabia", L"Israel", L"United Arab Emirates" };
const int kLocaleCount = sizeof(kLocaleNames) / sizeof(kLocaleNames[0]);
}  // namespace

std::wstring LocaleText()
{
    DWORD l = XGetLocale();
    return (int)l < kLocaleCount ? kLocaleNames[l] : Format(L"Locale %u", l);
}

bool Is24HourClock()
{
    return (ReadU32(kUser, kRetailFlags) & kRetail24HourClock) != 0;
}

bool IsDaylightSaving()
{
    return (ReadU32(kUser, kRetailFlags) & kRetailDstOff) == 0;
}

std::wstring TimeZoneText()
{
    int bias = (int)ReadU32(kUser, kTimeZoneBias);
    char stdName[5] = { 0 };
    Read(kUser, kTimeZoneStdName, stdName, 4);
    const Zone* best = NULL;
    for (size_t i = 0; i < sizeof(kZones) / sizeof(kZones[0]); i++)
        if (kZones[i].bias == bias && (!best || !strncmp(kZones[i].stdName, stdName, 4)))
            best = &kZones[i];
    if (best)
        return best->name;
    return Format(L"GMT%+d:%02d", -bias / 60, (bias < 0 ? -bias : bias) % 60);
}

std::wstring ClockText()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    int hour = t.wHour;
    bool h24 = Is24HourClock();
    const wchar_t* ampm = L"";
    if (!h24)
    {
        ampm = hour < 12 ? L" AM" : L" PM";
        hour = hour % 12 == 0 ? 12 : hour % 12;
    }
    return Format(L"%02u/%02u/%04u %u:%02u%s\r\n%s clock\r\n%s\r\nDaylight saving: %s", t.wDay, t.wMonth, t.wYear, hour,
                  t.wMinute, ampm, h24 ? L"24-hour" : L"12-hour", TimeZoneText().c_str(),
                  IsDaylightSaving() ? L"On" : L"Off");
}

bool IsAutoOffOn()
{
    return ReadS16(kConsole, kAutoShutOff) != 0;
}

bool IsScreensaverOn()
{
    short v = ReadS16(kConsole, kScreenSaver);
    return v > 0 && v < 4096;           // 4096: 1888's "off"
}

std::wstring SystemInfoText()
{
    ULARGE_INTEGER freeBytes, total;
    std::wstring s = L"Dashboard: SlopDash (Blades theme)";
    if (GetDiskFreeSpaceExA("hdd:\\", &freeBytes, &total, NULL))
        s += Format(L"\r\nHard drive: %.1f GB free of %.1f GB", freeBytes.QuadPart / 1e9, total.QuadPart / 1e9);
    return s;
}

bool SetAutoOff(bool on)
{
    short v = on ? 360 : 0;
    return Write(kConsole, kAutoShutOff, &v, sizeof(v));
}

bool SetScreensaver(bool on)
{
    short v = on ? 10 : 4096;
    return Write(kConsole, kScreenSaver, &v, sizeof(v));
}

int AnalogAudioIndex()
{
    return (ReadU32(kUser, kAudioFlags) & kAudioAnalogMask) == kAudioAnalogMono ? 0 : 1;
}

int DigitalAudioIndex()
{
    DWORD d = ReadU32(kUser, kAudioFlags) & 0x7FFF0000;
    return d == kAudioDolbyDigitalWmaPro ? 2 : d == kAudioDolbyDigital ? 1 : 0;
}

bool SetAnalogAudio(int index)
{
    DWORD f = ReadU32(kUser, kAudioFlags);
    f = (f & ~kAudioAnalogMask) | (index == 0 ? kAudioAnalogMono : kAudioDolbyProLogic);
    return Write(kUser, kAudioFlags, &f, sizeof(f));
}

bool SetDigitalAudio(int index)
{
    static const DWORD kValues[] = { 0, kAudioDolbyDigital, kAudioDolbyDigitalWmaPro };
    if (index < 0 || index > 2)
        return false;
    DWORD f = ReadU32(kUser, kAudioFlags);
    f = (f & ~0x7FFF0000) | kValues[index];
    return Write(kUser, kAudioFlags, &f, sizeof(f));
}

int RemoteIndex()
{
    BYTE b = 0;
    return Read(kUser, kParentalFlags, &b, 1) && (b & 0x80) ? 0 : 1;
}

bool SetRemote(int index)
{
    BYTE b = 0;
    if (!Read(kUser, kParentalFlags, &b, 1))
        return false;
    b = index == 0 ? (BYTE)(b | 0x80) : (BYTE)(b & ~0x80);
    return Write(kUser, kParentalFlags, &b, 1);
}

const wchar_t* RemoteName(int index)
{
    return index == 0 ? L"All Channels" : L"Xbox 360 Media Remote";
}

int ZoneCount()
{
    return (int)(sizeof(kZones) / sizeof(kZones[0]));
}

const wchar_t* ZoneName(int index)
{
    return index >= 0 && index < ZoneCount() ? kZones[index].name : L"";
}

int CurrentZone()
{
    int bias = (int)ReadU32(kUser, kTimeZoneBias);
    char stdName[5] = { 0 };
    Read(kUser, kTimeZoneStdName, stdName, 4);
    int best = -1;
    for (int i = 0; i < ZoneCount(); i++)
        if (kZones[i].bias == bias && (best < 0 || !strncmp(kZones[i].stdName, stdName, 4)))
            best = i;
    return best;
}

bool SetZone(int index)
{
    if (index < 0 || index >= ZoneCount())
        return false;
    const Zone& z = kZones[index];
    LONG bias = z.bias, stdBias = 0, dltBias = z.dltBias;
    char stdName[4] = { 0 }, dltName[4] = { 0 };
    for (int i = 0; i < 4 && z.stdName[i]; i++)
        stdName[i] = z.stdName[i];
    for (int i = 0; i < 4 && z.dltName[i]; i++)
        dltName[i] = z.dltName[i];
    bool ok = Write(kUser, kTimeZoneBias, &bias, 4) && Write(kUser, kTimeZoneStdName, stdName, 4) &&
              Write(kUser, kTimeZoneDltName, dltName, 4) && Write(kUser, kTimeZoneStdDate, z.stdDate, 4) &&
              Write(kUser, kTimeZoneDltDate, z.dltDate, 4) && Write(kUser, kTimeZoneStdBias, &stdBias, 4) &&
              Write(kUser, kTimeZoneDltBias, &dltBias, 4);
    Log("xconfig: time zone %S: %s", z.name, ok ? "set" : "FAILED");
    return ok;
}

int LocaleCount()
{
    return kLocaleCount;
}

const wchar_t* LocaleName(int index)
{
    return index >= 0 && index < kLocaleCount ? kLocaleNames[index] : L"";
}

bool SetLocale(int index)
{
    // XC_LOCALE_* (1 Australia .. 43 United Arab Emirates) -> XONLINE_COUNTRY_*
    // of the same name (the XDK's xconfig.h and xonline.h); 38 Worldwide: none.
    static const BYTE kCountryOf[] = { 0,  6,  5,  8,  13, 16, 19, 20, 21, 23, 25, 32, 34, 24, 37, 39, 42, 46, 44, 50, 53, 56,
                                     71, 74, 76, 75, 82, 84, 91, 93, 109, 31, 90, 18, 101, 35, 103, 88, 0, 99, 4, 89, 45, 1 };
    if (index <= 0 || index >= (int)sizeof(kCountryOf) || !kCountryOf[index])
        return false;
    BYTE c = kCountryOf[index];
    return Write(kUser, kCountry, &c, 1);
}

bool SetLanguage(DWORD language)
{
    return language >= 1 && language <= 0x11 && Write(kUser, kLanguage, &language, sizeof(language));
}

int CurrentLocale()
{
    DWORD l = XGetLocale();
    return (int)l < kLocaleCount ? (int)l : -1;
}

bool SetLocalTime(const SYSTEMTIME& local)
{
    // The local time's offset from UTC now (time zone and daylight saving as
    // the console has them), taken off the new local time.
    SYSTEMTIME nowLocal, nowUtc;
    GetLocalTime(&nowLocal);
    GetSystemTime(&nowUtc);
    FILETIME a, b, n;
    if (!SystemTimeToFileTime(&nowLocal, &a) || !SystemTimeToFileTime(&nowUtc, &b) ||
        !SystemTimeToFileTime(&local, &n))
        return false;
    LARGE_INTEGER la, lb, ln;
    la.LowPart = a.dwLowDateTime;
    la.HighPart = (LONG)a.dwHighDateTime;
    lb.LowPart = b.dwLowDateTime;
    lb.HighPart = (LONG)b.dwHighDateTime;
    ln.LowPart = n.dwLowDateTime;
    ln.HighPart = (LONG)n.dwHighDateTime;
    LARGE_INTEGER utc;
    utc.QuadPart = ln.QuadPart - (la.QuadPart - lb.QuadPart);
    LONG r = NtSetSystemTime(&utc, NULL);
    Log("xconfig: clock set %02u/%02u/%04u %02u:%02u: %08X", local.wDay, local.wMonth, local.wYear, local.wHour,
        local.wMinute, (unsigned)r);
    return r >= 0;
}

bool IsWidescreen()
{
    XVIDEO_MODE m;
    memset(&m, 0, sizeof(m));
    XGetVideoMode(&m);
    return m.fIsWideScreen != FALSE;
}

bool IsPal60()
{
    XVIDEO_MODE m;
    memset(&m, 0, sizeof(m));
    XGetVideoMode(&m);
    return m.RefreshRate >= 59.0f;
}

}  // namespace xconfig
