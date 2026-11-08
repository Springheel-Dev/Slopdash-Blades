// The console's own settings store (XConfig): what the System blade shows and
// changes. Categories, setting IDs and flag bits as documented by Xenia
// (src/xenia/kernel/xconfig.h); the calls are the kernel's ExGetXConfigSetting
// and ExSetXConfigSetting.
#pragma once

#include <xtl.h>
#include <string>

namespace xconfig {

enum Category
{
    kUser = 0x03,
    kConsole = 0x07,
};

enum UserSetting
{
    kTimeZoneBias = 0x01,
    kTimeZoneStdName = 0x02,
    kTimeZoneDltName = 0x03,
    kTimeZoneStdDate = 0x04,
    kTimeZoneDltDate = 0x05,
    kTimeZoneStdBias = 0x06,
    kTimeZoneDltBias = 0x07,
    kLanguage = 0x09,
    kVideoFlags = 0x0A,
    kAudioFlags = 0x0B,
    kRetailFlags = 0x0C,
    kCountry = 0x0E,
    kParentalFlags = 0x0F,          // one byte; bit 0x80: the remote control (1888 dash.xex, 92118B10)
};

enum ConsoleSetting
{
    kScreenSaver = 0x01,
    kAutoShutOff = 0x02,
};

// XCONFIG_USER_RETAIL_FLAGS bits.
const DWORD kRetailDstOff = 0x00000002;
const DWORD kRetail24HourClock = 0x00000008;

// XCONFIG_USER_AUDIO_FLAGS.
const DWORD kAudioAnalogMask = 0x0000FFFF;
const DWORD kAudioDolbyProLogic = 0x00000001;
const DWORD kAudioAnalogMono = 0x00000002;
const DWORD kAudioDigitalMask = 0xFFFF0000 & ~0x80000000;
const DWORD kAudioDolbyDigital = 0x00010000;
const DWORD kAudioDolbyDigitalWmaPro = 0x00030000;

bool Read(WORD category, WORD setting, void* buffer, WORD size);
bool Write(WORD category, WORD setting, const void* buffer, WORD size);
DWORD ReadU32(WORD category, WORD setting, DWORD fallback = 0);
short ReadS16(WORD category, WORD setting, short fallback = 0);

// What the System screens show as "Current Setting".
std::wstring DisplayText();
std::wstring AudioText();
std::wstring LanguageText();
std::wstring LocaleText();
std::wstring ClockText();           // date and time now, format, zone
std::wstring TimeZoneText();
bool Is24HourClock();
bool IsDaylightSaving();
bool IsAutoOffOn();
bool IsScreensaverOn();
std::wstring SystemInfoText();

// Changes, as 1888's dashboard makes them (its dash.xex's ExSetXConfigSetting
// calls): Auto-Off 360 minutes or 0 (console setting 2), Screen Saver 10
// minutes or 4096, off (console setting 1), both 2 bytes.
bool SetAutoOff(bool on);
bool SetScreensaver(bool on);
// Audio: analog 0 Mono, 1 Dolby Pro Logic II; digital 0 Digital Stereo,
// 1 Dolby Digital 5.1, 2 Dolby Digital with WMA Pro (1888's lists' order).
int AnalogAudioIndex();
int DigitalAudioIndex();
bool SetAnalogAudio(int index);
bool SetDigitalAudio(int index);
// Remote Control: 1888's list, item 0 All Channels, item 1 Xbox 360 Media
// Remote (its strings 122, 123, taken in order); item 0 is bit 0x80 of the
// parental flags byte set (dash.xex 92118B10 reads it so).
int RemoteIndex();
bool SetRemote(int index);
const wchar_t* RemoteName(int index);
// Time zones (Xenia's table, as TimeZoneText): every one, the console's, and a
// change (user settings 1 to 7, 4 bytes each, as 1888 writes them).
int ZoneCount();
const wchar_t* ZoneName(int index);
int CurrentZone();
bool SetZone(int index);
// Locales: the names LocaleText uses, in XGetLocale's order.
int LocaleCount();
const wchar_t* LocaleName(int index);
int CurrentLocale();
// A change, as 1888's dash.xex makes it (92124A18): the locale's country
// (XONLINE_COUNTRY_*, the same name as its XC_LOCALE_*) in user setting 14, one
// byte. Worldwide has no country: false.
bool SetLocale(int index);
// Language: XC_LANGUAGE_* in user setting 9, 4 bytes (1888 dash.xex 921249C8).
bool SetLanguage(DWORD language);
// The console's clock, set from a local date and time.
bool SetLocalTime(const SYSTEMTIME& local);
// Display: what the console runs now (read only: a change needs 1888's
// can-you-see-it check, which isn't built).
bool IsWidescreen();
bool IsPal60();

}  // namespace xconfig
