#include "platform/blade_colours.h"

#include <xtl.h>
#include <xui.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "platform/log.h"
#include "platform/profiles.h"
#include "platform/resources.h"
#include "platform/settings.h"

namespace {

#include "theme/blade_colours.inc"

// The parts, in kColour... order (the .inc's groups), and their settings keys.
struct Part
{
    const char* key;
    const wchar_t* name;
    bool hued;              // a blade's own colour (only its coloured parts change); else frames / background
};
const Part kParts[kColourGroups] = {
    { "marketplace", L"Marketplace", true },
    { "live", L"Xbox Live", true },
    { "games", L"Games", true },
    { "media", L"Media", true },
    { "system", L"System", true },
    { "frames", L"Blades", false },
    { "background", L"Background", false },
};

// The colours: a hue (degrees), or none (greys) with a lightness change, or
// the part's own colour darker (kOwnHue: Dark Mode's).
const float kOwnHue = -2;
struct Colour
{
    const wchar_t* name;
    float hue;              // degrees; < 0: grey; kOwnHue: the part's own hue
    float light;            // greys and own hue: 0 keeps the lightness; < 0 darker, > 0 lighter
};
const Colour kColours[] = {
    { L"Original", -1, 0 },
    { L"Red", 0, 0 },          { L"Orange", 26, 0 },      { L"Gold", 42, 0 },
    { L"Yellow", 56, 0 },      { L"Lime", 85, 0 },        { L"Green", 125, 0 },
    { L"Teal", 168, 0 },       { L"Cyan", 188, 0 },       { L"Blue", 212, 0 },
    { L"Indigo", 240, 0 },     { L"Purple", 272, 0 },     { L"Magenta", 300, 0 },
    { L"Pink", 330, 0 },
    { L"Silver", -1, 0.25f },  { L"Charcoal", -1, -0.55f }, { L"Black", -1, -0.85f },
    { L"Dark", kOwnHue, -0.5f },
};
const int kColourCount = sizeof(kColours) / sizeof(kColours[0]);

std::string Key(int part)
{
    return std::string("blades1888.colour.") + kParts[part].key;
}

// --- HSL ---------------------------------------------------------------------------
void ToHsl(float r, float g, float b, float& h, float& s, float& l)
{
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    l = (mx + mn) / 2;
    if (mx == mn)
    {
        h = s = 0;
        return;
    }
    float d = mx - mn;
    s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r)
        h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g)
        h = (b - r) / d + 2;
    else
        h = (r - g) / d + 4;
    h *= 60;
}

float Channel(float p, float q, float t)
{
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

void FromHsl(float h, float s, float l, float& r, float& g, float& b)
{
    if (s <= 0)
    {
        r = g = b = l;
        return;
    }
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q, hh = h / 360;
    r = Channel(p, q, hh + 1.0f / 3);
    g = Channel(p, q, hh);
    b = Channel(p, q, hh - 1.0f / 3);
}

unsigned Byte(float v)
{
    return v <= 0 ? 0 : v >= 1 ? 255 : (unsigned)(v * 255 + 0.5f);
}

// One of the skins' colours in the chosen colour. A blade's: only its coloured
// parts (its shines and shadows, near grey, stay); the hue changes, its own
// saturation and lightness kept. The frames and the background are grey: the
// hue with a tint's saturation, the lightness kept (the background's black
// lifted a little, so the colour shows). Greys: the colour's lightness change.
unsigned Recolour(unsigned argb, const Part& part, const Colour& c, bool background)
{
    float r = ((argb >> 16) & 255) / 255.0f, g = ((argb >> 8) & 255) / 255.0f, b = (argb & 255) / 255.0f;
    float h, s, l;
    ToHsl(r, g, b, h, s, l);
    if (part.hued && s < 0.15f)
        return argb;
    if (c.hue == kOwnHue)
        l = l * (1 + c.light);      // its own hue and saturation, darker
    else if (c.hue < 0)
    {
        s = 0;
        l = c.light < 0 ? l * (1 + c.light) : l + (1 - l) * c.light;
    }
    else if (part.hued)
        h = c.hue;
    else
    {
        h = c.hue;
        s = background ? 0.7f : 0.45f;
        if (background && l < 0.12f)
            l = 0.12f;
    }
    FromHsl(h, s, l, r, g, b);
    return (argb & 0xFF000000) | (Byte(r) << 16) | (Byte(g) << 8) | Byte(b);
}

}  // namespace

int BladeColourParts()
{
    return kColourGroups;
}

const wchar_t* BladeColourPartName(int part)
{
    return part >= 0 && part < kColourGroups ? kParts[part].name : L"";
}

int BladeColourCount(int)
{
    return kColourCount;
}

const wchar_t* BladeColourName(int, int colour)
{
    return colour >= 0 && colour < kColourCount ? kColours[colour].name : L"";
}

int BladeColourIndex(const wchar_t* name)
{
    for (int i = 0; i < kColourCount; i++)
        if (!wcscmp(kColours[i].name, name))
            return i;
    return 0;
}

int BladeColourChoice(int part)
{
    if (part < 0 || part >= kColourGroups)
        return 0;
    std::string v = GetSetting(Key(part).c_str());
    for (int i = 1; i < kColourCount; i++)
    {
        char name[32];
        WideCharToMultiByte(CP_ACP, 0, kColours[i].name, -1, name, sizeof(name), NULL, NULL);
        if (v == name)
            return i;
    }
    return 0;
}

bool SetBladeColourChoice(int part, int colour)
{
    if (part < 0 || part >= kColourGroups || colour < 0 || colour >= kColourCount)
        return false;
    char name[32] = "";
    if (colour > 0)
        WideCharToMultiByte(CP_ACP, 0, kColours[colour].name, -1, name, sizeof(name), NULL, NULL);
    return SetSetting(Key(part).c_str(), name);
}

void ApplyBladeColours()
{
    int choice[kColourGroups];
    bool any = false;
    for (int p = 0; p < kColourGroups; p++)
    {
        choice[p] = BladeColourChoice(p);
        any = any || choice[p] != 0;
    }
    if (!any)
        return;
    unsigned size[2] = { 0, 0 };
    unsigned char* skin[2] = { PackageFileForEdit(L"dashuisk", L"skin.xur", &size[0]),
                               PackageFileForEdit(L"dashuisk6717", L"skin.xur", &size[1]) };
    unsigned changed = 0, skipped = 0;
    for (size_t i = 0; i < sizeof(kBladeColourSpots) / sizeof(kBladeColourSpots[0]); i++)
    {
        const BladeColourSpot& spot = kBladeColourSpots[i];
        int c = choice[spot.group];
        unsigned char* d = skin[spot.skin];
        if (!c)
            continue;
        if (!d || spot.offset + 4 > size[spot.skin])
        {
            skipped++;
            continue;
        }
        unsigned char* at = d + spot.offset;
        unsigned now = ((unsigned)at[0] << 24) | ((unsigned)at[1] << 16) | ((unsigned)at[2] << 8) | at[3];
        if (now != spot.argb)
        {
            skipped++;              // not the skin this table was made from
            continue;
        }
        unsigned v = Recolour(now, kParts[spot.group], kColours[c], spot.group == kColourBackground);
        at[0] = (unsigned char)(v >> 24);
        at[1] = (unsigned char)(v >> 16);
        at[2] = (unsigned char)(v >> 8);
        at[3] = (unsigned char)v;
        changed++;
    }
    Log("blade colours: %u changed, %u left (not the expected skin)", changed, skipped);
}

void RestartForBladeColours()
{
    Log("blade colours: restarting to show them");
    LogFlush();
    XLaunchNewImage("game:\\SlopDash.xex", 0);
}

// --- The gamer card's look (Customize > Gamer Card) ----------
// 1888's gamer card has an Xbox Live Gold banner, a Silver one (GamerCard.xur's
// membershipTierScene: graphic_gold, graphic_silver) and an offline card
// (offlineBaseScene: Gamerscore, Games, Achievements). Kept in the settings and
// in cache\gamercard.txt (its name), where SlopGuide's Guide card reads it.
namespace {
const wchar_t* const kCardStyles[] = { L"Xbox Live Gold", L"Xbox Live Silver", L"Offline" };
const char* const kCardKeys[] = { "", "Silver", "Offline" };
const int kCardStyleCount = sizeof(kCardStyles) / sizeof(kCardStyles[0]);
}

int GamerCardStyles()
{
    return kCardStyleCount;
}

const wchar_t* GamerCardStyleName(int style)
{
    return style >= 0 && style < kCardStyleCount ? kCardStyles[style] : L"";
}

int GamerCardStyle()
{
    std::string v = GetSetting("blades1888.gamercard");
    for (int i = 1; i < kCardStyleCount; i++)
        if (v == kCardKeys[i])
            return i;
    return 0;
}

bool SetGamerCardStyle(int style)
{
    if (style < 0 || style >= kCardStyleCount)
        return false;
    CreateDirectoryA("game:\\cache", NULL);
    HANDLE h = CreateFileA("game:\\cache\\gamercard.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD put = 0;
        WriteFile(h, kCardKeys[style], (DWORD)strlen(kCardKeys[style]), &put, NULL);
        CloseHandle(h);
    }
    return SetSetting("blades1888.gamercard", kCardKeys[style]);
}

// --- Dashboard Trim ------------------------------------------------------------
namespace {
int g_trimLoaded = 0;
}

int ProfileTrim()
{
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
    {
        XUID xuid = 0;
        if (XUserGetSigninState(i) == eXUserSigninState_NotSignedIn || XUserGetXUID(i, &xuid) != ERROR_SUCCESS ||
            xuid == HiddenProfile())
            continue;
        char path[64], text[4] = "";
        _snprintf_s(path, sizeof(path), _TRUNCATE, "hdd:\\SlopDash\\Trims\\%016I64X.txt", xuid);
        HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return 0;
        DWORD got = 0;
        ReadFile(h, text, sizeof(text) - 1, &got, NULL);
        CloseHandle(h);
        int trim = atoi(text);
        return trim == 1 || trim == 2 ? trim : 0;
    }
    return 0;
}

void LoadTrim()
{
    // (Kept even if it fails to load: TrimChanged mustn't restart SlopDash
    // over and over for a trim that can't show.)
    int trim = g_trimLoaded = ProfileTrim();
    std::wstring base;
    if (!trim || !PackageLocator(trim == 1 ? L"dashskn1" : L"dashskn2", base))
        return;
    // As 1888 (920D0750): its visuals over the skin's, under no prefix. (Not
    // LoadSkin: that moves the skin's base path to this package.)
    std::wstring file = base + L"BladeSkin.xur";
    HRESULT hr = XuiLoadVisualFromBinary(file.c_str(), NULL);
    Log("trim %d %S: %08X", trim, file.c_str(), (unsigned)hr);
}

bool TrimChanged()
{
    return ProfileTrim() != g_trimLoaded;
}
