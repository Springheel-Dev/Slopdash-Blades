#include "theme/blades1888.h"
#include "platform/blade_colours.h"

#include <xui.h>
#include <xuiapp.h>
#include <xonline.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <stdio.h>
#include <wctype.h>

#include "core/library.h"
#include "platform/https.h"
#include "platform/net.h"
#include "platform/achievements.h"
#include "platform/console.h"
#include "platform/music.h"
#include "platform/videos.h"
#include "platform/movie.h"
#include "platform/storage.h"
#include "platform/pictures.h"
#include "platform/launcher.h"
#include "platform/log.h"
#include "platform/profiles.h"
#include "platform/scanner.h"
#include "platform/settings.h"
#include "platform/themes.h"
#include "platform/xconfig.h"
#include "theme/blades.h"
#include "platform/resources.h"
#include "platform/stand_ins.h"

// Layouts, frames and property lists below are 2.0.1888's own, as decoded from
// its dash.xex and xam.xex (and the xam.xex teardown): the blades' content
// scenes (gamesbla, mediabla, live) and xam's gamer card (gamercrd#GamerCard.xur).

// Kernel exports the XDK headers don't declare.
extern "C" {
LONG XexGetModuleHandle(PCHAR name, PHANDLE module);
LONG XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID* address);
VOID HalSendSMCMessage(PVOID command, PVOID reply);     // 16-byte buffers
}

namespace launch {

// --- helpers -------------------------------------------------------------------

HXUIOBJ g_openedFrom = NULL;    // the blade content a screen was opened from (hidden meanwhile)
std::vector<HXUIOBJ> g_bladeChrome;     // that blade's header and legends (hidden too)
HXUIOBJ g_openedFocus = NULL;   // the button that had the focus then: it gets it back

HRESULT AddScene(HXUIOBJ parent, const wchar_t* package, const wchar_t* scene, HXUIOBJ* added)
{
    *added = NULL;
    std::wstring base;
    if (!PackageLocator(package, base))
    {
        Log("1888: no package %S for %S", package, scene);
        return E_FAIL;
    }
    HXUIOBJ child = NULL;
    HRESULT hr = XuiSceneCreate(base.c_str(), scene, NULL, &child);
    if (SUCCEEDED(hr))
        hr = XuiElementAddChild(parent, child);
    Log("1888: %S#%S: %08X", package, scene, (unsigned)hr);
    if (SUCCEEDED(hr))
        *added = child;
    return hr;
}

HXUIOBJ Child(HXUIOBJ parent, const wchar_t* id)
{
    HXUIOBJ h = NULL;
    if (parent)
        XuiElementGetChildById(parent, id, &h);
    return h;
}

// The first element with this id anywhere under `parent`.
HXUIOBJ Find(HXUIOBJ parent, const wchar_t* id)
{
    for (HXUIOBJ c = NULL; parent && SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(parent, &c)) && c;)
    {
        LPCWSTR cid = NULL;
        if (SUCCEEDED(XuiElementGetId(c, &cid)) && cid && !wcscmp(cid, id))
            return c;
        if (HXUIOBJ deeper = Find(c, id))
            return deeper;
    }
    return NULL;
}

std::wstring ElementId(HXUIOBJ obj)
{
    LPCWSTR id = NULL;
    if (obj && SUCCEEDED(XuiElementGetId(obj, &id)) && id)
        return id;
    return L"";
}

// Controls (labels, buttons) and plain XuiText elements alike.
void SetText(HXUIOBJ parent, const wchar_t* id, const wchar_t* text)
{
    HXUIOBJ h = Child(parent, id);
    if (!h)
        return;
    // XuiControlSetText "succeeds" on a plain XuiText without changing it
    // (0.0.86-0.0.88 left hudbkgnd's placeholders, SniperMonkey and 4:20pm).
    if (XuiIsInstanceOf(h, XuiFindClass(XUI_CLASS_TEXT)))
        XuiTextElementSetText(h, text);
    else
        XuiControlSetText(h, text);
}

void LogBox(const wchar_t* what, HXUIOBJ obj)
{
    if (!obj)
    {
        Log("  %S: missing", what);
        return;
    }
    float w = 0, h = 0;
    D3DXVECTOR3 p(0, 0, 0);
    XuiElementGetBounds(obj, &w, &h);
    XuiElementGetPosition(obj, &p);
    Log("  %S: %.1f x %.1f at (%.1f, %.1f)%s", what, w, h, p.x, p.y, XuiElementIsShown(obj) ? "" : " hidden");
}

void Show(HXUIOBJ parent, const wchar_t* id, bool show)
{
    HXUIOBJ h = Child(parent, id);
    if (h)
        XuiElementSetShow(h, show);
}

HRESULT PlayRange(HXUIOBJ obj, const wchar_t* from, const wchar_t* to, bool recurse)
{
    int start = -1, end = -1;
    HRESULT hr = XuiElementFindNamedFrame(obj, from, &start);
    if (SUCCEEDED(hr))
        hr = XuiElementFindNamedFrame(obj, to, &end);
    if (SUCCEEDED(hr))
        hr = XuiElementPlayTimeline(obj, start, start, end, FALSE, recurse ? TRUE : FALSE);
    return hr;
}

int SignedInUser()
{
    for (int i = 0; i < XUSER_MAX_COUNT; i++)
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn)
            return i;
    return -1;
}

XUID WhoIsSignedIn()
{
    int user = SignedInUser();
    if (user < 0)
        return 0;
    XUID xuid = 0;
    if (XUserGetXUID(user, &xuid) != ERROR_SUCCESS || xuid == 0)
        xuid = 1;                       // signed in, XUID unknown: still someone
    return xuid;
}

// --- the gamer card's numbers -------------------------------------------------------
// The offline card shows gamerscore, games played and achievements earned: the
// dashboard's (title FFFE07D1) profile settings. Read asynchronously only: a
// synchronous XUserReadProfileSettings switched the console off (6717 theme, 0.0.17).
const DWORD kDashTitleId = 0xFFFE07D1;
const int kReadPolls = 100;             // x 100 ms

struct CardRead
{
    XOVERLAPPED ov;
    DWORD size;
    bool pending;
    int polls;
    BYTE buf[4096];
};
CardRead g_read;

bool StartCardRead(int user)
{
    if (g_read.pending && !XHasOverlappedIoCompleted(&g_read.ov))
        return false;
    static DWORD ids[3] = { XPROFILE_GAMERCARD_CRED, XPROFILE_GAMERCARD_TITLES_PLAYED,
                            XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED };
    memset(&g_read.ov, 0, sizeof(g_read.ov));
    g_read.size = sizeof(g_read.buf);
    g_read.polls = 0;
    DWORD r = XUserReadProfileSettings(kDashTitleId, user, 3, ids, &g_read.size,
                                       (XUSER_READ_PROFILE_SETTING_RESULT*)g_read.buf, &g_read.ov);
    Log("1888 gamer card: profile read started: %u", (unsigned)r);
    if (r != ERROR_IO_PENDING && r != ERROR_SUCCESS)
    {
        // 0.0.85-0.0.89: 1627 at once. The same settings asked by XUID (as a
        // gamer card for anyone is read).
        XUID xuid = 0;
        if (XUserGetXUID(user, &xuid) == ERROR_SUCCESS)
        {
            memset(&g_read.ov, 0, sizeof(g_read.ov));
            g_read.size = sizeof(g_read.buf);
            r = XUserReadProfileSettingsByXuid(kDashTitleId, user, 1, &xuid, 3, ids, &g_read.size,
                                               (XUSER_READ_PROFILE_SETTING_RESULT*)g_read.buf, &g_read.ov);
            Log("1888 gamer card: read by XUID started: %u", (unsigned)r);
        }
    }
    g_read.pending = r == ERROR_IO_PENDING || r == ERROR_SUCCESS;
    return g_read.pending;
}

// True once finished or given up; the numbers are -1 where unknown.
bool CardReadDone(int& score, int& played, int& earned)
{
    score = played = earned = -1;
    if (!XHasOverlappedIoCompleted(&g_read.ov))
        return ++g_read.polls >= kReadPolls;
    g_read.pending = false;
    DWORD result = 0;
    if (XGetOverlappedResult(&g_read.ov, &result, FALSE) != ERROR_SUCCESS)
        return true;
    const XUSER_READ_PROFILE_SETTING_RESULT* res = (const XUSER_READ_PROFILE_SETTING_RESULT*)g_read.buf;
    for (DWORD i = 0; i < res->dwSettingsLen; i++)
    {
        const XUSER_PROFILE_SETTING& s = res->pSettings[i];
        if (s.data.type != XUSER_DATA_TYPE_INT32)
            continue;
        if (s.dwSettingId == XPROFILE_GAMERCARD_CRED) score = (int)s.data.nData;
        else if (s.dwSettingId == XPROFILE_GAMERCARD_TITLES_PLAYED) played = (int)s.data.nData;
        else if (s.dwSettingId == XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED) earned = (int)s.data.nData;
    }
    return true;
}

void SetNumber(HXUIOBJ parent, const wchar_t* id, int n)
{
    wchar_t buf[16];
    if (n < 0)
        buf[0] = 0;
    else
        _snwprintf_s(buf, 16, _TRUNCATE, L"%d", n);
    SetText(parent, id, buf);
}

// --- XuiGamerCard ---------------------------------------------------------------
// The blades' control: P0 its size, one of GamerCard.xur's own frame ranges
// ("Normal", "Macro", "Mini"); P1 ShowExtendedPanel (xam's names for them).
// In 1888 xam drew it from gamercrd#GamerCard.xur, which is what this does.
const StandInInfo kGamerCard = { L"XuiGamerCard", L"XuiControl", 2, { 5, 1 } };
const DWORD kBuildTimer = 0x47434231;           // "GCB1"
const DWORD kReadTimer = 0x47435231;            // "GCR1"
const DWORD kFocusTimer = 0x47434631;           // "GCF1"
std::map<HXUIOBJ, HXUIOBJ> g_cards;             // control -> its GamerCard.xur scene
std::map<HXUIOBJ, bool> g_lit;
std::map<HXUIOBJ, std::wstring> g_modes;        // control -> its size ("Macro"...)
std::map<HXUIOBJ, int> g_stars;                 // control -> its Rep, in quarter stars
const DWORD kLayoutTimer = 0x47434C31;          // "GCL1"
const DWORD kMeasureTimer = 0x47434D31;         // "GCM1"

// The card's look (System > Themes > X > Gamer Card;
// platform/blade_colours.h): 1888's own Xbox Live Gold or Silver banner on the
// Live card (membershipTierScene), or its offline card (offlineBaseScene,
// filled as the Live card is).
void CardStyle(HXUIOBJ card, int user, XUID xuid)
{
    int style = GamerCardStyle();
    HXUIOBJ online = Child(card, L"onlineBaseScene"), offline = Child(card, L"offlineBaseScene");
    HXUIOBJ tier = Child(online, L"membershipTierScene");
    Show(tier, L"graphic_gold", style == 0);
    Show(tier, L"graphic_silver", style == 1);
    Show(tier, L"graphic_mem", false);
    if (online)
        XuiElementSetShow(online, style != 2);
    if (offline)
        XuiElementSetShow(offline, style == 2);
    if (style != 2 || !offline)
        return;
    char name[XUSER_NAME_SIZE + 1] = { 0 };
    std::wstring tag;
    if (XUserGetName(user, name, sizeof(name)) == ERROR_SUCCESS)
        for (const char* c = name; *c; c++)
            tag += (wchar_t)(unsigned char)*c;
    SetText(offline, L"gamerTagText", tag.c_str());
    int score = -1, played = -1, earned = -1;
    ProfileNumbers(xuid, score, played, earned);
    GuideCardNumbers(xuid, score, played, earned);
    SetNumber(offline, L"credText", score < 0 ? 0 : score);
    SetNumber(offline, L"titlesPlayedText", played < 0 ? 0 : played);
    SetNumber(offline, L"achievementsText", earned < 0 ? 0 : earned);
    HXUIOBJ tile = Child(offline, L"gamerTileImage");
    if (tile && xuid)
        XuiImageElementSetImagePath(tile, ProfilePictureLocator(xuid).c_str());
}

void BuildCard(HXUIOBJ obj)
{
    int user = SignedInUser();
    if (user < 0)
        return;
    HXUIOBJ card = NULL;
    if (FAILED(AddScene(obj, L"gamercrd", L"GamerCard.xur", &card)) || !card)
        return;
    g_cards[obj] = card;
    // Our own copy of the size: XUI's copy of a stand-in's property can be garbage.
    std::wstring mode = L"Macro";
    const XUIElementPropVal* own = StandInProperty(obj, L"XuiGamerCard", 0);
    if (own && own->type == XUI_EPT_STRING && own->szVal &&
        (!wcscmp(own->szVal, L"Normal") || !wcscmp(own->szVal, L"Macro") || !wcscmp(own->szVal, L"Mini")))
        mode = own->szVal;
    // In the Guide's Gamer Profile (gamerprofile#GamerCardScene.xur, its card
    // 425 x 450): the full card, as 1888 showed it.
    for (HXUIOBJ up = NULL, o = obj; SUCCEEDED(XuiElementGetParent(o, &up)) && up; o = up)
        if (ElementId(up) == L"gamerCardScene")
        {
            mode = L"Normal";
            break;
        }
    g_modes[obj] = mode;
    XuiSetTimer(obj, kLayoutTimer, 100);    // its size: once the new scene has started its own timeline

    // The Live base (GamerCard.xur's onlineBaseScene: Rep,
    // Gamerscore, Zone, under the gold membership banner), with the profile's
    // own gamer zone (the Guide reads it: ProfileZoneName; "Homebrew" with
    // none). The offline base and the extended panel stay hidden.
    HXUIOBJ offline = Child(card, L"onlineBaseScene");
    Show(card, L"offlineBaseScene", false);
    Show(card, L"extendedScene", false);
    if (!offline)
        return;
    XuiElementSetShow(offline, TRUE);
    HXUIOBJ tier = Child(offline, L"membershipTierScene");
    Show(tier, L"graphic_gold", true);
    Show(tier, L"graphic_silver", false);
    Show(tier, L"graphic_mem", false);
    XUID zoneOf = 0;
    XUserGetXUID(user, &zoneOf);
    SetText(offline, L"zoneText", ProfileZoneName(zoneOf));
    char name[XUSER_NAME_SIZE + 1] = { 0 };
    if (XUserGetName(user, name, sizeof(name)) == ERROR_SUCCESS)
    {
        std::wstring tag;
        for (const char* c = name; *c; c++)
            tag += (wchar_t)(unsigned char)*c;
        SetText(offline, L"gamerTagText", tag.c_str());
        Log("1888 gamer card: %S", tag.c_str());
    }
    // Until the profile read answers: blank, not the file's "<count>" placeholders.
    SetNumber(offline, L"credText", -1);
    XUID xuid = 0;
    HXUIOBJ tile = Child(offline, L"gamerTileImage");
    if (tile && XUserGetXUID(user, &xuid) == ERROR_SUCCESS)
        XuiImageElementSetImagePath(tile, ProfilePictureLocator(xuid).c_str());   // the Guide's read first
    // XUserReadProfileSettings refuses (1627, 0.0.85-0.0.90): the numbers come
    // from the profile's own dashboard settings file, read while it was signed
    // out (platform/profiles.cpp keeps them).
    int score = -1, played = -1, earned = -1;
    if (XUserGetXUID(user, &xuid) == ERROR_SUCCESS)
        ProfileNumbers(xuid, score, played, earned);
    // Newer: what the Guide read from the profile while it's signed in (all a
    // profile made since its sign-in has).
    GuideCardNumbers(xuid, score, played, earned);
    // The profile's own settings, read now through the system (platform/achievements).
    AchievementData now;
    bool fresh = xuid && LoadAchievements(xuid, now, true);
    if (fresh && now.gamerscoreSetting >= 0)
        score = now.gamerscoreSetting;
    SetNumber(offline, L"credText", score);
    CardStyle(card, user, xuid);
    // Rep: XPROFILE_GAMERCARD_REP (0 to 100) as 0 to 5 stars, in quarters
    // (ReputationScene: Starfull1..5 over the Star outlines, Star.25/.5/.75 for
    // a part star at the next one's place, 31.56 apart). A profile without one
    // (an offline profile has none) shows five, as the user's reference does.
    float stars = fresh && now.hasRep ? now.rep / 20.0f : 5.0f;
    if (stars < 0)
        stars = 0;
    if (stars > 5)
        stars = 5;
    int full = (int)stars, quarter = (int)((stars - full) * 4.0f + 0.5f);
    if (quarter == 4)
    {
        full++;
        quarter = 0;
    }
    g_stars[obj] = full * 4 + quarter;             // shown once the card is laid out (LayOutCard)
    Log("1888 gamer card: gamerscore %d (%s), rep %s %.1f = %d stars + %d quarters", score,
        fresh ? "read now" : "cached", fresh && now.hasRep ? "stored" : "none", fresh ? now.rep : 0.0f, full, quarter);
}

// The Rep stars (ReputationScene: Starfull1..5 over the Star outlines, Star.25
// /.5/.75 for a part star at the next one's place, 31.56 apart). After the
// card's size range: that range sets them back (0.0.99 showed outlines only).
void ShowStars(HXUIOBJ base, int quarters)
{
    int full = quarters / 4, quarter = quarters % 4;
    HXUIOBJ repScene = Child(base, L"reputationScene");
    for (int i = 1; i <= 5; i++)
    {
        wchar_t id[16];
        _snwprintf_s(id, 16, _TRUNCATE, L"Starfull%d", i);
        Show(repScene, id, i <= full);
    }
    static const wchar_t* const kPart[] = { NULL, L"Star.25", L"Star.5", L"Star.75" };
    for (int q = 1; q <= 3; q++)
    {
        HXUIOBJ part = Child(repScene, kPart[q]);
        if (!part)
            continue;
        XuiElementSetShow(part, q == quarter && full < 5);
        if (q == quarter)
        {
            D3DXVECTOR3 at(0, 0, 0);
            XuiElementGetPosition(part, &at);
            at.x = 62.6604f + 31.5556f * full;
            XuiElementSetPosition(part, &at);
        }
    }
}

// The card's size: GamerCard.xur's own frame range for it (the card's
// "Macro".."EndMacro"), and the offline base's single frame of that name.
void LayOutCard(HXUIOBJ obj)
{
    HXUIOBJ card = g_cards.count(obj) ? g_cards[obj] : NULL;
    if (!card)
        return;
    const std::wstring& mode = g_modes[obj];
    HRESULT hr = PlayRange(card, mode.c_str(), (L"End" + mode).c_str(), false);
    HXUIOBJ base = Child(card, L"onlineBaseScene");
    HRESULT hr2 = base ? PlayRange(base, mode.c_str(), mode.c_str(), true) : E_FAIL;
    if (base && g_stars.count(obj))
        ShowStars(base, g_stars[obj]);
    Log("1888 gamer card: %S layout: %08X, online base %08X", mode.c_str(), (unsigned)hr, (unsigned)hr2);
}

// As laid out (the teardown's Macro: baseMetaPane 341 x 130; offline
// gamerTagText 330 wide, credText at (113.7, 58.7) 217 wide).
void LogCard(HXUIOBJ obj)
{
    HXUIOBJ card = g_cards.count(obj) ? g_cards[obj] : NULL;
    HXUIOBJ offline = Child(card, L"onlineBaseScene");
    LogBox(L"control", obj);
    LogBox(L"card", card);
    LogBox(L"baseMetaPane", Child(card, L"baseMetaPane"));
    LogBox(L"onlineBaseScene", offline);
    LogBox(L"gamerTagText", Child(offline, L"gamerTagText"));
    LogBox(L"credText", Child(offline, L"credText"));
}

// The Guide closed (main.cpp, XN_SYS_UI): the cards were filled when they
// were made, before the Guide read the profile and kept its picture,
// gamerscore and zone (SlopGuide FillCard: cache\profiles\<XUID>); a zone
// picked in the Guide never reached them.
void RefreshCards()
{
    int user = SignedInUser();
    XUID xuid = 0;
    if (user < 0 || XUserGetXUID(user, &xuid) != ERROR_SUCCESS || !xuid)
        return;
    int score = -1, played = -1, earned = -1;
    bool numbers = GuideCardNumbers(xuid, score, played, earned) && score >= 0;
    std::wstring picture = ProfilePictureLocator(xuid);
    for (std::map<HXUIOBJ, HXUIOBJ>::iterator it = g_cards.begin(); it != g_cards.end(); ++it)
    {
        HXUIOBJ base = Child(it->second, L"onlineBaseScene");
        if (!base)
            continue;
        SetText(base, L"zoneText", ProfileZoneName(xuid));
        if (numbers)
            SetNumber(base, L"credText", score);
        HXUIOBJ tile = Child(base, L"gamerTileImage");
        if (tile && !picture.empty())
            XuiImageElementSetImagePath(tile, picture.c_str());
        CardStyle(it->second, user, xuid);
    }
    Log("1888 gamer cards refreshed: %u, zone %S, gamerscore %d", (unsigned)g_cards.size(), ProfileZoneName(xuid),
        numbers ? score : -1);
}

HRESULT GamerCardProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        XuiSetTimer(obj, kBuildTimer, 100);     // once the scene around it is complete
        XuiSetTimer(obj, kFocusTimer, 250);
    }
    else if (msg->dwMessage == XM_DESTROY)
    {
        g_cards.erase(obj);
        g_lit.erase(obj);
        g_modes.erase(obj);
        g_stars.erase(obj);
    }
    else if (msg->dwMessage == XM_TIMER)
    {
        unsigned id = ((XUIMessageTimer*)msg->pvData)->nId;
        if (id == kBuildTimer)
        {
            XuiKillTimer(obj, kBuildTimer);
            BuildCard(obj);
        }
        else if (id == kLayoutTimer)
        {
            XuiKillTimer(obj, kLayoutTimer);
            LayOutCard(obj);
            XuiSetTimer(obj, kMeasureTimer, 500);
        }
        else if (id == kMeasureTimer)
        {
            XuiKillTimer(obj, kMeasureTimer);
            HXUIOBJ base = g_cards.count(obj) ? Child(g_cards[obj], L"onlineBaseScene") : NULL;
            if (base && g_stars.count(obj))
                ShowStars(base, g_stars[obj]);      // after the size range has run
            LogCard(obj);
        }
        else if (id == kReadTimer)
        {
            int score, played, earned;
            if (CardReadDone(score, played, earned))
            {
                XuiKillTimer(obj, kReadTimer);
                Log("1888 gamer card: gamerscore %d, %d games, %d achievements", score, played, earned);
                HXUIOBJ base = g_cards.count(obj) ? Child(g_cards[obj], L"onlineBaseScene") : NULL;
                SetNumber(base, L"credText", score < 0 ? 0 : score);
            }
        }
        else if (id == kFocusTimer && g_cards.count(obj))
        {
            // The card's frame is its baseMetaPane button (btn_Gamercard); the
            // control, not the button, takes the focus, so the highlight is
            // played here by where the focus is.
            bool focused = XuiElementTreeHasFocus(obj) != FALSE;
            bool& lit = g_lit[obj];
            HXUIOBJ pane = Child(g_cards[obj], L"baseMetaPane");
            if (pane && focused != lit)
            {
                if (focused)
                    XuiControlPlayVisualRangeEx(pane, L"Focus", L"Focus", L"EndFocus", FALSE);
                else
                    XuiControlPlayVisualRange(pane, L"KillFocus", L"KillFocus", L"EndNormal");
                lit = focused;
            }
        }
    }
    return S_OK;
}

// --- the blades ------------------------------------------------------------------
// dashmain.xur gives Xbox Live, Games and Media an empty 700 x 369 slot
// (scContainer); the dashboard's code put the signed-in or signed-out content
// in it. The System blade is complete in dashmain.xur itself.
struct BladeContent
{
    const wchar_t* name;
    const wchar_t* package;
    const wchar_t* signedIn;
    const wchar_t* signedOut;
};
const BladeContent kGames = { L"Games", L"gamesbla", L"gamesSignedIn.xur", L"gamesSignedOut.xur" };
const BladeContent kMedia = { L"Media", L"mediabla", L"mediaSignedIn.xur", L"mediaSignedOut.xur" };
const BladeContent kLive = { L"Xbox Live", L"live", L"liveSignedInNLUI.xur", L"liveSignedOutUI.xur" };
// The Marketplace blade (6717's dashmain Tab1, DashMPScene),
// its content 6717's (blademp; signed in without Xbox Live: the NL scene, as
// the Xbox Live blade's).
const BladeContent kMarketplace = { L"Marketplace", L"blademp", L"marketplaceSignedInNL.xur",
                                    L"marketplaceSignedOut.xur" };
// With 1888's strip (dashmain5.xzp), the Marketplace as the user's screenshot of
// the 2007 dashboard shows it: tools/marketplace5.py's scene, in that package.
const BladeContent kMarketplaceHub = { L"Marketplace", L"dashmain", L"marketplaceHub.xur", L"marketplaceHub.xur" };

// The ids a content's focus starts on, in order (its DashScene P0 list, as
// stored in 1888's files; the stand-in's own copy isn't relied on).
const wchar_t* const kFirstFocus[] = { L"ctlGamerCard", L"fakeGamerCard", L"navGames", L"navMusic",
                                       L"btnJoinXbox", L"btnTile0", NULL };

class CBlade : public CXuiSceneImpl
{
public:
    XUI_BEGIN_MSG_MAP()
        XUI_ON_XM_INIT(OnInit)
        XUI_ON_XM_TIMER(OnTimer)
        XUI_ON_XM_SET_FOCUS(OnSetFocus)
        XUI_ON_XM_NOTIFY_SET_FOCUS(OnNotifySetFocus)
    XUI_END_MSG_MAP()

    CBlade() : m_cfg(&kGames), m_container(NULL), m_content(NULL), m_who(0) {}

    HRESULT OnInit(XUIMessageInit*, BOOL& bHandled)
    {
        m_container = Child(m_hObj, L"scContainer");
        Log("1888 %S blade: slot %s", m_cfg->name, m_container ? "found" : "MISSING");
        m_who = WhoIsSignedIn();
        AddContent();
        XuiSetTimer(m_hObj, kSigninTimer, 1000);
        bHandled = FALSE;
        return S_OK;
    }

    void AddContent()
    {
        if (m_cfg == &kMarketplace)
        {
            // 1888's strip's package holds the Marketplace as the user wants it.
            std::wstring base;
            const unsigned char* data = NULL;
            unsigned size = 0;
            if (PackageLocator(L"dashmain", base) && LocatorBytes((base + L"marketplaceHub.xur").c_str(), &data, &size))
                m_cfg = &kMarketplaceHub;
        }
        AddScene(m_container ? m_container : m_hObj, m_cfg->package, m_who ? m_cfg->signedIn : m_cfg->signedOut,
                 &m_content);
        if (m_content && !m_who)
        {
            // The signed-out card: how many profiles the console has.
            ScanProfiles();
            int n = (int)Profiles().size();
            wchar_t buf[64];
            _snwprintf_s(buf, 64, _TRUNCATE, n == 1 ? L"%d Profile Found" : L"%d Profiles Found", n);
            launch::SetText(m_content, L"labFoundProfiles", buf);
        }
    }

    // Every second: a sign-in or sign-out (the Guide) swaps the content.
    HRESULT OnTimer(XUIMessageTimer* t, BOOL& bHandled)
    {
        if (t->nId != kSigninTimer)
            return S_OK;
        bHandled = TRUE;
        XUID who = WhoIsSignedIn();
        if (who == m_who)
            return S_OK;
        Log("1888 %S blade: sign-in changed", m_cfg->name);
        m_who = who;
        bool hadFocus = m_content && XuiElementTreeHasFocus(m_content);
        if (m_content)
        {
            if (g_openedFrom == m_content)
                g_openedFrom = NULL;
            g_openedFocus = NULL;
            XuiElementUnlink(m_content);
            XuiDestroyObject(m_content);
            m_content = NULL;
        }
        AddContent();
        if (hadFocus)
        {
            BOOL handled = FALSE;
            FocusFirstItem(handled);
        }
        return S_OK;
    }

    HRESULT OnNotifySetFocus(HXUIOBJ source, XUINotifyFocus*, BOOL& bHandled)
    {
        if (source == m_container || source == m_hObj)
            FocusFirstItem(bHandled);
        return S_OK;
    }

    HRESULT OnSetFocus(HXUIOBJ, BOOL& bHandled)
    {
        FocusFirstItem(bHandled);
        return S_OK;
    }

    void FocusFirstItem(BOOL& bHandled)
    {
        for (const wchar_t* const* id = kFirstFocus; m_content && *id; id++)
        {
            HXUIOBJ target = Child(m_content, *id);
            if (target && SUCCEEDED(XuiElementSetFocus(target)))
            {
                bHandled = TRUE;
                return;
            }
        }
    }

protected:
    const BladeContent* m_cfg;

private:
    HXUIOBJ m_container, m_content;
    XUID m_who;
    static const DWORD kSigninTimer = 0x53494731;   // "SIG1"
};

class CGamesBlade : public CBlade
{
public:
    XUI_IMPLEMENT_CLASS(CGamesBlade, L"CGamesBladeScene", XUI_CLASS_SCENE)
};

class CMediaBlade : public CBlade
{
public:
    XUI_IMPLEMENT_CLASS(CMediaBlade, L"DashMediaScene", XUI_CLASS_SCENE)
    CMediaBlade() { m_cfg = &kMedia; }
};

class CMarketplaceBlade : public CBlade
{
public:
    XUI_IMPLEMENT_CLASS(CMarketplaceBlade, L"DashMPScene", XUI_CLASS_SCENE)
    CMarketplaceBlade() { m_cfg = &kMarketplace; }
};

class CLiveBlade : public CBlade
{
public:
    XUI_IMPLEMENT_CLASS(CLiveBlade, L"DashLiveScene", XUI_CLASS_SCENE)
    CLiveBlade() { m_cfg = &kLive; }
};

// --- the Guide host (xam#hudbkgnd.xur) ---------------------------------------------
// In 1888 xam hosted the Guide and its own apps (sign-in, profiles, friends...)
// in one blade: hudbkgnd.xur's HUDRootScene. The app scene goes in
// AppHostElementId; the blade then plays one of its own transitions (named
// frames, from the teardown): ClosedToHalf / HalfToClosed, ClosedToFull /
// FullToClosed, HalfToFull / FullToHalf, and the Error ones. An app's own
// HUDScene says which (OpenType 1 = half, 2 = full) and its A/B/X/Y legends.
// Everything here draws with 1888's Guide skin (huduiskin), loaded under the
// visual prefix kHudPrefix.
const wchar_t* const kHudPrefix = L"Hud";
const DWORD kHudCloseTimer = 0x48554443;        // "HUDC"
const DWORD kHudCloseMs = 520;                  // the 30-frame close (FullToClosed 194-223) at 60 fps, and a little

HXUIOBJ g_layer = NULL;         // where full-screen scenes and the Guide go (main.cpp)
HXUIOBJ g_hud = NULL;           // HUDRootScene
HXUIOBJ g_hudApp = NULL;        // the hosted app's scene
HXUIOBJ g_hudReturn = NULL;     // the focus before the Guide opened
int g_hudState = 0;             // 0 closed, 1 half, 2 full

bool HudOpen()
{
    return g_hudApp != NULL;
}

// The Guide's clock as xam writes it: XamFormatTimeString (xam export 487) on the
// local time - 1888's Guide (xam 818CFAC0) and 17559's (xam 816932D8) both do.
// 17559's takes (local FILETIME, buffer, length).
typedef void (*FormatTimeFn)(ULONGLONG localTime, WCHAR* buffer, DWORD length);

std::wstring ClockText()
{
    static FormatTimeFn format = NULL;
    if (!format)
    {
        HANDLE xam = NULL;
        PVOID fn = NULL;
        if (XexGetModuleHandle("xam.xex", &xam) >= 0 && XexGetProcedureAddress(xam, 487, &fn) >= 0)
            format = (FormatTimeFn)fn;
    }
    FILETIME now, local;
    GetSystemTimeAsFileTime(&now);
    FileTimeToLocalFileTime(&now, &local);
    WCHAR buf[64] = { 0 };
    if (format)
        format(((ULONGLONG)local.dwHighDateTime << 32) | local.dwLowDateTime, buf, 64);
    return buf;
}

// A Guide legend's text; an empty one draws nothing, not even its button (its
// visual dropped: hudbkgnd's own timelines show every legend as the Guide
// opens, so hiding it didn't stay; the Gamer Profile kept a bare X and Y).
bool SetStringProperty(HXUIOBJ obj, const wchar_t* name, const wchar_t* value);

void SetLegend(const wchar_t* id, const wchar_t* text)
{
    static const wchar_t* const kIds[] = { L"Legend_A", L"Legend_B", L"Legend_X", L"Legend_Y" };
    static const wchar_t* const kVisuals[] = { L"legend_A", L"legend_B", L"legend_X", L"legend_Y" };
    HXUIOBJ legend = Child(g_hud, id);
    if (!legend)
        return;
    for (int i = 0; i < 4; i++)
        if (!wcscmp(id, kIds[i]))
        {
            SetStringProperty(legend, L"Visual", *text ? kVisuals[i] : L"");
            XuiElementSkinChanged(legend);
        }
    XuiControlSetText(legend, text);
}

// The legends' text, from the app's HUDScene (P1..P4 = LegendA, B, X, Y).
void SetLegends(HXUIOBJ app)
{
    static const wchar_t* const kIds[] = { L"Legend_A", L"Legend_B", L"Legend_X", L"Legend_Y" };
    for (int i = 0; i < 4; i++)
    {
        const XUIElementPropVal* v = StandInProperty(app, L"HUDScene", i + 1);
        SetLegend(kIds[i], v && v->type == XUI_EPT_STRING && v->szVal ? v->szVal : L"");
    }
}

// Where 1888's xam put the Guide on screen (its view transform, sub_818D0248 in
// the 1888 xam disassembly): scaled evenly to the screen's height (height /
// 770), moved left 40 pixels at 720 lines on a widescreen display (60 on 4:3),
// not moved down. The Guide sits in the dashboard's layer, which is scaled to
// cover the screen; its own scale and position undo that and apply 1888's.
void PlaceGuide()
{
    // XUI draws at the display's own size (main.cpp: XUI's device).
    XVIDEO_MODE mode;
    ZeroMemory(&mode, sizeof(mode));
    XGetVideoMode(&mode);
    float w = mode.dwDisplayWidth ? (float)mode.dwDisplayWidth : 1280.0f;
    float h = mode.dwDisplayHeight ? (float)mode.dwDisplayHeight : 720.0f;
    D3DXVECTOR3 layerScale(1, 1, 1), layerPos(0, 0, 0);
    XuiElementGetScale(g_layer, &layerScale);
    XuiElementGetPosition(g_layer, &layerPos);
    float s = h / 770.0f;
    float tx = (mode.fIsWideScreen ? -40.0f : -60.0f) * h / 720.0f;
    D3DXVECTOR3 scale(s / layerScale.x, s / layerScale.y, 1);
    D3DXVECTOR3 pos((tx - layerPos.x) / layerScale.x, (0 - layerPos.y) / layerScale.y, 0);
    XuiElementSetScale(g_hud, &scale);
    XuiElementSetPosition(g_hud, &pos);
    Log("1888 guide: placed as 1888 did: scale %.3f, x %.0f px (%ux%u%s)", s, tx, (unsigned)w, (unsigned)h,
        mode.fIsWideScreen ? ", widescreen" : "");
    (void)w;
}

// Opens `scene` of `package` in the Guide. Returns the app scene, or NULL.
HXUIOBJ HudShow(const wchar_t* package, const wchar_t* scene)
{
    // (one still sliding away goes now: RetroAchievements' password keyboard
    // follows Done on the username's at once, and was refused)
    if (g_hudApp && !g_hudState && g_hud)
    {
        XuiKillTimer(g_hud, kHudCloseTimer);
        XuiElementUnlink(g_hudApp);
        XuiDestroyObject(g_hudApp);
        g_hudApp = NULL;
    }
    if (g_hudApp || !g_layer)
        return NULL;
    if (!g_hud)
    {
        HXUIOBJ hud = NULL;
        if (FAILED(AddScene(g_layer, L"xam", L"hudbkgnd.xur", &hud)) || !hud)
            return NULL;
        g_hud = hud;
        XuiElementSetVisualPrefix(g_hud, kHudPrefix);
        XuiElementSkinChanged(g_hud);
        PlaceGuide();
    }
    HXUIOBJ host = Child(g_hud, L"AppHostElementId");
    HXUIOBJ app = NULL;
    if (!host || FAILED(AddScene(host, package, scene, &app)) || !app)
        return NULL;
    XuiElementSkinChanged(app);
    g_hudApp = app;
    g_hudReturn = XuiElementGetFocus();
    const XUIElementPropVal* type = StandInProperty(app, L"HUDScene", 0);
    g_hudState = type && type->type == XUI_EPT_UNSIGNED && type->uVal == 2 ? 2 : 1;
    SetLegends(app);
    SetText(g_hud, L"DateTimeTextId", ClockText().c_str());
    int user = SignedInUser();
    char name[XUSER_NAME_SIZE + 1] = { 0 };
    std::wstring tag;
    if (user >= 0 && XUserGetName(user, name, sizeof(name)) == ERROR_SUCCESS)
        for (const char* c = name; *c; c++)
            tag += (wchar_t)(unsigned char)*c;
    SetText(g_hud, L"GamerTag", tag.c_str());
    XuiElementSetShow(g_hud, TRUE);
    HRESULT hr = g_hudState == 2 ? PlayRange(g_hud, L"ClosedToFull", L"EndClosedToFull", false)
                                 : PlayRange(g_hud, L"ClosedToHalf", L"EndClosedToHalf", false);
    Log("1888 guide: %S#%S, %s: %08X", package, scene, g_hudState == 2 ? "full" : "half", (unsigned)hr);
    XuiElementSetFocus(app);
    return app;
}

DWORD g_hudClosedAt = 0;                        // when the Guide last closed (B's fallback leaves that B alone)

void HudClose()
{
    if (!g_hudApp)
        return;
    g_hudClosedAt = GetTickCount();
    HRESULT hr = g_hudState == 2 ? PlayRange(g_hud, L"FullToClosed", L"EndFullToClosed", false)
                                 : PlayRange(g_hud, L"HalfToClosed", L"EndHalfToClosed", false);
    Log("1888 guide: closing: %08X", (unsigned)hr);
    g_hudState = 0;
    if (g_hudReturn)
        XuiElementSetFocus(g_hudReturn);
    XuiSetTimer(g_hud, kHudCloseTimer, kHudCloseMs);
}

// HUDRootScene's class (HUDBkgndScene): B closes the hosted app (when the app
// itself doesn't take it), and the app goes once the blade has slid away.
class CHudBkgnd : public CXuiSceneImpl
{
public:
    XUI_IMPLEMENT_CLASS(CHudBkgnd, L"HUDBkgndScene", XUI_CLASS_SCENE)
    XUI_BEGIN_MSG_MAP()
        XUI_ON_XM_KEYDOWN(OnKeyDown)
        XUI_ON_XM_TIMER(OnTimer)
    XUI_END_MSG_MAP()

    HRESULT OnKeyDown(XUIMessageInput* in, BOOL& bHandled)
    {
        if (g_hudApp && g_hudState && in->dwKeyCode == VK_PAD_B)
        {
            HudClose();
            bHandled = TRUE;
        }
        return S_OK;
    }

    HRESULT OnTimer(XUIMessageTimer* t, BOOL& bHandled)
    {
        if (t->nId != kHudCloseTimer)
            return S_OK;
        XuiKillTimer(m_hObj, kHudCloseTimer);
        if (g_hudApp && !g_hudState)
        {
            XuiElementUnlink(g_hudApp);
            XuiDestroyObject(g_hudApp);
            g_hudApp = NULL;
        }
        bHandled = TRUE;
        return S_OK;
    }
};

// --- Sign In (signin.xex) -------------------------------------------------------------
// signin1.xur (one controller: OpenType 1, half the Guide, legends Select /
// Back) with its pane Scene_Pane0 holding UserList.xur: the list of the
// console's profiles (row visual btn_SignIn: line 1 the gamertag, line 3 the
// device, the gamer picture in image slot 3).
std::vector<ProfileEntry> g_signinList;

void FillSigninList()
{
    ScanProfiles();
    g_signinList.clear();
    const std::vector<ProfileEntry>& all = Profiles();
    for (size_t i = 0; i < all.size(); i++)
        if (all[i].gamertag != L"ABadAvatar")    // the exploit profile stays hidden (SlopDash-wide)
            g_signinList.push_back(all[i]);
}

std::vector<std::wstring> g_signinPics;

const wchar_t* SigninLine(const ProfileEntry& pe, int line)
{
    return line == 0 ? pe.gamertag.c_str()
         : line == 1 ? (pe.live ? L"Xbox Live" : L"")
         : line == 2 ? (pe.onUsb ? L"MU" : L"HDD") : L"";
}

const StandInInfo kUserList = { L"UserList", L"XuiList", 0, { 0 } };

HRESULT UserListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = (int)g_signinList.size();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData && d->iItem >= 0 && d->iItem < (int)g_signinList.size())
        {
            // signin.xex (90102140): line 2 is "Xbox Live" (its string 15) for a
            // profile with Xbox Live (901021BC; SlopDash knows it from the
            // system while the profile is signed in, and keeps it). Line 3 is
            // where it is: "HDD" (14), or "MU" (35, for USB).
            const ProfileEntry& pe = g_signinList[d->iItem];
            d->szText = SigninLine(pe, d->iData);
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = NULL;
            if (g_signinPics.size() != g_signinList.size())
            {
                g_signinPics.clear();
                for (size_t i = 0; i < g_signinList.size(); i++)
                {
                    std::wstring pic = ProfilePictureLocator(g_signinList[i].xuid);
                    g_signinPics.push_back(pic.empty() ? L"sharedres://ico_64x_DefaultPicture.png" : pic);
                }
            }
            if (d->iData == 3 && d->iItem >= 0 && d->iItem < (int)g_signinPics.size())
                d->szPath = g_signinPics[d->iItem].c_str();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
    {
        int i = XuiListGetCurSel(obj, NULL);
        if (i >= 0 && i < (int)g_signinList.size())
        {
            int slot = SignedInUser();
            bool started = StartProfileSwitch(slot < 0 ? 0 : slot, g_signinList[i].xuid);
            Log("1888 sign in: %S: %s", g_signinList[i].gamertag.c_str(), started ? "signing in" : "FAILED");
            msg->bHandled = TRUE;
            // Straight back to the dashboard: adding signin.xex's status.xur
            // here, inside the list's own press, froze SlopDash (0.0.96). The
            // console's Guide (SystemGuideSignIn) shows 1888's status instead.
            HudClose();
        }
    }
    return S_OK;
}

// With SlopGuide's 1888 Guide running, Sign In is the console's own Guide
// (one Guide: the Guide button closes it), its sign-in scenes 1888's
// (plugin/SlopGuide.cpp). Without it, SlopDash's own copy below.
bool SystemGuideSignIn()
{
    HANDLE plugin = NULL;
    if (XexGetModuleHandle("SlopGuide.xex", &plugin) < 0 || !plugin ||
        GetFileAttributesA("game:\\Guide\\blades1888.xzp") == (DWORD)-1)
        return false;
    DWORD r = XShowSigninUI(1, 0);
    Log("1888 sign in: the console's Guide: %u", (unsigned)r);
    // With SlopGuide loaded, never SlopDash's own copy as well: refused
    // (the console's Guide already open), it opened behind the console's one,
    // two Guides on screen (0.0.111).
    (void)r;
    return true;
}

void OpenSignIn()
{
    if (SystemGuideSignIn())
        return;
    FillSigninList();
    g_signinPics.clear();
    HXUIOBJ app = HudShow(L"signin", L"signin1.xur");
    // signin.xex's own legends besides Select / Back: Y "Create New Profile"
    // and X "Recover Gamertag" (its strings 5 and 40).
    SetLegend(L"Legend_Y", L"Create New Profile");
    SetLegend(L"Legend_X", L"Recover Gamertag");
    HXUIOBJ pane = Child(app, L"Scene_Pane0");
    HXUIOBJ users = NULL;
    if (pane && SUCCEEDED(AddScene(pane, L"signin", L"UserList.xur", &users)) && users)
    {
        XuiElementSkinChanged(users);
        HXUIOBJ list = Child(users, L"listUser");
        if (list)
            XuiElementSetFocus(list);
    }
    Log("1888 sign in: %u profiles", (unsigned)g_signinList.size());
}

// --- Gamer Profile (gamerprofile.xex) -------------------------------------------------
// A on your gamer card (any blade): 1888's own Gamer Profile, as its Guide
// showed it (gamerprofile#GamerCardScene.xur, class GamerCardUIScene, which
// 1888's xam filled in): the card at its full size and the offline profile's
// buttons (SlopDash runs offline). Not the console's XShowGamerCardUI: that's
// 17559's, mangled in the 1888 Guide blade.
void OpenGamerProfile()
{
    HXUIOBJ app = HudShow(L"gamerprofile", L"GamerCardScene.xur");
    Log("1888 gamer profile: %s", app ? "shown" : "FAILED");
}

// --- The keyboard (vk.xex) ----------------------------------------------------------
// 1888's own keyboard (vk#Keyboard.xur, class CKeyboardScene) in its Guide, for
// SlopDash's text entry (Rename, RetroAchievements, Add Repo). Not the
// console's XShowKeyboardUI: that's 17559's, dropped into the 1888 Guide blade,
// its text tiny and its right column outside the blade.
// 1888's code for it isn't here; its key table is (1888 vk.xex at 917D1A38:
// each key's text on its three pages, Alphabet, Accents and Symbols; LT / RT
// step through them, labelled with the page they go to), and the keys do what
// their names say: Caps (the alphabet's capitals), Space (Y), Backspace (X),
// the cursor (LB / RB), Done (START).
struct VkKey
{
    const wchar_t* id;
    const wchar_t* page[3];
};
const VkKey kVkKeys[] = {
    { L"Key.A", { L"a", L"\x00e0", L"," } }, { L"Key.B", { L"b", L"\x00e1", L";" } },
    { L"Key.C", { L"c", L"\x00e2", L":" } }, { L"Key.D", { L"d", L"\x00e3", L"'" } },
    { L"Key.E", { L"e", L"\x00e4", L"\"" } }, { L"Key.F", { L"f", L"\x00e5", L"!" } },
    { L"Key.G", { L"g", L"\x00f1", L"?" } }, { L"Key.H", { L"h", L"\x00e8", L"[" } },
    { L"Key.I", { L"i", L"\x00e9", L"]" } }, { L"Key.J", { L"j", L"\x00ea", L"{" } },
    { L"Key.K", { L"k", L"\x00eb", L"}" } }, { L"Key.L", { L"l", L"\x00fe", L"`" } },
    { L"Key.M", { L"m", L"\x00e7", L"$" } }, { L"Key.N", { L"n", L"\x00fd", L"\x00a3" } },
    { L"Key.O", { L"o", L"\x00ec", L"<" } }, { L"Key.P", { L"p", L"\x00ed", L">" } },
    { L"Key.Q", { L"q", L"\x00ee", L"(" } }, { L"Key.R", { L"r", L"\x00ef", L")" } },
    { L"Key.S", { L"s", L"\x00f9", L"\x20ac" } }, { L"Key.T", { L"t", L"\x00fa", L"\x00a5" } },
    { L"Key.U", { L"u", L"\x00fb", L"\x00a4" } }, { L"Key.V", { L"v", L"\x00f2", L"|" } },
    { L"Key.W", { L"w", L"\x00f3", L"=" } }, { L"Key.X", { L"x", L"\x00f4", L"*" } },
    { L"Key.Y", { L"y", L"\x00f5", L"/" } }, { L"Key.Z", { L"z", L"\x00f0", L"+" } },
    { L"Key.@", { L"@", L"\x00f8", L"@" } }, { L"Key.0", { L"0", L"\x00d7", L"&" } },
    { L"Key.1", { L"1", L"\x0153", L"\x00a1" } }, { L"Key.2", { L"2", L"\x00e6", L"\x00bf" } },
    { L"Key.3", { L"3", L"\x00df", L"%" } }, { L"Key.4", { L"4", L"\x00ff", L"\x00ab" } },
    { L"Key.5", { L"5", L"\x00ba", L"\x00bb" } }, { L"Key.6", { L"6", L"\x00aa", L"#" } },
    { L"Key.7", { L"7", L"\x00fc", L"~" } }, { L"Key.8", { L"8", L"\x00b5", L"^" } },
    { L"Key.9", { L"9", L"\x0161", L"\\" } }, { L"Key._", { L"_", L"_", L"_" } },
    { L"Key.-", { L"-", L"\x00f6", L"-" } }, { L"Key..", { L".", L".", L"." } },
};
const wchar_t* const kVkPages[] = { L"Alphabet", L"Accents", L"Symbols" };     // its strings: IDS_ALPHABET...

struct Keyboard
{
    bool open;
    std::wstring text;
    size_t caret;
    int page;
    bool caps;
    bool password;
    LPWSTR result;
    DWORD max;
    PXOVERLAPPED ov;
    std::wstring title, description;
    HXUIOBJ scene;                          // its scene in the Guide
};
Keyboard g_vk = { false };
const DWORD kVkTimer = 0x564B4231;              // "VKB1": once its parts exist

void VkShowText(HXUIOBJ scene)
{
    HXUIOBJ edit = Child(scene, L"XuiEdit1");
    if (!edit)
        return;
    std::wstring shown = g_vk.password ? std::wstring(g_vk.text.size(), L'*') : g_vk.text;
    XuiControlSetText(edit, shown.c_str());
    XuiEditSetCaretPosition(edit, (int)g_vk.caret);
}

void VkShowKeys(HXUIOBJ scene)
{
    for (size_t i = 0; i < ARRAYSIZE(kVkKeys); i++)
        if (HXUIOBJ key = Child(scene, kVkKeys[i].id))
        {
            std::wstring label = kVkKeys[i].page[g_vk.page];
            if (g_vk.caps && g_vk.page == 0)
                label[0] = towupper(label[0]);
            XuiControlSetText(key, label.c_str());
        }
    // (LT / RT say the page they go to, as 1888's table has them)
    SetText(scene, L"Key.Prev", kVkPages[(g_vk.page + 2) % 3]);
    SetText(scene, L"Key.Next", kVkPages[(g_vk.page + 1) % 3]);
}

void VkFinish(bool done, bool close)
{
    if (!g_vk.open)
        return;
    g_vk.open = false;
    if (done && g_vk.result && g_vk.max)
        wcsncpy_s(g_vk.result, g_vk.max, g_vk.text.c_str(), _TRUNCATE);
    SecureZeroMemory(&g_vk.text[0], g_vk.text.size() * sizeof(wchar_t));
    g_vk.text.clear();
    DWORD r = done ? ERROR_SUCCESS : ERROR_CANCELLED;
    g_vk.ov->dwExtendedError = r;
    g_vk.ov->InternalHigh = 0;
    g_vk.ov->InternalLow = r;               // (last: the asker polls it)
    Log("1888 keyboard: %s", done ? "done" : "cancelled");
    if (close)
        HudClose();
}

void VkType(HXUIOBJ scene, const std::wstring& s)
{
    if (g_vk.text.size() + s.size() + 1 > g_vk.max)
        return;
    g_vk.text.insert(g_vk.caret, s);
    g_vk.caret += s.size();
    VkShowText(scene);
}

// What a key of the keyboard does: by its name.
void VkKeyPressed(HXUIOBJ scene, const std::wstring& id)
{
    if (id == L"Key.OK")
        VkFinish(true, true);
    else if (id == L"Key.BS")
    {
        if (g_vk.caret)
        {
            g_vk.text.erase(--g_vk.caret, 1);
            VkShowText(scene);
        }
    }
    else if (id == L"Key.Spc")
        VkType(scene, L" ");
    else if (id == L"Key.Left" || id == L"Key.Right")
    {
        if (id == L"Key.Left" && g_vk.caret)
            g_vk.caret--;
        else if (id == L"Key.Right" && g_vk.caret < g_vk.text.size())
            g_vk.caret++;
        VkShowText(scene);
    }
    else if (id == L"Key.Prev" || id == L"Key.Next")
    {
        g_vk.page = (g_vk.page + (id == L"Key.Next" ? 1 : 2)) % 3;
        VkShowKeys(scene);
    }
    else if (id == L"Key.Caps")
    {
        g_vk.caps = !g_vk.caps;
        VkShowKeys(scene);
    }
    else
        for (size_t i = 0; i < ARRAYSIZE(kVkKeys); i++)
            if (id == kVkKeys[i].id)
            {
                std::wstring c = kVkKeys[i].page[g_vk.page];
                if (g_vk.caps && g_vk.page == 0)
                    c[0] = towupper(c[0]);
                VkType(scene, c);
            }
}

HRESULT KeyboardProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kVkTimer, 20);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kVkTimer)
    {
        XuiKillTimer(obj, kVkTimer);
        SetText(obj, L"Text_Header", g_vk.title.c_str());
        SetText(obj, L"XuiText1", g_vk.description.c_str());
        SetText(obj, L"XuiText2", L"");
        VkShowKeys(obj);
        VkShowText(obj);
        if (HXUIOBJ first = Child(obj, L"Key.A"))
            XuiElementSetFocus(first);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
    {
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        if (id.compare(0, 4, L"Key.") == 0)
        {
            VkKeyPressed(obj, id);
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_KEYDOWN)
    {
        // The pad's own keys for the keyboard's, as its buttons show them.
        static const struct { DWORD vk; const wchar_t* key; } kPad[] = {
            { VK_PAD_X, L"Key.BS" }, { VK_PAD_Y, L"Key.Spc" }, { VK_PAD_LSHOULDER, L"Key.Left" },
            { VK_PAD_RSHOULDER, L"Key.Right" }, { VK_PAD_LTRIGGER, L"Key.Prev" }, { VK_PAD_RTRIGGER, L"Key.Next" },
            { VK_PAD_START, L"Key.OK" }, { VK_PAD_LTHUMB_PRESS, L"Key.Caps" } };
        DWORD vk = ((XUIMessageInput*)msg->pvData)->dwKeyCode;
        // A on a character key: its character (they have no press key of
        // their own; 1888's keyboard code took A for them).
        std::wstring focus = ElementId(XuiElementGetFocus());
        if (vk == VK_PAD_A && focus.compare(0, 4, L"Key.") == 0)
        {
            VkKeyPressed(obj, focus);
            msg->bHandled = TRUE;
        }
        if (vk == VK_PAD_B)
        {
            VkFinish(false, true);
            msg->bHandled = TRUE;
        }
        for (size_t i = 0; i < ARRAYSIZE(kPad); i++)
            if (vk == kPad[i].vk)
            {
                VkKeyPressed(obj, kPad[i].key);
                msg->bHandled = TRUE;
            }
    }
    else if (msg->dwMessage == XM_DESTROY && obj == g_vk.scene)
    {
        // (gone without Done or Back; an earlier keyboard's scene going as the
        // next opens - the password after the username - isn't this one)
        g_vk.scene = NULL;
        VkFinish(false, false);
    }
    return S_OK;
}

DWORD ShowKeyboard1888(DWORD, DWORD flags, LPCWSTR defaultText, LPCWSTR title, LPCWSTR description, LPWSTR result,
                       DWORD maxChars, PXOVERLAPPED overlapped)
{
    if (g_vk.open || !overlapped || !result || !maxChars)
        return ERROR_INVALID_PARAMETER;
    g_vk.text = defaultText ? defaultText : L"";
    if (g_vk.text.size() >= maxChars)
        g_vk.text.resize(maxChars - 1);
    g_vk.caret = g_vk.text.size();
    g_vk.page = 0;
    g_vk.caps = false;
    g_vk.password = (flags & VKBD_LATIN_PASSWORD) == VKBD_LATIN_PASSWORD;
    g_vk.result = result;
    g_vk.max = maxChars;
    g_vk.ov = overlapped;
    g_vk.title = title ? title : L"";
    g_vk.description = description ? description : L"";
    overlapped->InternalLow = ERROR_IO_PENDING;
    g_vk.open = true;
    g_vk.scene = NULL;
    HXUIOBJ app = HudShow(L"vk", L"Keyboard.xur");
    g_vk.scene = app;
    Log("1888 keyboard: %s", app ? "shown" : "FAILED");
    if (!app)
    {
        g_vk.open = false;
        overlapped->InternalLow = ERROR_CANCELLED;
        return ERROR_ACCESS_DENIED;
    }
    SetLegend(L"Legend_A", L"Select");
    SetLegend(L"Legend_B", L"Back");
    return ERROR_IO_PENDING;
}

// --- DashMainScene: the blades' own navigation -------------------------------------
// dashmain.xur's RootScene. A button with a PressPath opens its screen over
// the blades (the screens are 1120 x 770, laid out for the canvas), and the
// tab scene plays dashmain's own frames for it: "<n>Open" (second level,
// _2ndLevel_Sounds), "<n>OpenFull" from there to a third level
// (_3rdLevel_Sounds), and "<n>CloseFull" / "<n>Close" on the way back; n is the
// blade (6717's dashmain: Tab1 Marketplace, Tab2 Xbox Live
// .. Tab5 System; its frames are named the same way).
const DWORD kLevelTimer = 0x4C564C31;           // "LVL1"
const DWORD kBootTimer = 0x424F4F31;            // "BOO1"
const DWORD kOpenGameTimer = 0x4F475431;        // "OGT1": the Xbox theme's game screen, once the library's ready
const DWORD kRefocusTimer = 0x52464F43;         // "RFOC"
HXUIOBJ g_root = NULL;          // dashmain.xur's RootScene

// A screen opened from the blades shows once they've opened round it. It
// was drawn at once, at its full size, while the blades still met in the
// middle (the level timer plays <n>Open up to 100 ms later, after the
// screen's load: 0.9 s for RetroAchievements), so it lay across them and
// then the blades slid apart under it - the user's "glitch on the panels",
// 2026-11-03 (and the blade mask rule: nothing over a blade's edge). Now its
// contents are clear (opacity 0) until the blades' Open has played, then fade
// in. The contents, not the screen: XUI's own transition into the screen sets
// the screen's show and opacity as it ends (at +492 ms, before Open's 571), and
// a screen hidden or cleared itself came back early (2026-11-03's log).
const DWORD kRevealTimer = 0x5256454C;          // "RVEL"
const DWORD kRevealFade = 120, kRevealLongest = 1500;    // ms: the fade; shown by then whatever happens
HXUIOBJ g_reveal = NULL;        // the screen waiting to show
struct RevealKid
{
    HXUIOBJ obj;
    float own;                  // its own opacity (what it shows at once revealed)
    float set;                  // what the reveal last gave it
};
std::vector<RevealKid> g_revealKids;    // the screen's children
DWORD g_revealOpened = 0;       // when it opened
DWORD g_revealFrom = 0;         // when the blades' Open will have finished (0: not started yet)

// The screen's children at a (0..1) of their own opacity. One whose opacity
// other code changed meanwhile keeps that as its own (the Game Library's
// views clear its stock list as they build: the fade put it back, under
// Launch, 2026-11-03).
void RevealAt(float a)
{
    // (children added since - a library view's own elements - joined, at their own opacity)
    HXUIOBJ c = NULL;
    for (XuiElementGetFirstChild(g_reveal, &c); g_reveal && c; XuiElementGetNext(c, &c))
    {
        bool known = false;
        for (size_t i = 0; i < g_revealKids.size() && !known; i++)
            known = g_revealKids[i].obj == c;
        if (!known)
        {
            RevealKid k = { c, 1.0f, 1.0f };
            XuiElementGetOpacity(c, &k.own);
            k.set = k.own;
            g_revealKids.push_back(k);
        }
    }
    for (size_t i = 0; i < g_revealKids.size(); i++)
    {
        RevealKid& k = g_revealKids[i];
        float now = k.set;
        XuiElementGetOpacity(k.obj, &now);
        if (now - k.set > 0.001f || k.set - now > 0.001f)
            k.own = now;
        k.set = k.own * a;
        XuiElementSetOpacity(k.obj, k.set);
    }
}

void HideUntilBladesOpen(HXUIOBJ scene)
{
    if (!scene || !g_root)
        return;
    if (g_reveal)
        RevealAt(1.0f);                 // (one still waiting: shown as it was)
    g_revealKids.clear();
    g_reveal = scene;
    RevealAt(0.0f);
    g_revealOpened = GetTickCount();
    g_revealFrom = 0;
    XuiSetTimer(g_root, kRevealTimer, 16);
}

// A press on a blade's button: its PressPath's screen opens over the blades
// (beside the main scene, in its layer), as 1888's own code did; the main
// scene's level timer then plays the blade's "<n>Open". False if the button
// has no press path.
void HideBlade(HXUIOBJ content, HXUIOBJ focus);

bool OpenScreen(HXUIOBJ source, HXUIOBJ content = NULL)
{
    if (HudOpen() || !g_root || !g_layer)
        return false;
    DWORD id = 0;
    XUIElementPropVal v;
    std::wstring path;
    if (SUCCEEDED(XuiObjectGetPropertyId(source, L"PressPath", &id)) &&
        SUCCEEDED(XuiObjectGetProperty(source, id, 0, &v)) && v.type == XUI_EPT_STRING && v.szVal)
        path = v.szVal;
    if (path.empty())
        return false;
    std::wstring base;
    PackageLocator(L"dashmain", base);      // the resolver finds the scene in whichever package has it
    HXUIOBJ scene = NULL;
    HXUIOBJ focus = XuiElementGetFocus();
    HRESULT hr = XuiSceneCreate(base.c_str(), path.c_str(), NULL, &scene);
    if (SUCCEEDED(hr))
        hr = XuiSceneNavigateForward(g_root, TRUE, scene, XUSER_INDEX_FOCUS);
    Log("1888 blades: open %S: %08X", path.c_str(), (unsigned)hr);
    // (not held back till the blades open, as the shared screens are: 1888's
    // own scenes, the Game Library among them, change their children as they
    // build, and the fade fought that - its stock list showed under Launch, and
    // the blades opened 4 s late, 2026-11-03)
    if (SUCCEEDED(hr) && content)
        HideBlade(content, focus);
    return SUCCEEDED(hr);
}

// The blade's own buttons give way to a screen opened from them, while it's open.
void HideBlade(HXUIOBJ content, HXUIOBJ focus)
{
    {
        g_openedFocus = focus;
        XuiElementSetShow(content, FALSE);
        g_openedFrom = content;
        // ... and so do its header and A/B/X/Y legends, which sit beside the
        // content in the blade (scBlade: txt_Header / labHeader, legend_*).
        g_bladeChrome.clear();
        HXUIOBJ blade = content;
        for (HXUIOBJ up = NULL; blade && SUCCEEDED(XuiElementGetParent(blade, &up)) && up;)
        {
            if (ElementId(up).compare(0, 3, L"Tab") == 0)
                break;
            blade = up;
        }
        for (HXUIOBJ c = NULL; blade && SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(blade, &c)) && c;)
        {
            std::wstring cid = ElementId(c);
            if ((cid == L"txt_Header" || cid == L"labHeader" || !cid.compare(0, 7, L"legend_")) && XuiElementIsShown(c))
            {
                XuiElementSetShow(c, FALSE);
                g_bladeChrome.push_back(c);
            }
        }
    }
}

// --- legends -----------------------------------------------------------------------
// The A/B/X/Y legends at the foot of the blades and of every screen opened
// from them. 1888's scenes carry all four, the unused ones without text
// (legend_y, navX, btnY...), and its code hid those; a blade's X, Sign Out,
// was there only for someone signed in. Here, every 300 ms (the level timer):
// a legend without text, or Sign Out with nobody signed in, is hidden; one
// hidden here shows again once it has something to say. Legends other code
// hides (a blade's, under a screen opened from it) are left alone.
bool IsLegendId(const std::wstring& raw)
{
    // (legend_a, Legend_A, "Legend X", navA, btnA...)
    std::wstring id = raw;
    for (size_t i = 0; i < id.size(); i++)
        id[i] = (wchar_t)towlower(id[i]);
    static const wchar_t* const kPrefixes[] = { L"legend_", L"legend ", L"nav", L"btn" };
    for (int i = 0; i < 4; i++)
    {
        size_t n = wcslen(kPrefixes[i]);
        if (id.size() == n + 1 && !id.compare(0, n, kPrefixes[i]) && wcschr(L"abxy", id[n]))
            return true;
    }
    return false;
}

void CollectLegends(HXUIOBJ obj, int depth, std::vector<HXUIOBJ>& out)
{
    for (HXUIOBJ c = NULL; SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(obj, &c)) && c;)
    {
        D3DXVECTOR3 at(0, 0, 0);
        if (IsLegendId(ElementId(c)))
        {
            // (the legend row: 1888's legends sit at y 611 and 639)
            if (SUCCEEDED(XuiElementGetPosition(c, &at)) && at.y >= 560.0f)
                out.push_back(c);
        }
        else if (depth > 0)
            CollectLegends(c, depth - 1, out);
    }
}

HXUIOBJ g_legendTop = NULL;
std::vector<HXUIOBJ> g_legends;
std::vector<HXUIOBJ> g_legendsHidden;       // the ones hidden here

void TidyLegends(HXUIOBJ top)
{
    if (top != g_legendTop)
    {
        g_legendTop = top;
        g_legends.clear();
        // (the ones hidden here stay on record: the blades' Sign Out, hidden
        // while nobody was signed in, comes back after a screen in between)
        if (top)
            CollectLegends(top, 2, g_legends);
    }
    bool someone = SignedInUser() >= 0;
    for (size_t i = 0; i < g_legends.size(); i++)
    {
        HXUIOBJ l = g_legends[i];
        LPCWSTR text = XuiControlGetText(l);
        bool says = false;
        for (LPCWSTR t = text; t && *t && !says; t++)
            says = !iswspace(*t);
        bool wanted = says && (someone || !text || wcscmp(text, L"Sign Out"));
        std::vector<HXUIOBJ>::iterator mine = std::find(g_legendsHidden.begin(), g_legendsHidden.end(), l);
        if (!wanted && XuiElementIsShown(l))
        {
            XuiElementSetShow(l, FALSE);
            if (mine == g_legendsHidden.end())
                g_legendsHidden.push_back(l);
        }
        else if (wanted && mine != g_legendsHidden.end())
        {
            XuiElementSetShow(l, TRUE);
            g_legendsHidden.erase(mine);
        }
    }
}

// The blade content a button is in: up to the blade's slot (scContainer) or
// its tab (the System blade's content sits in its tab directly).
HXUIOBJ ContentOf(HXUIOBJ from)
{
    for (HXUIOBJ o = from; o;)
    {
        HXUIOBJ up = NULL;
        if (FAILED(XuiElementGetParent(o, &up)) || !up)
            return NULL;
        std::wstring id = ElementId(up);
        if (id == L"scContainer" || id.compare(0, 3, L"Tab") == 0)
            return o;
        o = up;
    }
    return NULL;
}

bool OnBlades(HXUIOBJ obj)
{
    for (HXUIOBJ o = obj; o; XuiElementGetParent(o, &o))
        if (o == g_root)
            return true;
    return false;
}

// A screen the shared code (Themes, the Games Library's settings...) opens:
// from the blades it goes over them, as OpenScreen's do; from another screen
// the shared code places it.
HRESULT OpenFromBlades(HXUIOBJ from, HXUIOBJ scene)
{
    if (!g_root || !OnBlades(from))
        return E_NOTIMPL;
    if (!scene)
        return S_OK;                // (only asked whether it would)
    HXUIOBJ focus = XuiElementGetFocus();
    HRESULT hr = XuiSceneNavigateForward(g_root, TRUE, scene, XUSER_INDEX_FOCUS);
    if (SUCCEEDED(hr))
        HideUntilBladesOpen(scene);
    HXUIOBJ content = ContentOf(from);
    if (SUCCEEDED(hr) && content)
        HideBlade(content, focus);
    return hr;
}

// B where nothing took it, in a screen opened over the blades (some 1888
// screens' code handled B itself; Console Settings' Display list...): back.
void BackFallback()
{
    // (a B that closed the Guide - the keyboard over Add Repo - put the focus
    // back in the screen under it, which then went back too)
    if (GetTickCount() - g_hudClosedAt < 500)
        return;
    HXUIOBJ top = NULL;
    for (HXUIOBJ o = XuiElementGetFocus(); o; XuiElementGetParent(o, &o))
    {
        HXUIOBJ parent = NULL;
        XuiElementGetParent(o, &parent);
        if (parent == g_layer)
        {
            top = o;
            break;
        }
    }
    if (!top || top == g_root || top == g_hud)
        return;
    HXUIOBJ back = XuiSceneGetBackScene(top);
    if (back)
    {
        HRESULT hr = XuiSceneNavigateBack(top, back, XUSER_INDEX_FOCUS);
        Log("1888 B: back from %S: %08X", ElementId(top).c_str(), (unsigned)hr);
    }
}

// The blades' contents' own classes (1888's code): the scene a blade's
// buttons sit in. XuiScene would navigate a press itself, inside the blade's
// slot; these open it over the blades instead. A on the "Sign In" card (a
// nav button that goes nowhere, so it sends no press) opens Sign In.
const StandInInfo kDashScene = { L"DashScene", L"XuiScene", 3, { 5, 5, 5 } };
const StandInInfo kContentClasses[] = {
    { L"CGamesBladeSignedIn", L"DashScene", 0, { 0 } },
    { L"CGamesBladeSignedOut", L"DashScene", 0, { 0 } },
    { L"MediaBladeSignedInScene", L"DashScene", 0, { 0 } },
    { L"MediaBladeSignedOutScene", L"DashScene", 0, { 0 } },
    { L"DashSystemScene", L"DashScene", 0, { 0 } },
    { L"DashLiveSignedIn", L"XuiScene", 0, { 0 } },
    { L"DashLiveSignedOut", L"XuiScene", 0, { 0 } },
};


bool SetStringProperty(HXUIOBJ obj, const wchar_t* name, const wchar_t* value)
{
    DWORD id = 0;
    if (FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetString(&v, value);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

// --- DashScene: the side panel ----------------------------------------------------
// 1888's DashScene keeps three lists ("\0"-separated, the two characters): P0
// its buttons' ids, P1 each one's description, P2 each one's panel scene (e.g.
// gamesMetaMOTD.xur). With a button focused, its panel scene - or else its
// description - shows in the scene's metaPanelScene. A class can put its own
// line first (the Console Settings' "Current Setting").
std::vector<std::wstring> SplitList(const std::wstring& s)
{
    std::vector<std::wstring> out;
    size_t start = 0;
    for (;;)
    {
        size_t at = s.find(L"\\0", start);
        out.push_back(s.substr(start, at == std::wstring::npos ? std::wstring::npos : at - start));
        if (at == std::wstring::npos)
            return out;
        start = at + 2;
    }
}

std::wstring OwnString(HXUIOBJ obj, const wchar_t* cls, int index)
{
    const XUIElementPropVal* v = StandInProperty(obj, cls, index);
    return v && v->type == XUI_EPT_STRING && v->szVal ? v->szVal : L"";
}

// The panel's description box (its visual's Pane_txt) as wide as the panel:
// the blades' visual (metaScene_1line_GamesMid) is drawn 420 wide with the box
// anchored to its sides, and the blades' panels are 345, so the box shrank to
// 270 and the text wrapped well short of the panel's edge. The visual's own 10
// either side.
void WidenPaneText(HXUIOBJ panel)
{
    HXUIOBJ box = Find(panel, L"Pane_txt");
    float pw = 0, ph = 0, w = 0, h = 0;
    if (!box || FAILED(XuiElementGetBounds(panel, &pw, &ph)) || FAILED(XuiElementGetBounds(box, &w, &h)))
        return;
    D3DXVECTOR3 at(0, 0, 0);
    XuiElementGetPosition(box, &at);
    float want = pw - at.x - 10.0f;
    if (want > w + 1.0f)
        XuiElementSetBounds(box, want, h);
}

std::map<HXUIOBJ, HXUIOBJ> g_metaScenes;        // DashScene -> its panel scene now
std::map<HXUIOBJ, std::wstring> g_metaFor;      // DashScene -> the button that panel is for
std::map<HXUIOBJ, std::wstring> g_metaPending;  // DashScene -> the panel scene to build, once landed
const DWORD kMetaTimer = 0x4D455431;            // "MET1"
const DWORD kMetaPrebuildTimer = 0x4D455432;    // "MET2"
// The panel's visual (metaScene_1line) shows the "Current Setting" in a text
// presenter of its own, data 4 (Pane_txtCurrentSetting): MetaPanelScene answers
// it, empty (no current setting there).
std::map<HXUIOBJ, std::wstring> g_metaValues;   // panel -> its current-setting line
// Panels whose texts lose the room 1888 left for the current setting (four
// empty lines first, Console Settings'). By panel: the scene's base class
// (DashScene) sets the text too, after the scene's own class.
std::set<HXUIOBJ> g_noCurrentSetting;
// Buttons SlopDash gave another job (System: Computers -> Themes): their text.
const wchar_t* OwnDescription(const std::wstring& id)
{
    if (id == L"navSystemSetUp")    // the System blade's Themes (SystemBladeSetup)
        return L"Select your dashboard";
    if (id == L"navDemos")          // the Games blade's Game Library (GamesBladeSetup): 1888's Demos text was left
        return L"Play the games on your hard drive and storage devices.";
    if (id == L"navTrailers")       // ... its Emulators
        return L"Play the emulators on your hard drive and storage devices. Get more from the Marketplace.";
    if (id == L"btnVideos")         // Media's Videos (theme/video1888.inc)
        return L"Watch the videos on your hard drive, storage devices and discs.";
    if (id == L"navApps")           // the Media blade's Applications (MediaBladeSetup)
        return L"Run the applications and dashboards on your hard drive and storage devices. Get more from the "
               L"Marketplace.";
    return NULL;
}

// The Xbox Live Arcade menu's panels (arcade#2500_meta*.xur): their text came
// from 1888's Arcade code (CMyArcadeMetaScene, CDownloadMetaScene,
// CRecentGameMetaScene), in its own words (arcade#Strings.xus); the files
// hold "<text>" and "<live prompt text>".
// Music and Pictures (music#1000_metaByDevice.xur, pictures#900_metaByDevice.xur,
// classes MediaSourceMetadataScene / PicturesMetadataScene): the focused
// source's name, icon and 1888's own words for it (the packages' Strings.xus:
// music 13, 32, 34, 41, 69; pictures 3, 5, 7, 16, 18) instead of the files'
// "<Source Name>" / "<Text>" placeholders and the "connecting" animation.
struct SourceWords { const wchar_t* button; const wchar_t* name; const wchar_t* icon; const wchar_t* words; };
const SourceWords kMusicSources[] = {
    { L"navMusicPlayer", L"Music Player", L"ico_96x_Music.png", L"To play music, select a music source." },
    { L"navOption1", L"Hard Drive", L"ico_96x_HD.png", L"Listen to music stored on your Xbox 360 Hard Drive." },
    { L"navOption2", L"Computer", NULL, L"Stream music from a Windows-based PC." },
    { L"navOption3", L"Current Disc", NULL, L"To listen to or rip music from a CD, insert a disc." },
    { L"navOption4", L"Portable Device", NULL, L"Listen to music stored on the connected portable device." },
};
const SourceWords kPictureSources[] = {
    { L"btnOption1", L"Computer", NULL, L"View pictures from a Windows-based PC." },
    { L"btnOption2", L"Digital Camera", L"ico_96x_Camera.png",
      L"To view pictures stored on a digital camera, connect the camera to your console's USB port." },
    { L"btnOption3", L"Current Disc", L"ico_96x_OpticalMedia.png", L"To view pictures on a data disc, insert the data disc." },
    { L"btnOption4", L"Portable Device", L"ico_96x_folder.png", L"View pictures stored on the connected portable device." },
    // (SlopDash's: the scene's hidden fifth and sixth rows, their words after 1888's)
    { L"btnOption5", L"Recordings", L"ico_96x_folder.png", L"Watch recordings made with the Screen Recorder." },
    { L"btnOption6", L"Screenshots", L"ico_96x_folder.png", L"View screenshots taken in your games." },
};

void MediaMeta(HXUIOBJ shown, const std::wstring& scene, const std::wstring& button)
{
    const SourceWords* table = NULL;
    size_t n = 0;
    if (scene == L"1000_metaByDevice.xur")
        table = kMusicSources, n = ARRAYSIZE(kMusicSources);
    else if (scene == L"900_metaByDevice.xur")
        table = kPictureSources, n = ARRAYSIZE(kPictureSources);
    if (!shown || !table)
        return;
    const SourceWords* w = NULL;
    for (size_t i = 0; i < n && !w; i++)
        if (button == table[i].button)
            w = &table[i];
    SetText(shown, L"labelHeader", w ? w->name : L"");
    SetText(shown, L"labelText", w ? w->words : L"");
    SetText(shown, L"labelSecondaryText", L"");
    if (HXUIOBJ spinner = Find(shown, L"labelConnectingAnimation"))
        XuiElementSetShow(spinner, FALSE);
    if (HXUIOBJ icon = Find(shown, L"imageMediaSourceIcon"))
    {
        if (w && w->icon)
            XuiImageElementSetImagePath(icon, w->icon);
        XuiElementSetShow(icon, w && w->icon);
    }
}

void ArcadeMeta(HXUIOBJ shown, const std::wstring& scene)
{
    HXUIOBJ text = Find(shown, L"lblText");
    if (!text)
        return;
    std::wstring line;
    if (scene == L"2500_metaMyArcade.xur")
    {
        int n = 0;
        const std::vector<library::Game>& all = AllGames();
        for (size_t i = 0; i < all.size(); i++)
            if (all[i].kind == library::kArcade)
                n++;
        wchar_t b[160];
        if (n == 0)
            wcscpy_s(b, L"You don't have any Arcade games on your console. Select Download Games to try out a game for free!");
        else if (n == 1)
            wcscpy_s(b, L"You have 1 Arcade game on your console. Select this option to play your game now.");
        else
            _snwprintf_s(b, 160, _TRUNCATE, L"You have %d Arcade games on your console. Select this option to play a game now.", n);
        line = b;
    }
    else if (scene == L"2500_metaDownload.xur")
        // Offline (as SlopDash is): the "Connect to Xbox Live" wording.
        line = L"Choose from a variety of games, including arcade classics, puzzles, board games, and more. "
               L"Connect to Xbox Live now and try out all of the games for free.";
    else
        return;
    XuiControlSetText(text, line.c_str());
}

// A focused button's panel scene in a DashScene's metaPanelScene.
// Each panel shows one scene, recorded with the button it's for
// (g_panelScene), so a second asker for the same panel finds it there: each
// focus change built its scene twice, about 30 ms each (the crawl of
// 2026-10-14).
std::map<HXUIOBJ, std::pair<std::wstring, HXUIOBJ> > g_panelScene;     // panel: (scene|button, shown)

// The panel's scene goes (only the panel's own: a handle another asker kept
// may be stale).
void DropMeta(HXUIOBJ panel, HXUIOBJ& shown)
{
    std::pair<std::wstring, HXUIOBJ>& onPanel = g_panelScene[panel];
    if (shown && shown == onPanel.second && XuiHandleIsValid(shown))
    {
        XuiElementUnlink(shown);
        XuiDestroyObject(shown);
        onPanel = std::make_pair(std::wstring(), (HXUIOBJ)NULL);
    }
    shown = NULL;
}

void BuildMetaScene(HXUIOBJ obj, HXUIOBJ panel, const std::wstring& scene)
{
    HXUIOBJ& shown = g_metaScenes[obj];
    std::pair<std::wstring, HXUIOBJ>& onPanel = g_panelScene[panel];
    std::wstring key = scene + L"|" + g_metaFor[obj];
    if (onPanel.second && onPanel.first == key && XuiHandleIsValid(onPanel.second))
    {
        if (shown != onPanel.second)
            Log("1888 meta: %S already on the panel (asked by %S)", scene.c_str(), ElementId(obj).c_str());
        shown = onPanel.second;
        return;
    }
    if (onPanel.second && XuiHandleIsValid(onPanel.second))
    {
        XuiElementUnlink(onPanel.second);
        XuiDestroyObject(onPanel.second);
    }
    onPanel = std::make_pair(key, (HXUIOBJ)NULL);
    shown = NULL;
    AddScene(panel, L"dashmain", scene.c_str(), &shown);     // the resolver finds its package
    onPanel.second = shown;
    ArcadeMeta(shown, scene);
    MediaMeta(shown, scene, g_metaFor[obj]);
    // Computers (dashcomm#742_metaByDevice.xur): which computer is connected;
    // SlopDash connects to none, so 1888's own "not connected" words.
    if (scene == L"742_metaByDevice.xur")
    {
        bool mce = g_metaFor[obj] == L"btnMediaCenter";
        SetText(shown, L"labelDeviceType", mce ? L"Windows Media Center" : L"Windows-based PC");
        SetText(shown, L"labelDeviceConnection",
                mce ? L"Your console is not connected to a PC with Windows Media Center." : L"Not connected.");
        SetText(shown, L"labelDeviceName", L"");
        SetText(shown, L"labelHelpText",
                mce ? L"To connect to a PC with Windows Media Center, go to Media and select Media Center."
                    : L"To connect to a Windows-based PC, go to Media and select Music or Pictures.");
    }
    // The blades' message-of-the-day panels (gamesMetaMOTD.xur, class
    // CBladeMOTD): "Create Gamer Profile" (scnNoProfiles) for a console
    // without profiles, else the billboard (botdBillboard, an
    // XuiBOTDContainer) with the banner its P1 names (offline,
    // botd#defaultbanner_games.xur). Neither class's code is here: done now.
    HXUIOBJ noProfiles = Find(shown, L"scnNoProfiles");
    HXUIOBJ billboard = Find(shown, L"botdBillboard");
    if (noProfiles)
    {
        // (the profiles already read; scanned again only when there were
        // none, at most every 10 s: a new one may have been made)
        static DWORD scanned = 0;
        if (Profiles().empty() && (!scanned || GetTickCount() - scanned > 10000))
        {
            ScanProfiles();
            scanned = GetTickCount();
        }
        bool none = Profiles().empty();
        XuiElementSetShow(noProfiles, none);
        if (billboard && !none)
        {
            std::wstring banner = OwnString(billboard, L"XuiBOTDContainer", 1);
            HXUIOBJ added = NULL;
            if (!banner.empty())
                AddScene(billboard, L"botd", banner.c_str(), &added);
            Log("1888 MOTD: banner %S: %s", banner.c_str(), added ? "shown" : "MISSING");
        }
    }
}

// A description without the empty lines 1888 put first.
std::wstring TrimTop(const std::wstring& text)
{
    size_t start = text.find_first_not_of(L"\r\n");
    return start == std::wstring::npos ? std::wstring() : text.substr(start);
}

void DashMeta(HXUIOBJ obj, XUIMessage* msg)
{
    if (msg->dwMessage == XM_DESTROY)
    {
        if (HXUIOBJ panel = Child(obj, L"metaPanelScene"))
        {
            g_panelScene.erase(panel);
            g_noCurrentSetting.erase(panel);
        }
        g_metaScenes.erase(obj);
        g_metaFor.erase(obj);
        g_metaPending.erase(obj);
        return;
    }
    // A blade's first panel (its gamer card's: the message of the day) is
    // built shortly after start-up, so the first visit has it ready.
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMetaPrebuildTimer, 1500);
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMetaPrebuildTimer)
    {
        XuiKillTimer(obj, kMetaPrebuildTimer);
        std::vector<std::wstring> ids = SplitList(OwnString(obj, L"DashScene", 0));
        std::vector<std::wstring> scenes = SplitList(OwnString(obj, L"DashScene", 2));
        HXUIOBJ panel = Child(obj, L"metaPanelScene");
        if (panel && !g_metaScenes[obj] && !ids.empty() && !scenes.empty() &&
            scenes[0].find(L"MOTD") != std::wstring::npos)
        {
            g_metaFor[obj] = ids[0];
            BuildMetaScene(obj, panel, scenes[0]);
        }
        msg->bHandled = TRUE;
        return;
    }
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMetaTimer)
    {
        XuiKillTimer(obj, kMetaTimer);
        std::wstring scene = g_metaPending[obj];
        g_metaPending.erase(obj);
        HXUIOBJ panel = Child(obj, L"metaPanelScene");
        // (moved on past this blade meanwhile, or to another of its buttons:
        // nothing to build)
        bool onIt = false;
        for (HXUIOBJ f = XuiElementGetFocus(); f && f != obj && !onIt; XuiElementGetParent(f, &f))
            onIt = ElementId(f) == g_metaFor[obj];
        if (panel && !scene.empty() && XuiElementTreeHasFocus(obj) && onIt)
            BuildMetaScene(obj, panel, scene);
        msg->bHandled = TRUE;
        return;
    }
    if (msg->dwMessage != XM_NOTIFY || ((XUINotify*)msg->pvData)->dwNotify != XN_SET_FOCUS)
        return;
    std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
    std::vector<std::wstring> ids = SplitList(OwnString(obj, L"DashScene", 0));
    size_t i = 0;
    while (i < ids.size() && ids[i] != id)
        i++;
    if (i == ids.size() && !OwnDescription(id))
        return;
    HXUIOBJ panel = Child(obj, L"metaPanelScene");
    if (!panel)
        return;
    std::vector<std::wstring> texts = SplitList(OwnString(obj, L"DashScene", 1));
    std::vector<std::wstring> scenes = SplitList(OwnString(obj, L"DashScene", 2));
    HXUIOBJ& shown = g_metaScenes[obj];
    // The same button's panel already up (a blade switch focuses the blade's
    // first button again): kept. Building it anew - its scene, the banner's,
    // a scan of every profile on the drives - held one frame 300 to 700 ms
    // on every switch to Games or Media, the blades then jumped to the end
    // of their slide.
    if (shown && g_metaFor[obj] == id)
        return;
    g_metaFor[obj] = id;
    DropMeta(panel, shown);
    if (i < scenes.size() && !scenes[i].empty() && !OwnDescription(id))
    {
        XuiControlSetText(panel, L"");
        // Built once the blade has landed (a switch focuses the new blade's
        // first button as it starts to slide; building a panel scene takes
        // frames, which the slide would skip).
        // (the blades' panels only: an Arcade menu's show at once)
        if (scenes[i].find(L"MOTD") != std::wstring::npos)
        {
            g_metaPending[obj] = scenes[i];
            XuiSetTimer(obj, kMetaTimer, 350);
        }
        else
        {
            // (a message-of-the-day panel still waiting is for a button the
            // focus has left: back from a screen, the focus passes the gamer
            // card on its way to the button it had, and the card's panel came
            // up over that button's: the crawl of 2026-10-14)
            g_metaPending.erase(obj);
            XuiKillTimer(obj, kMetaTimer);
            BuildMetaScene(obj, panel, scenes[i]);
        }
        return;
    }
    g_metaPending.erase(obj);
    XuiKillTimer(obj, kMetaTimer);
    std::wstring text = OwnDescription(id) ? OwnDescription(id) : i < texts.size() ? texts[i] : L"";
    g_metaValues[panel] = L"";
    if (g_noCurrentSetting.count(panel))
        text = TrimTop(text);
    XuiControlSetText(panel, L"");              // the presenters ask again
    XuiControlSetText(panel, text.c_str());
    WidenPaneText(panel);
}

HRESULT MetaPanelProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        std::map<HXUIOBJ, std::wstring>::const_iterator v = g_metaValues.find(obj);
        if (!d->bItemData && d->iData == 4 && v != g_metaValues.end())
        {
            d->szText = v->second.c_str();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_DESTROY)
        g_metaValues.erase(obj);
    return S_OK;
}

const DWORD kArcadeHomeTimer = 0x41524331;     // "ARC1": once the Arcade menu's parts exist

HRESULT DashSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    // Xbox Live Arcade's menu (arcade#2500_LiveArcadeHome.xur): no Last Played
    // Game (btnRecent).
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kArcadeHomeTimer, 20);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kArcadeHomeTimer)
    {
        XuiKillTimer(obj, kArcadeHomeTimer);
        if (HXUIOBJ recent = Child(obj, L"btnRecent"))
        {
            XuiElementSetShow(recent, FALSE);
            XuiControlSetEnable(recent, FALSE);
            if (HXUIOBJ more = Child(obj, L"btnMore"))
                SetStringProperty(more, L"NavDown", L"");
            Log("1888 arcade: no Last Played Game");
        }
        msg->bHandled = TRUE;
        return S_OK;
    }
    DashMeta(obj, msg);
    return S_OK;
}

// Console Settings (consoles#dashSysCslSet.xur, class dashSettingsRoot). The
// user's choice (2026-10-15): no "Current Setting" there, its heading
// (labMetaHeader) and the value line gone: the description alone, as on the
// blades. (Each setting's own screen still shows its current setting.) The
// description starts at the panel's top, where the heading was (DashMeta).
const DWORD kCslPanelTimer = 0x43534C34;        // "CSL4": the heading hidden, once the scene's parts exist

HRESULT SettingsRootProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        if (HXUIOBJ panel = Child(obj, L"metaPanelScene"))
            g_noCurrentSetting.insert(panel);
        XuiSetTimer(obj, kCslPanelTimer, 20);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kCslPanelTimer)
    {
        XuiKillTimer(obj, kCslPanelTimer);
        if (HXUIOBJ heading = Child(obj, L"labMetaHeader"))
            XuiElementSetShow(heading, FALSE);
        // (a text set before the panel was known: without its empty lines too)
        if (HXUIOBJ panel = Child(obj, L"metaPanelScene"))
        {
            g_noCurrentSetting.insert(panel);
            LPCWSTR now = XuiControlGetText(panel);
            std::wstring text = now ? now : L"";
            if (TrimTop(text) != text)
                XuiControlSetText(panel, TrimTop(text).c_str());
        }
        msg->bHandled = TRUE;
        return S_OK;
    }
    DashMeta(obj, msg);
    return S_OK;
}

// --- the games blade: SlopDash's own buttons ----------------------------------------
// Demos becomes Game Library (every game but Xbox Live
// Arcade's), Trailers becomes Emulators, Played Games goes; Xbox Live Arcade >
// My Arcade Games is the Arcade games. Each sets the library's scope as it
// opens it.
const DWORD kGamesSetupTimer = 0x47424C31;      // "GBL1"
const wchar_t kLibraryScene[] = L"2502_TitleSelectorScene.xur";

void GamesBladeSetup(HXUIOBJ content)
{
    HXUIOBJ demos = Child(content, L"navDemos"), trailers = Child(content, L"navTrailers");
    HXUIOBJ tray = Child(content, L"btn_tray");
    if (!demos)
        return;
    XuiControlSetText(demos, L"Game Library");
    SetStringProperty(demos, L"PressPath", kLibraryScene);
    // Trailers is Emulators: the library's emulators (the Emulators folders,
    // where the Marketplace's store installs them), with xam's controller icon.
    if (trailers)
    {
        XuiControlSetText(trailers, L"Emulators");
        SetStringProperty(trailers, L"PressPath", kLibraryScene);
        SetStringProperty(trailers, L"ImagePath", L"ico_32x_controller.png");
    }
    // Played Games goes.
    HXUIOBJ played = Child(content, L"navGames");
    if (played)
    {
        XuiElementSetShow(played, FALSE);
        XuiControlSetEnable(played, FALSE);
    }
    // The order: Game Library, Xbox Live Arcade, Emulators,
    // Achievements (signed out: Create Gamer Profile first, no Achievements).
    // The rows take the file's places top down (Played Games' too, so no gap
    // is left where it was); up / down follow.
    static const wchar_t* const kOrder[] = { L"navCreateProfile", L"navDemos", L"navArcade", L"navTrailers",
                                             L"navAchievements" };
    std::vector<HXUIOBJ> rows;
    std::vector<float> ys;
    for (size_t i = 0; i < sizeof(kOrder) / sizeof(kOrder[0]); i++)
        if (HXUIOBJ b = Child(content, kOrder[i]))
            rows.push_back(b);
    for (size_t i = 0; i <= rows.size(); i++)
    {
        HXUIOBJ b = i < rows.size() ? rows[i] : played;
        if (!b)
            continue;
        D3DXVECTOR3 at(0, 0, 0);
        XuiElementGetPosition(b, &at);
        ys.push_back(at.y);
    }
    std::sort(ys.begin(), ys.end());
    HXUIOBJ card = Child(content, L"ctlGamerCard");
    if (!card)
        card = Child(content, L"fakeGamerCard");
    for (size_t i = 0; i < rows.size(); i++)
    {
        D3DXVECTOR3 at(0, 0, 0);
        XuiElementGetPosition(rows[i], &at);
        at.y = ys[i];
        XuiElementSetPosition(rows[i], &at);
        std::wstring up = i ? ElementId(rows[i - 1]) : (card ? ElementId(card) : L"");
        std::wstring down = i + 1 < rows.size() ? ElementId(rows[i + 1]) : (tray ? L"btn_tray" : L"");
        SetStringProperty(rows[i], L"NavUp", up.c_str());
        SetStringProperty(rows[i], L"NavDown", down.c_str());
    }
    if (card && !rows.empty())
        SetStringProperty(card, L"NavDown", ElementId(rows[0]).c_str());
    if (tray && !rows.empty())
        SetStringProperty(tray, L"NavUp", ElementId(rows.back()).c_str());
    Log("1888 games blade: Game Library in place of Demos, Emulators in place of Trailers, %u rows in order",
        (unsigned)rows.size());
}

// The Media blade: Applications (the library's
// apps and homebrew dashboards: the Apps folders, where the store installs
// them), Music, Media (Pictures and Video as one: the Pictures screen, its
// sources; SlopDash plays no video yet), Media Center. The rows take the
// file's four places, top down; up / down follow.
void MediaBladeSetup(HXUIOBJ content)
{
    HXUIOBJ mcx = Child(content, L"navMCX"), tray = Child(content, L"btn_tray");
    if (!mcx || Child(content, L"navApps"))
        return;
    HXUIOBJ apps = NULL;
    if (FAILED(XuiCreateObject(L"XuiNavButton", &apps)) || !apps)
        return;
    SetStringProperty(apps, L"Id", L"navApps");
    SetStringProperty(apps, L"Visual", L"btn_1line_icon");
    float w = 0, h = 0;
    XuiElementGetBounds(mcx, &w, &h);
    XuiElementSetBounds(apps, w, h);
    XuiElementAddChild(content, apps);
    XuiControlSetText(apps, L"Applications");
    SetStringProperty(apps, L"ImagePath", L"ico_32x_Folder.png");
    SetStringProperty(apps, L"PressPath", kLibraryScene);
    if (HXUIOBJ pictures = Child(content, L"navPictures"))
        XuiControlSetText(pictures, L"Media");
    if (HXUIOBJ video = Child(content, L"navVideo"))
    {
        XuiElementSetShow(video, FALSE);
        XuiControlSetEnable(video, FALSE);
    }
    // The file's four places (Music, Pictures, Video, Media Center).
    static const wchar_t* const kSlots[] = { L"navMusic", L"navPictures", L"navVideo", L"navMCX" };
    std::vector<float> ys;
    for (int i = 0; i < 4; i++)
        if (HXUIOBJ b = Child(content, kSlots[i]))
        {
            D3DXVECTOR3 at(0, 0, 0);
            XuiElementGetPosition(b, &at);
            ys.push_back(at.y);
        }
    std::sort(ys.begin(), ys.end());
    static const wchar_t* const kOrder[] = { L"navApps", L"navMusic", L"navPictures", L"navMCX" };
    std::vector<HXUIOBJ> rows;
    for (int i = 0; i < 4; i++)
        if (HXUIOBJ b = Child(content, kOrder[i]))
            rows.push_back(b);
    HXUIOBJ card = Child(content, L"ctlGamerCard");
    if (!card)
        card = Child(content, L"fakeGamerCard");
    D3DXVECTOR3 x0(0, 0, 0);
    XuiElementGetPosition(mcx, &x0);
    for (size_t i = 0; i < rows.size() && i < ys.size(); i++)
    {
        D3DXVECTOR3 at(x0.x, ys[i], 0);
        XuiElementSetPosition(rows[i], &at);
        std::wstring up = i ? ElementId(rows[i - 1]) : (card ? ElementId(card) : L"");
        std::wstring down = i + 1 < rows.size() ? ElementId(rows[i + 1]) : (tray ? L"btn_tray" : L"");
        SetStringProperty(rows[i], L"NavUp", up.c_str());
        SetStringProperty(rows[i], L"NavDown", down.c_str());
    }
    if (card && !rows.empty())
        SetStringProperty(card, L"NavDown", ElementId(rows[0]).c_str());
    if (tray && !rows.empty())
        SetStringProperty(tray, L"NavUp", ElementId(rows.back()).c_str());
    Log("1888 media blade: Applications, Music, Media, Media Center (%u rows)", (unsigned)rows.size());
}

// The System blade (DashSystemScene): SlopDash is a theme loader, and its
// Themes takes Initial Setup's place, with its icon.
void SystemBladeSetup(HXUIOBJ content)
{
    HXUIOBJ setup = Child(content, L"navSystemSetUp");
    if (!setup)
        return;
    XuiControlSetText(setup, L"Themes");
    // 6717's System blade (its dashmain) as 1888's: no Xbox LIVE Vision or IPTV,
    // and Initial Setup (Themes) back under Computers (1888: y 378, 45 below).
    HXUIOBJ vision = Child(content, L"navLiveVision"), iptv = Child(content, L"navIPTVSettings");
    HXUIOBJ computers = Child(content, L"navWindowMediaConnect");
    for (HXUIOBJ h = vision; h; h = (h == vision ? iptv : NULL))
    {
        XuiElementSetShow(h, FALSE);
        XuiControlSetEnable(h, FALSE);
    }
    if (vision)
    {
        D3DXVECTOR3 at(0, 0, 0);
        XuiElementGetPosition(setup, &at);
        at.y = 378.0f;
        XuiElementSetPosition(setup, &at);
        SetStringProperty(setup, L"NavUp", L"navWindowMediaConnect");
        SetStringProperty(setup, L"NavDown", L"");
        if (computers)
            SetStringProperty(computers, L"NavDown", L"navSystemSetUp");
    }
    Log("1888 system blade: Themes in place of Initial Setup%s", vision ? " (6717's extras hidden)" : "");
}

void ScopeFor(const std::wstring& id)
{
    if (id == L"navGames")
        SetLibraryScope(kScopeAll);
    else if (id == L"navArcade")
        SetLibraryScope(kScopeArcade);
    else if (id == L"navDemos")
        SetLibraryScope(kScopeNoArcade);
    else if (id == L"navTrailers")
        SetLibraryScope(kScopeEmulators);
    else if (id == L"navApps")
        SetLibraryScope(kScopeApps);
}

// The disc tray, through the console's system controller (SMC) as 1888's xam
// talks to it: command 0x0A asks the tray's state (818C2F64: reply byte 1 is
// 0x60 open, 0x62 closed, 0x63 closing, 0x64 opening); command 0x8B moves it
// (0x60 open, 0x62 close).
BYTE TrayState()
{
    BYTE ask[16] = { 0x0A }, state[16] = { 0 };
    HalSendSMCMessage(ask, state);
    return state[0] == 0x0A ? state[1] : 0;
}

void ToggleTray()
{
    BYTE was = TrayState();
    bool open = was == 0x60 || was == 0x64;
    Log("1888 tray: was %02X, %s", (unsigned)was, open ? "closing" : "opening");
    // 0x8B sends no reply: given a buffer for one, the call waited for it
    // forever and SlopDash froze (0.0.111).
    BYTE move[16] = { 0x8B, (BYTE)(open ? 0x62 : 0x60) };
    HalSendSMCMessage(move, NULL);
}

// The tray button's text, as 1888's dashboard showed it (dashcomm#dashStrings.xus:
// "Open Tray", "Opening", "Close Tray", "Closing", "Reading", "Play Game",
// "Play DVD", "Play CD", "Mixed Media Disc", "Unrecognized Disc"). The disc is
// looked at each time the tray has closed, until the drive answers.
enum DiscKind { kDiscUnknown, kDiscNone, kDiscGame, kDiscDvd, kDiscCd, kDiscMixed, kDiscOther, kDiscReading };
bool g_discXbe = false;      // the game disc is an original Xbox one (default.xbe)
volatile DiscKind g_disc = kDiscUnknown;
wchar_t g_discTitle[96] = L"";                  // a game disc's title (its default.xex), once read
BYTE g_trayWas = 0;
const DWORD kTrayTimer = 0x54524159;            // "TRAY"

// What's in the drive: kDiscReading while it can't answer yet (a disc takes
// some seconds to spin up after the tray closes; looked at once, straight
// away, a game disc was never found).
DiscKind LookAtDisc()
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("dvd:\\*", &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD e = GetLastError();
        if (e == ERROR_NOT_READY)
            return kDiscReading;
        return e == ERROR_FILE_NOT_FOUND || e == ERROR_NO_MORE_FILES ? kDiscOther : kDiscNone;
    }
    FindClose(h);
    if (GetFileAttributesA("dvd:\\default.xex") != (DWORD)-1)
    {
        // The game's own title, from its executable (as the Game Library reads them).
        g_discTitle[0] = 0;
        HANDLE x = CreateFileA("dvd:\\default.xex", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (x != INVALID_HANDLE_VALUE)
        {
            DWORD size = GetFileSize(x, NULL), got = 0;
            if (size > 0 && size <= 32 * 1024 * 1024)
            {
                std::vector<unsigned char> data(size);
                library::Game g;
                if (ReadFile(x, &data[0], size, &got, NULL) && got == size && library::ReadXex(&data[0], size, g))
                    wcsncpy_s(g_discTitle, g.name.c_str(), _TRUNCATE);
            }
            CloseHandle(x);
        }
        g_discXbe = false;
        Log("1888 tray: game disc \"%S\"", g_discTitle);
        return kDiscGame;
    }
    if (GetFileAttributesA("dvd:\\default.xbe") != (DWORD)-1)
    {
        // An original Xbox game: its executable is default.xbe (the console's
        // emulator runs it), and there's no 360 header to name it.
        g_discTitle[0] = 0;
        g_discXbe = true;
        Log("1888 tray: original Xbox game disc");
        return kDiscGame;
    }
    if (GetFileAttributesA("dvd:\\VIDEO_TS") != (DWORD)-1)
        return kDiscDvd;
    return kDiscMixed;
}

// The tray's state and the disc, kept by a thread of their own: asked from
// the blades' timers (0.0.112, every content each second, on the UI thread)
// each call waited on the system controller, and the dashboard crawled.
volatile LONG g_trayState = 0;
volatile LONG g_trayThread = 0;

DWORD WINAPI TrayThread(LPVOID)
{
    for (;;)
    {
        BYTE s = TrayState();
        if (s != g_trayWas)
        {
            if (s == 0x62)
                g_disc = kDiscUnknown;      // just closed: look again
            g_trayWas = s;
        }
        // Closed: looked at until the drive answers. An empty drive answers
        // "not ready" too: still not ready after 10 s, there's no disc.
        static int looks = 0;
        if (s != 0x62)
            looks = 0;
        if (s == 0x62 && (g_disc == kDiscUnknown || g_disc == kDiscReading))
        {
            looks++;
            DiscKind d = LookAtDisc();
            if (d == kDiscReading && looks > 20)
                d = kDiscNone;
            if (d != g_disc)
                Log("1888 tray: closed, disc %d", (int)d);
            g_disc = d;
        }
        InterlockedExchange(&g_trayState, s);
        Sleep(500);
    }
}

const wchar_t* TrayText()
{
    if (InterlockedCompareExchange(&g_trayThread, 1, 0) == 0)
    {
        HANDLE t = CreateThread(NULL, 0, TrayThread, NULL, 0, NULL);
        if (t)
            CloseHandle(t);
    }
    BYTE s = (BYTE)g_trayState;
    switch (s)
    {
    case 0x60: return L"Close Tray";
    case 0x63: return L"Closing";
    case 0x64: return L"Opening";
    case 0x62: break;
    default: return L"Please Wait";
    }
    static wchar_t play[128];
    switch (g_disc)
    {
    case kDiscReading: return L"Reading";
    case kDiscGame:
        if (!g_discTitle[0])
            return L"Play Game";
        _snwprintf_s(play, 128, _TRUNCATE, L"Play %s", g_discTitle);
        return play;
    case kDiscDvd: return L"Play DVD";
    case kDiscMixed: return L"Mixed Media Disc";
    case kDiscOther: return L"Unrecognized Disc";
    default: return L"Open Tray";
    }
}

void PressTray()
{
    if (g_trayState == 0x62 && g_disc == kDiscGame)
    {
        Log("1888 tray: playing the game disc");
        LogFlush();
        XLaunchNewImage(g_discXbe ? "dvd:\\default.xbe" : "dvd:\\default.xex", 0);
        return;
    }
    ToggleTray();
}

// The console's box SlopDash has open, if any.
XOVERLAPPED* g_boxOv = NULL;

void BoxOpened(XOVERLAPPED* ov, DWORD r)
{
    if (r == ERROR_IO_PENDING)
        g_boxOv = ov;
}

void CancelBox()
{
    if (g_boxOv && !XHasOverlappedIoCompleted(g_boxOv))
        Log("message box closed: %u", (unsigned)XCancelOverlapped(g_boxOv));
    g_boxOv = NULL;
}

// The Xbox Live blade's buttons that went to Xbox Live itself (Join, Connect,
// Recover Gamertag): SlopDash runs offline, so the console's message box says
// so (XShowMessageBoxUI: drawn by the Guide, in 1888's style with SlopGuide).
// Short.
void SayOffline(const wchar_t* title)
{
    static XOVERLAPPED ov;
    static MESSAGEBOX_RESULT result;
    static LPCWSTR buttons[] = { L"OK" };
    int user = SignedInUser();
    ZeroMemory(&ov, sizeof(ov));
    DWORD r = XShowMessageBoxUI(user >= 0 ? user : 0, title, L"Cannot Connect. SlopDash Runs Offline", 1, buttons, 0,
                                XMB_ALERTICON, &result, &ov);
    BoxOpened(&ov, r);
    Log("1888 xbox live: %S: message box %u", title, (unsigned)r);
}

// The Xbox Live blade's banner: the Marketplace blade (SlopDash's homebrew
// store), one blade left, as LB takes you there.
void ToMarketplaceBlade()
{
    XINPUT_KEYSTROKE k;
    ZeroMemory(&k, sizeof(k));
    k.VirtualKey = VK_PAD_LSHOULDER;
    k.Flags = XINPUT_KEYSTROKE_KEYDOWN;
    XuiProcessInput(&k);
    k.Flags = XINPUT_KEYSTROKE_KEYUP;
    XuiProcessInput(&k);
}

void StartSlideshow(HXUIOBJ owner, HXUIOBJ box);       // (the Xbox Live blade's billboard: below)
bool SlideshowMessage(HXUIOBJ obj, XUIMessage* msg);

HRESULT ContentProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    DashMeta(obj, msg);
    if (SlideshowMessage(obj, msg))
        return S_OK;
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kGamesSetupTimer, 20);         // once its parts exist
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kGamesSetupTimer)
    {
        XuiKillTimer(obj, kGamesSetupTimer);
        if (Child(obj, L"navArcade"))
            GamesBladeSetup(obj);
        if (Child(obj, L"navMCX"))
            MediaBladeSetup(obj);
        if (Child(obj, L"navSystemSetUp"))
            SystemBladeSetup(obj);
        // The Xbox Live blade's Connect to Xbox Live / Join Xbox Live (no Xbox
        // Live here): RetroAchievements, the online service SlopDash signs in to.
        // A row like the other blades': 1888's one-line
        // row button (btn_1line_icon, 330 x 47) where the Games blade's first
        // row sits, under the gamer card; no Recover Gamertag below it.
        static const wchar_t* const kRaButtons[] = { L"btnConnect", L"btnJoinXbox" };
        for (int i = 0; i < 2; i++)
            if (HXUIOBJ b = Find(obj, kRaButtons[i]))
            {
                XuiControlSetText(b, L"RetroAchievements");
                SetStringProperty(b, L"Visual", L"btn_1line_icon");
                XuiElementSkinChanged(b);
                XuiElementSetBounds(b, 330.0f, 47.0f);
                D3DXVECTOR3 at(8.0f, 130.0f, 0.0f);
                XuiElementSetPosition(b, &at);
                SetStringProperty(b, L"NavDown", L"btn_AdBanner");
            }
        if (HXUIOBJ recover = Find(obj, L"btnUseExistingTag"))
        {
            XuiElementSetShow(recover, FALSE);
            XuiControlSetEnable(recover, FALSE);
        }
        if (HXUIOBJ banner = Find(obj, L"btn_AdBanner"))
        {
            if (Find(obj, L"btnJoinXbox"))
                SetStringProperty(banner, L"NavUp", L"btnJoinXbox");
            // (The slideshow in its billboard is off: the user didn't like it
            // there, 2026-10-15; to come back to. StartSlideshow(obj,
            // Find(obj, L"botdBillboard")) puts it back.)
        }
        if (Child(obj, L"btn_tray"))
        {
            XuiControlSetText(Child(obj, L"btn_tray"), TrayText());
            XuiSetTimer(obj, kTrayTimer, 1000);
        }
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kTrayTimer)
    {
        HXUIOBJ tray = Child(obj, L"btn_tray");
        if (tray && XuiElementIsShown(obj))
            XuiControlSetText(tray, TrayText());
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
    {
        HXUIOBJ source = ((XUINotify*)msg->pvData)->hObjSource;
        ScopeFor(ElementId(source));
        if (ElementId(source) == L"btn_tray")
        {
            PressTray();
            msg->bHandled = TRUE;
        }
        else if (ElementId(source) == L"navCreateProfile" && !HudOpen())
        {
            // The console's Create Profile (xam's XamShowCreateProfileUI, export
            // 719: user, flags; 1888's xam took the user), in the Guide with
            // 1888's scenes (plugin/SlopGuide.cpp: CP_*.xur).
            typedef DWORD (*ShowCreateProfileFn)(DWORD, DWORD);
            HANDLE xam = NULL;
            PVOID fn = NULL;
            DWORD r = (DWORD)-1;
            if (XexGetModuleHandle("xam.xex", &xam) >= 0 && XexGetProcedureAddress(xam, 719, &fn) >= 0 && fn)
                r = ((ShowCreateProfileFn)fn)(0, 0);
            Log("1888 create profile: %u", (unsigned)r);
            msg->bHandled = TRUE;
        }
        else if (ElementId(source) == L"navSystemSetUp" && !HudOpen())
        {
            HRESULT hr = OpenThemesScreen(source);
            Log("1888 themes: %08X", (unsigned)hr);
            msg->bHandled = TRUE;
        }
        else if (ElementId(source) == L"fakeGamerCard" && !HudOpen())
        {
            OpenSignIn();
            msg->bHandled = TRUE;
        }
        else if ((ElementId(source) == L"btnJoinXbox" || ElementId(source) == L"btnConnect") && !HudOpen())
        {
            // (now RetroAchievements: ContentProc renames them)
            Log("1888 retroachievements: %08X", (unsigned)OpenRetroAchievements(source));
            msg->bHandled = TRUE;
        }
        else if (ElementId(source) == L"btnUseExistingTag" && !HudOpen())
        {
            SayOffline(L"Recover Gamertag");
            msg->bHandled = TRUE;
        }
        else if (ElementId(source) == L"btn_AdBanner" && Child(obj, L"btn_AdBanner") && !HudOpen())
        {
            ToMarketplaceBlade();
            msg->bHandled = TRUE;
        }
        else if (OpenScreen(source, obj))
            msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_A && !HudOpen() &&
             ElementId(XuiElementGetFocus()) == L"fakeGamerCard")
    {
        OpenSignIn();
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_X && !HudOpen())
    {
        // Each blade's X legend: "Sign Out" (legend_x, class dashSignOut).
        int user = SignedInUser();
        if (user >= 0)
        {
            bool started = StartProfileSwitch(user, 0);
            Log("1888 sign out: user %d: %s", user, started ? "signing out" : "FAILED");
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}


// The blades' own control sounds on a blade switch: the focus landing on the
// new blade's first control
// (the gamer card, Console Settings...) played that control's focus sound
// (btn_focus.xma) over the blade's whoosh and landing. As the Marketplace hub
// does (HubSoundTick): a blade's controls' sounds (their visuals'
// Xaudio_Sound01; the strip's own whoosh and landing aren't in a control) are
// muted while the focus is on another blade, and back 300 ms after it arrives.
int MuteSoundsIn(HXUIOBJ e, bool mute, int depth)
{
    int found = 0;
    HXUIOBJ visual = NULL;
    if (SUCCEEDED(XuiControlGetVisual(e, &visual)) && visual)
        if (HXUIOBJ sound = Find(visual, L"Xaudio_Sound01"))
            XuiSoundMute(sound, mute ? TRUE : FALSE), found++;
    if (depth >= 8)
        return found;
    HXUIOBJ c = NULL;
    if (FAILED(XuiElementGetFirstChild(e, &c)))
        c = NULL;
    for (int guard = 0; c && guard < 128; guard++)
    {
        found += MuteSoundsIn(c, mute, depth + 1);
        HXUIOBJ next = NULL;
        if (FAILED(XuiElementGetNext(c, &next)))
            next = NULL;
        c = next;
    }
    return found;
}

void BladeSoundTick(HXUIOBJ root)
{
    static bool muted[8] = { false };
    static DWORD inSince[8] = { 0 };
    HXUIOBJ focus = XuiElementGetFocus();
    for (int i = 1; i <= 5; i++)
    {
        wchar_t name[8];
        _snwprintf_s(name, 8, _TRUNCATE, L"Tab%d", i);
        HXUIOBJ tab = Child(root, name);
        if (!tab)
            continue;
        bool inside = false;
        for (HXUIOBJ f = focus; f && !inside; XuiElementGetParent(f, &f))
            inside = f == tab;
        if (!inside)
        {
            inSince[i] = 0;
            if (!muted[i])
                MuteSoundsIn(tab, true, 0), muted[i] = true;
            continue;
        }
        if (!inSince[i])
            inSince[i] = GetTickCount();
        if (muted[i] && GetTickCount() - inSince[i] >= 300)     // (as the hub's kHubUnmuteAfter)
            MuteSoundsIn(tab, false, 0), muted[i] = false;
    }
}

class CDashMain;
CDashMain* g_dashMain = NULL;
DWORD g_levelHoldUntil = 0;     // the focus-based check waits until then (a level set ahead of it)

class CDashMain : public CXuiTabSceneImpl
{
public:
    XUI_IMPLEMENT_CLASS(CDashMain, L"DashMainScene", XUI_CLASS_TABSCENE)
    XUI_BEGIN_MSG_MAP()
        XUI_ON_XM_INIT(OnInit)
        XUI_ON_XM_NOTIFY_PRESS(OnNotifyPress)
        XUI_ON_XM_KEYDOWN(OnKeyDown)
        XUI_ON_XM_TIMER(OnTimer)
        XUI_ON_XM_SET_FOCUS(OnSetFocus)
        XUI_ON_XM_NOTIFY_SET_FOCUS(OnNotifySetFocus)
        XUI_ON_XM_TRANSITION_START(OnTransitionStart)
    XUI_END_MSG_MAP()

    CDashMain() : m_level(0), m_blade(0), m_legendTicks(0), m_target(0), m_closedAt(0) {}

    // Back from a screen, the focus goes straight to the button that opened
    // it. XUI's back navigation put it on the blade's first part first (the
    // tab, or the gamer card), then on this scene as it finished, before the
    // button got it back: it bounced.
    // While a screen closes (the level not 0 yet) any focus landing in the
    // blades, and for a moment after, this scene or a tab taking it, goes to
    // the button instead; the blade's own parts shown again first (hidden
    // while the screen was open, they couldn't take it).
    DWORD m_closedAt;               // when the level last went back to 0

    bool Returning(HXUIOBJ to)
    {
        if (!g_openedFocus || to == g_openedFocus)
            return false;
        if (m_level >= 1)
        {
            for (HXUIOBJ o = to; o; XuiElementGetParent(o, &o))
                if (o == m_hObj)
                    return true;        // (in the blades: a screen over them never hands it there)
            return false;
        }
        return GetTickCount() - m_closedAt < 1000 && (to == m_hObj || ElementId(to).compare(0, 3, L"Tab") == 0);
    }

    void ReturnFocus()
    {
        ShowBladeAgain();
        XuiElementSetFocus(g_openedFocus);
    }

    HRESULT OnSetFocus(HXUIOBJ, BOOL& bHandled)
    {
        if (Returning(m_hObj))
        {
            ReturnFocus();
            bHandled = TRUE;
        }
        return S_OK;
    }

    HRESULT OnNotifySetFocus(HXUIOBJ source, XUINotifyFocus*, BOOL& bHandled)
    {
        if (Returning(source))
        {
            ReturnFocus();
            bHandled = TRUE;
        }
        return S_OK;
    }

    // The blade's own parts back on show (hidden while a screen was open from it).
    static void ShowBladeAgain()
    {
        if (g_openedFrom)
            XuiElementSetShow(g_openedFrom, TRUE);
        g_openedFrom = NULL;
        for (size_t i = 0; i < g_bladeChrome.size(); i++)
            XuiElementSetShow(g_bladeChrome[i], TRUE);
        g_bladeChrome.clear();
    }

    HRESULT OnInit(XUIMessageInit*, BOOL& bHandled)
    {
        g_root = m_hObj;
        g_dashMain = this;
        XuiSetTimer(m_hObj, kLevelTimer, 100);
        XuiSetTimer(m_hObj, kBootTimer, 50);     // once the scene is complete
        XuiSetTimer(m_hObj, kOpenGameTimer, 500);
        m_openGameTries = 0;
        bHandled = FALSE;
        return S_OK;
    }

    // 6717's blades: the parts drawn with the Marketplace blade's visuals,
    // which only 6717's skin has (loaded under "M6", main.cpp). Once the scene
    // is complete (the boot timer).
    void UseM6Visuals()
    {
        static const wchar_t* const kM6[] = { L"BackgroundMarketplace", L"BG_color_2", L"blade1_top_jewel",
                                              L"color_highlight_left", L"color_highlight_rt" };
        // 1888's own strip with the Marketplace added (tools/dashmain5.py): its
        // Marketplace colour parts are their own elements; 1888's shared ones
        // keep 1888's skin.
        static const wchar_t* const kM6Strip5[] = { L"BG_color_1_mp", L"BG_color_2_mp", L"bladeMP_top_jewel",
                                                    L"color_highlight_left_mp", L"color_highlight_rt_mp" };
        bool strip5 = Child(m_hObj, L"BG_color_2_mp") != NULL;
        // (1888's strip names 6717's visuals with the prefix itself: given it
        // again here, they'd be looked for as "M6M6...".)
        const wchar_t* const* list = strip5 ? kM6Strip5 : kM6;
        int found = 0;
        for (size_t i = 0; i < 5 && !strip5; i++)
            if (HXUIOBJ e = Child(m_hObj, list[i]))
            {
                HRESULT hr = XuiElementSetVisualPrefix(e, L"M6");
                XuiElementSkinChanged(e);
                found++;
                Log("1888 blades: %S drawn with 6717's visual: %08X", list[i], (unsigned)hr);
            }
        Log("1888 blades: %d of 5 Marketplace parts found", found);
        // 1888's own dashmain: the tabs' names are 23 point (6717's 18), and
        // the Xbox Live blade's header reads "Xbox Live" (6717's "Xbox LIVE").
        // 23 point in 6717's layout (0.0.111) overran the tabs: 6717's kept.
        const float kTabPointSize = 0;
        for (int i = 0; i <= 5; i++)
        {
            wchar_t id[16];
            _snwprintf_s(id, 16, _TRUNCATE, L"blade_%d_txt", i);
            DWORD prop = 0;
            HXUIOBJ t = Child(m_hObj, id);
            if (kTabPointSize > 0 && t && SUCCEEDED(XuiObjectGetPropertyId(t, L"PointSize", &prop)))
            {
                XUIElementPropVal v;
                XUIElementPropVal_SetFloat(&v, kTabPointSize);
                XuiObjectSetProperty(t, prop, 0, &v);
            }
        }
        HXUIOBJ header = Find(Child(m_hObj, L"Tab2"), L"txt_Header");
        if (header)
            XuiControlSetText(header, L"Xbox Live");
        Log("1888 blades: Xbox Live header %s", header ? "set" : "MISSING");
    }

    int CurrentBlade()
    {
        HXUIOBJ tab = NULL;
        if (FAILED(XuiTabSceneGetCurrentTab(m_hObj, &tab)) || !tab)
            return 0;
        std::wstring id = ElementId(tab);
        return id.compare(0, 3, L"Tab") == 0 ? _wtoi(id.c_str() + 3) : 0;
    }

    // How long the blade's <n><what> range plays, in ms (XUI's timelines: 60 frames a second).
    DWORD RangeMs(const wchar_t* what)
    {
        wchar_t from[24], to[24];
        _snwprintf_s(from, 24, _TRUNCATE, L"%d%s", m_blade, what);
        _snwprintf_s(to, 24, _TRUNCATE, L"%d%sEnd", m_blade, what);
        int a = -1, b = -1;
        if (FAILED(XuiElementFindNamedFrame(m_hObj, from, &a)) || FAILED(XuiElementFindNamedFrame(m_hObj, to, &b)) ||
            b < a)
            return 0;
        return (DWORD)(b - a) * 1000 / 60;
    }

    void Play(const wchar_t* what)
    {
        wchar_t from[24], to[24];
        _snwprintf_s(from, 24, _TRUNCATE, L"%d%s", m_blade, what);
        _snwprintf_s(to, 24, _TRUNCATE, L"%d%sEnd", m_blade, what);
        HRESULT hr = PlayRange(m_hObj, from, to, false);
        Log("1888 blades: %S: %08X at %u", from, (unsigned)hr, (unsigned)GetTickCount());
    }

    // Back to the blades from a screen opened over them (B): XUI's own
    // "back to" transition on this scene found none of its frames here (the
    // strip has no TransBackTo range) and left it at frame 0, the blades'
    // home pose; the level timer's <n>Close then started up to 100 ms later
    // from the open pose, so the strip snapped home, jumped back open and
    // slid home again. The
    // close is played here, as the screen goes, and XUI's transition is
    // taken as done.
    HRESULT OnTransitionStart(XUIMessageTransition* t, BOOL& bHandled)
    {
        if (t->dwTransType != XUI_TRANSITION_BACKTO || m_level == 0)
            return S_OK;
        LevelTo(0);
        m_levels.clear();
        m_target = 0;
        t->bStartedSelfTransition = FALSE;
        bHandled = TRUE;
        return S_OK;
    }

    HRESULT OnNotifyPress(HXUIOBJ source, BOOL& bHandled)
    {
        // The Marketplace blade's (6717's) Sign In: the same as the blades' Sign In card.
        if (ElementId(source) == L"btnSignIn" && !HudOpen())
        {
            OpenSignIn();
            bHandled = TRUE;
        }
        else if (OpenScreen(source))
            bHandled = TRUE;
        return S_OK;
    }

    // A on the "Sign In" card, if it reaches here.
    HRESULT OnKeyDown(XUIMessageInput* in, BOOL& bHandled)
    {
        // System is the last blade: the tab after it is 1888's first-run setup
        // (scOOBE), which the tab scene would move to on RB or right, focus
        // going into it unseen (System still showing).
        if (!HudOpen() && (in->dwKeyCode == VK_PAD_RSHOULDER || in->dwKeyCode == VK_PAD_DPAD_RIGHT ||
                           in->dwKeyCode == VK_PAD_LTHUMB_RIGHT))
        {
            wchar_t next[16];
            _snwprintf_s(next, 16, _TRUNCATE, L"Tab%d", CurrentBlade() + 1);
            HXUIOBJ tab = Child(m_hObj, next);
            if (tab && Find(tab, L"scOOBE"))
            {
                bHandled = TRUE;
                return S_OK;
            }
        }
        if (HudOpen() || in->dwKeyCode != VK_PAD_A)
            return S_OK;
        std::wstring focused = ElementId(XuiElementGetFocus());
        if (focused == L"fakeGamerCard")
        {
            OpenSignIn();
            bHandled = TRUE;
        }
        else if (focused == L"ctlGamerCard" && SignedInUser() >= 0)
        {
            OpenGamerProfile();         // 1888's own, in the Guide
            bHandled = TRUE;
        }
        return S_OK;
    }

    // The screen in front: the blades (this scene) or one opened over them;
    // NULL while the Guide's host has the focus.
    HXUIOBJ FrontScene()
    {
        for (HXUIOBJ o = XuiElementGetFocus(); o; XuiElementGetParent(o, &o))
        {
            HXUIOBJ parent = NULL;
            XuiElementGetParent(o, &parent);
            if (parent == g_layer)
                return o == g_hud ? NULL : o;
        }
        return m_hObj;
    }

    // How deep the focus is: 0 on the blades, 1 in a screen opened from them,
    // 2 in one opened from that. Its changes play the frames.
    int Depth()
    {
        HXUIOBJ top = NULL;
        for (HXUIOBJ o = XuiElementGetFocus(); o; XuiElementGetParent(o, &o))
        {
            HXUIOBJ parent = NULL;
            XuiElementGetParent(o, &parent);
            if (parent == g_layer)
            {
                top = o;
                break;
            }
        }
        if (!top || top == m_hObj || top == g_hud)
            return top == g_hud ? m_level : 0;
        int depth = 1;
        for (HXUIOBJ s = XuiSceneGetBackScene(top); s && s != m_hObj && depth < 8; s = XuiSceneGetBackScene(s))
            depth++;
        return depth;
    }

    HRESULT OnTimer(XUIMessageTimer* t, BOOL& bHandled)
    {
        if (t->nId == kOpenGameTimer)
        {
            // The Xbox theme's game screen (blades.opengame): asked again
            // every half second until the library's there, 15 s at most.
            if (OpenGameFromBladesSetting(m_hObj) || ++m_openGameTries >= 30)
                XuiKillTimer(m_hObj, kOpenGameTimer);
            bHandled = TRUE;
            return S_OK;
        }
        if (t->nId == kBootTimer)
        {
            // dashmain's own boot sequence: the blades build up from the
            // "Default" frame. Games: offline (BootLive needs Xbox Live,
            // BootOOBE is the first-run setup).
            XuiKillTimer(m_hObj, kBootTimer);
            UseM6Visuals();
            // As dash.xex chooses (920A5800): the console's retail flags (XConfig
            // user setting 12). 0x40 clear: the first-run setup (BootOOBE);
            // otherwise, started for no particular reason (as SlopDash is),
            // BootLive with 0x4 set, else BootGames.
            DWORD flags = xconfig::ReadU32(xconfig::kUser, xconfig::kRetailFlags, 0x40);
            const wchar_t* from = !(flags & 0x40) ? L"BootOOBE" : (flags & 0x4) ? L"BootLive" : L"BootGames";
            std::wstring to = std::wstring(L"End") + from;
            HRESULT hr = PlayRange(m_hObj, from, to.c_str(), false);
            Log("1888 blades: boot %S (retail flags %08X): %08X", from, (unsigned)flags, (unsigned)hr);
            bHandled = TRUE;
            return S_OK;
        }
        if (t->nId == kRevealTimer)
        {
            bHandled = TRUE;
            if (!g_reveal)
            {
                XuiKillTimer(m_hObj, kRevealTimer);
                return S_OK;
            }
            DWORD now = GetTickCount();
            DWORD start = g_revealFrom ? g_revealFrom : g_revealOpened + kRevealLongest;
            if (now - g_revealOpened >= kRevealLongest && start > now)
                start = now;                // (shown by then whatever happens)
            float a = now < start ? 0.0f : (float)(now - start) / kRevealFade;
            RevealAt(a > 1.0f ? 1.0f : a);
            if (a > 0.0f && g_revealFrom && now - start < 20)
                Log("1888 reveal: showing %u ms after it opened", (unsigned)(now - g_revealOpened));
            if (a >= 1.0f)
            {
                g_reveal = NULL;
                g_revealKids.clear();
                XuiKillTimer(m_hObj, kRevealTimer);
            }
            return S_OK;
        }
        if (t->nId == kRefocusTimer)
        {
            XuiKillTimer(m_hObj, kRefocusTimer);
            if (g_openedFocus && Depth() == 0 && XuiElementGetFocus() == m_hObj)
            {
                HRESULT hr = XuiElementSetFocus(g_openedFocus);
                Log("1888 blades: focus back to %S: %08X", ElementId(g_openedFocus).c_str(), (unsigned)hr);
            }
            g_openedFocus = NULL;
            bHandled = TRUE;
            return S_OK;
        }
        if (t->nId != kLevelTimer)
            return S_OK;
        bHandled = TRUE;
        if (++m_legendTicks % 3 == 0)
            TidyLegends(FrontScene());
        BladeSoundTick(m_hObj);         // (the switched-to blade's controls quiet as the focus lands)
        int level = LevelNow();         // (kept up to date even while held)
        if (GetTickCount() < g_levelHoldUntil)
            return S_OK;
        LevelTo(level);
        return S_OK;
    }

    // The level the blades should show (the blades only on the main menu). Each
    // screen opened is a level deeper than
    // the one it opened from, the third level's look (blades away) at most,
    // so a screen opened from a full-screen one keeps that look (HDTV Settings
    // brought them back: the back-scene chain skipped Display). Going back,
    // the level of the screen the focus is back in.
    //
    // A screen is the outermost named element the focus is in under the
    // layer (XUI wraps screens in unnamed elements while they move, and takes
    // the one left behind off the layer: the layer itself can't be followed).
    // The focus is in a new screen as soon as it opens; one not met before is
    // a level deeper than where the blades were, one met before keeps its level.
    std::map<HXUIOBJ, int> m_levels;    // the screens met, and their levels
    int m_target;

    // The screen the focus is in; the blades (this scene); NULL for the Guide.
    HXUIOBJ FocusScreen()
    {
        HXUIOBJ screen = NULL;
        for (HXUIOBJ o = XuiElementGetFocus(); o; XuiElementGetParent(o, &o))
        {
            if (o == m_hObj)
                return m_hObj;
            if (o == g_hud)
                return NULL;
            if (o == g_layer)
                break;
            if (!ElementId(o).empty())
                screen = o;
        }
        return screen;
    }

    int LevelNow()
    {
        HXUIOBJ screen = FocusScreen();
        if (!screen)
            return m_target;
        if (screen == m_hObj)
        {
            m_levels.clear();           // all closed
            m_target = 0;
            return 0;
        }
        std::map<HXUIOBJ, int>::const_iterator known = m_levels.find(screen);
        if (known != m_levels.end())
            m_target = known->second;   // back, or still there
        else
        {
            m_target = m_target < 2 ? m_target + 1 : 2;
            m_levels[screen] = m_target;
            Log("1888 blades: screen %S, level %d", ElementId(screen).c_str(), m_target);
        }
        return m_target;
    }

    // The frames for a change of depth (0 the blades, 1 a screen opened from
    // them, 2 one opened from that).
    void LevelTo(int depth)
    {
        if (depth == m_level)
            return;
        if (depth > m_level + 1)
            depth = m_level + 1;        // one level at a time (the next tick goes on)
        Log("1888 blades: level %d -> %d", m_level, depth);
        if (!m_blade)
            m_blade = CurrentBlade();
        if (m_level == 0 && depth >= 1)
        {
            Play(L"Open");
            if (g_reveal)
            {
                DWORD ms = RangeMs(L"Open");
                g_revealFrom = GetTickCount() + ms;
                Log("1888 reveal: Open plays %u ms, %u ms after the screen opened", (unsigned)ms,
                    (unsigned)(GetTickCount() - g_revealOpened));
            }
        }
        else if (m_level == 1 && depth == 2)
            Play(L"OpenFull");
        else if (m_level == 2 && depth == 1)
            Play(L"CloseFull");
        else if (depth == 0)
            Play(L"Close");
        m_level = depth;
        if (!depth)
        {
            if (g_reveal)                   // (gone before it showed)
            {
                g_reveal = NULL;
                g_revealKids.clear();
                XuiKillTimer(m_hObj, kRevealTimer);
            }
            m_blade = 0;
            m_closedAt = GetTickCount();
            ShowBladeAgain();
            // Back on the blades the focus was left on the root scene (the
            // button was hidden as the screen closed): nothing highlighted, and
            // up/down did nothing (0.0.109). The button that opened it again
            // (and OnSetFocus keeps it there as the back navigation finishes);
            // checked again a moment later too.
            if (g_openedFocus)
            {
                XuiElementSetFocus(g_openedFocus);
                XuiSetTimer(m_hObj, kRefocusTimer, 1200);
            }
        }
    }

    int Level() const { return m_level; }

    // Screens opened over one another at once: their levels as if opened in
    // turn (the level timer then plays Open, then OpenFull).
    void Opened(HXUIOBJ first, HXUIOBJ second)
    {
        if (first)
            m_levels[first] = 1;
        if (second)
            m_levels[second] = first ? 2 : 1;
        m_target = second && first ? 2 : 1;
        Log("1888 blades: screens opened at once, levels to %d", m_target);
    }

private:
    int m_level, m_blade;
    int m_openGameTries;            // the Xbox theme's game screen: tries so far (kOpenGameTimer)
    unsigned m_legendTicks;
};


// --- the game library ---------------------------------------------------------------
// Played Games (games#408_GamesList.xur: CGameShowcaseScene, list
// CGameShowcaseListList, rows btn_twoline-icon_RightText: line 1 the name, line
// 2 what kind of game, the icon in image slot 3) holds every game found; Xbox
// Live Arcade > My Arcade Games (arcade#2502_TitleSelectorScene.xur, list
// Arcade::CTitleListCtrl, rows btn_oneline_icon: the name, the icon in slot 0)
// the Arcade ones. A starts the game.
std::vector<size_t> GameIndex(bool arcadeOnly)
{
    std::vector<size_t> out;
    const std::vector<library::Game>& games = Games();
    for (size_t i = 0; i < games.size(); i++)
        if (!arcadeOnly || games[i].kind == library::kArcade)
            out.push_back(i);
    return out;
}

std::wstring Wide(const std::string& s)
{
    std::wstring w;
    for (size_t i = 0; i < s.size(); i++)
        w += (wchar_t)(unsigned char)s[i];
    return w;
}

struct GameRows
{
    std::vector<size_t> index;
    std::vector<std::wstring> names, kinds, icons;
};
std::map<HXUIOBJ, GameRows> g_rows;

GameRows& Rows(HXUIOBJ list, bool arcadeOnly)
{
    GameRows& r = g_rows[list];
    if (r.index.empty())
    {
        const std::vector<library::Game>& games = Games();
        r.index = GameIndex(arcadeOnly);
        for (size_t i = 0; i < r.index.size(); i++)
        {
            const library::Game& g = games[r.index[i]];
            r.names.push_back(g.name);
            r.kinds.push_back(Wide(library::KindName(g.kind)));
            r.icons.push_back(IconLocator(r.index[i]));
        }
    }
    return r;
}

HRESULT GameListMessage(HXUIOBJ obj, XUIMessage* msg, bool arcadeOnly, int iconSlot, int kindSlot)
{
    if (msg->dwMessage == XM_DESTROY)
    {
        g_rows.erase(obj);
        return S_OK;
    }
    GameRows& r = Rows(obj, arcadeOnly);
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = (int)r.index.size();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData && d->iItem >= 0 && d->iItem < (int)r.index.size())
        {
            d->szText = d->iData == 0 ? r.names[d->iItem].c_str() : d->iData == kindSlot ? r.kinds[d->iItem].c_str() : L"";
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = d->iData == iconSlot && d->iItem >= 0 && d->iItem < (int)r.icons.size() ? r.icons[d->iItem].c_str()
                                                                                              : NULL;
            msg->bHandled = TRUE;
        }
    }
    else if ((msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS) ||
             (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_A))
    {
        int i = XuiListGetCurSel(obj, NULL);
        if (i >= 0 && i < (int)r.index.size())
        {
            Log("1888 games: starting %S", r.names[i].c_str());
            LaunchGame(Games()[r.index[i]]);        // returns only if it failed
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

HRESULT PlayedGamesListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return GameListMessage(obj, msg, false, 3, 1);
}

HRESULT ArcadeListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return GameListMessage(obj, msg, true, 0, 1);
}

// The scene around Played Games: its "loading" and "Your games will be listed
// here." give way to the list once there are games.
HRESULT PlayedGamesSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, 0x50474D31, 50);           // "PGM1": once its parts exist
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == 0x50474D31)
    {
        XuiKillTimer(obj, 0x50474D31);
        bool any = !Games().empty();
        Show(obj, L"ctlLoading", false);
        Show(obj, L"labEmpty", !any);
        Show(obj, L"gameList", any);
        HXUIOBJ list = Child(obj, L"gameList");
        if (any && list)
            XuiElementSetFocus(list);
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// --- Memory: the storage devices ------------------------------------------------------
// memory#DeviceSelector.xur (DeviceSelectorScene, list DeviceSelectorList, rows
// btn_3Line_Icon_prgbr): each drive with its free space.
struct Device
{
    std::wstring name, space, icon;
    int used;                       // percent of the drive in use (its bar)
    std::string root;               // "hdd:", "usb:" (the File Manager's root)
};
std::vector<Device> g_devices;

void ScanDevices()
{
    g_devices.clear();
    // 1888's 96 x 96 drive icons:
    // the hard drive's from its music package, USB's from dashcomm.
    static const struct { const char* root; const wchar_t* name; const wchar_t* icon; } kDrives[] = {
        { "hdd:\\", L"Hard Drive", L"section://0,music#ico_96x_HD.png" },
        { "usb:\\", L"USB Storage Device", L"common://ico_96x_USB.png" },
    };
    for (size_t i = 0; i < sizeof(kDrives) / sizeof(kDrives[0]); i++)
    {
        ULARGE_INTEGER freeBytes, total, totalFree;
        if (!GetDiskFreeSpaceExA(kDrives[i].root, &freeBytes, &total, &totalFree))
            continue;
        wchar_t buf[96];
        _snwprintf_s(buf, 96, _TRUNCATE, L"%.1f GB free of %.1f GB", freeBytes.QuadPart / 1073741824.0,
                     total.QuadPart / 1073741824.0);
        int used = total.QuadPart ? (int)(100 - freeBytes.QuadPart * 100 / total.QuadPart) : 0;
        std::string root = kDrives[i].root;
        Device d = { kDrives[i].name, buf, kDrives[i].icon, used, root.substr(0, root.size() - 1) };
        g_devices.push_back(d);
    }
}

HRESULT DeviceListProc(HXUIOBJ, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        ScanDevices();
    else if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = (int)g_devices.size();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData && d->iItem >= 0 && d->iItem < (int)g_devices.size())
        {
            // (the item's visual, btn_3Line_Icon_prgbr, shows data 1 - txt_line4,
            // the top line - and data 2 - txt_line3, by the bar; nothing shows
            // data 0: the name was lost and the space took its place)
            d->szText = d->iData == 1 ? g_devices[d->iItem].name.c_str()
                      : d->iData == 2 ? g_devices[d->iItem].space.c_str() : L"";
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = d->iData == 0 && d->iItem >= 0 && d->iItem < (int)g_devices.size()
                      ? g_devices[d->iItem].icon.c_str() : NULL;
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

bool MemoryDeviceKey(HXUIOBJ obj, XUIMessage* msg, const std::string& root);     // (theme/memory1888.inc)
void OpenCategories(HXUIOBJ from, const std::string& root);

HRESULT DeviceSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    // Y on a device: its Device Options (theme/memory1888.inc).
    if (msg->dwMessage == XM_KEYDOWN)
    {
        HXUIOBJ list = Child(obj, L"list_devices");
        int i = list ? XuiListGetCurSel(list, NULL) : -1;
        if (i >= 0 && i < (int)g_devices.size() && MemoryDeviceKey(obj, msg, g_devices[i].root))
            return S_OK;
    }
    // A on a device: its content, as 1888's Memory (Categories).
    if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
    {
        HXUIOBJ list = Child(obj, L"list_devices");
        int i = list ? XuiListGetCurSel(list, NULL) : -1;
        bool fromList = false;
        for (HXUIOBJ o = ((XUINotify*)msg->pvData)->hObjSource; o && !fromList; XuiElementGetParent(o, &o))
            fromList = o == list;
        if ((fromList || ElementId(((XUINotify*)msg->pvData)->hObjSource) == L"legend_a") && i >= 0 &&
            i < (int)g_devices.size())
        {
            OpenCategories(obj, g_devices[i].root);
            msg->bHandled = TRUE;
            return S_OK;
        }
    }
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, 0x44455631, 50);           // "DEV1"
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == 0x44455631)
    {
        XuiKillTimer(obj, 0x44455631);
        if (g_devices.empty())
            ScanDevices();
        Show(obj, L"labDots", false);
        Show(obj, L"txt_EmptyList", g_devices.empty());
        wchar_t buf[48];
        _snwprintf_s(buf, 48, _TRUNCATE, g_devices.size() == 1 ? L"%u device" : L"%u devices",
                     (unsigned)g_devices.size());
        SetText(obj, L"labTotal", buf);
        HXUIOBJ list = Child(obj, L"list_devices");
        if (list && !g_devices.empty())
            XuiElementSetFocus(list);
        // Y Device Options (theme/memory1888.inc; the File Manager is there).
        SetText(obj, L"legend_y", L"Device Options");
        XuiSetTimer(obj, 0x44455632, 200);          // "DEV2": the bars, once the items exist
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == 0x44455632)
    {
        // Each item's bar (its visual's XuiProgressBar): how full the drive is,
        // which 1888's code set (empty bars).
        XuiKillTimer(obj, 0x44455632);
        HXUIOBJ list = Child(obj, L"list_devices");
        for (size_t i = 0; list && i < g_devices.size(); i++)
        {
            HXUIOBJ item = XuiListGetItemControl(list, (int)i), visual = NULL, bar = NULL;
            if (!item || FAILED(XuiControlGetVisual(item, &visual)) || !visual)
                continue;
            bar = Find(visual, L"XuiProgressBar");
            Log("1888 memory: item %u bar %s", (unsigned)i, bar ? "found" : "MISSING");
            if (bar)
            {
                // The runtime sizes the fill (ProgressBody) by the value; its
                // visual (prgbr_Memory) keeps the fill's parts - Left_end,
                // Bar_mid, rt_end - hidden, which 1888's code showed.
                XuiProgressBarSetRange(bar, 0, 100);
                XuiProgressBarSetValue(bar, g_devices[i].used);
                HXUIOBJ bv = NULL;
                XuiControlGetVisual(bar, &bv);
                static const wchar_t* const kParts[] = { L"Left_end", L"Bar_mid", L"rt_end" };
                for (int k = 0; k < 3; k++)
                    if (HXUIOBJ part = Find(bv, kParts[k]))
                        XuiElementSetShow(part, TRUE);
                Log("1888 memory: bar %u: %d%%", (unsigned)i, g_devices[i].used);
            }
        }
        msg->bHandled = TRUE;
    }
    return S_OK;
}


// A message in the console's own box (the Guide's: 1888's style with
// SlopGuide), one OK button: for the settings SlopDash can't act on. Short
//.
void SayBox(const wchar_t* title, const wchar_t* text)
{
    static XOVERLAPPED ov;
    static MESSAGEBOX_RESULT result;
    static LPCWSTR buttons[] = { L"OK" };
    int user = SignedInUser();
    ZeroMemory(&ov, sizeof(ov));
    DWORD r = XShowMessageBoxUI(user >= 0 ? user : 0, title, text, 1, buttons, 0, XMB_ALERTICON, &result, &ov);
    BoxOpened(&ov, r);
    Log("1888 message %S: %u", title, (unsigned)r);
}

// A screen of a package, over the screen `from` is in (Network Settings'
// own screens): as OpenScreen, one level deeper.
bool OpenOver(HXUIOBJ from, const wchar_t* package, const wchar_t* scene)
{
    std::wstring base;
    if (!PackageLocator(package, base))
        return false;
    HXUIOBJ top = NULL;
    for (HXUIOBJ o = from; o; XuiElementGetParent(o, &o))
    {
        HXUIOBJ parent = NULL;
        XuiElementGetParent(o, &parent);
        if (parent == g_layer)
        {
            top = o;
            break;
        }
    }
    HXUIOBJ created = NULL;
    HRESULT hr = top ? XuiSceneCreate(base.c_str(), scene, NULL, &created) : E_FAIL;
    if (SUCCEEDED(hr))
        hr = XuiSceneNavigateForward(top, FALSE, created, XUSER_INDEX_FOCUS);
    Log("1888 open %S over %S: %08X", scene, ElementId(top).c_str(), (unsigned)hr);
    if (SUCCEEDED(hr))
        Blades1888ScreenOpened();
    return SUCCEEDED(hr);
}

std::wstring Dotted(const IN_ADDR& a)
{
    wchar_t b[20];
    _snwprintf_s(b, 20, _TRUNCATE, L"%u.%u.%u.%u", a.S_un.S_un_b.s_b1, a.S_un.S_un_b.s_b2, a.S_un.S_un_b.s_b3,
                 a.S_un.S_un_b.s_b4);
    return b;
}

// The connection as SlopDash sees it (Network Settings > Edit Settings, shown
// only: the console's own network setup is kept as it is).
std::wstring NetworkDetails()
{
    DWORD link = XNetGetEthernetLinkStatus();
    bool up = (link & XNET_ETHERNET_LINK_ACTIVE) != 0;
    XNADDR addr;
    ZeroMemory(&addr, sizeof(addr));
    DWORD got = XNetGetTitleXnAddr(&addr);
    bool haveIp = got != XNET_GET_XNADDR_PENDING && addr.ina.s_addr;
    wchar_t b[512];
    _snwprintf_s(b, 512, _TRUNCATE,
                 L"Network: %s\r\nIP Settings: %s\r\nIP Address: %s\r\n"
                 L"MAC Address: %02X-%02X-%02X-%02X-%02X-%02X\r\nSpeed: %s",
                 !up ? L"Disconnected" : (link & XNET_ETHERNET_LINK_WIRELESS) ? L"Wireless" : L"Wired",
                 (got & XNET_GET_XNADDR_DHCP) ? L"Automatic" : (got & XNET_GET_XNADDR_STATIC) ? L"Manual" : L"Unknown",
                 haveIp ? Dotted(addr.ina).c_str() : L"none", addr.abEnet[0], addr.abEnet[1], addr.abEnet[2],
                 addr.abEnet[3], addr.abEnet[4], addr.abEnet[5],
                 !up ? L"-"
                            : (link & XNET_ETHERNET_LINK_100MBPS) ? L"100 Mbps" : L"10 Mbps");
    return b;
}

// --- Test Connection (network#2010_TestingNetwork.xur, C2010_TestingNetwork) ----------
// 1888's own test screen and rows, with SlopDash's own tests: the adapter, the
// wireless network, the IP address, DNS (consolemods.org), and in Xbox Live's
// row the internet (an HTTPS request to ConsoleMods, as the store makes). MTU,
// ICMP and NAT were Xbox Live's own checks: "Not Run" (1888's words).
volatile LONG g_netTest = 0;            // 0 idle, 1 running, 2 done
volatile LONG g_dnsOk = 0, g_webOk = 0;

DWORD WINAPI NetTestThread(LPVOID)
{
    unsigned sock = 0;
    std::string err;
    g_dnsOk = NetStartup() && NetConnect("consolemods.org", 443, sock, err) ? 1 : 0;
    if (g_dnsOk)
        NetClose(sock);
    std::vector<unsigned char> body;
    int status = 0;
    g_webOk = g_dnsOk && HttpsGet("https://consolemods.org/wiki/images/0/0a/ConsoleModsRepo.ini", body, status, err,
                                  64 * 1024) ? 1 : 0;
    Log("network test: dns %d, internet %d (%d)%s%s", (int)g_dnsOk, (int)g_webOk, status, err.empty() ? "" : " ",
        err.c_str());
    g_netTest = 2;
    return 0;
}

void TestRow(HXUIOBJ scene, int row, int result, const wchar_t* text = NULL)
{
    // result: 1 passed, 0 failed, -1 not run, -2 running
    wchar_t id[16];
    _snwprintf_s(id, 16, _TRUNCATE, L"scnTestItem%d", row);
    HXUIOBJ item = Child(scene, id);
    if (!item)
        return;
    HXUIOBJ pass = Child(item, L"txtPassPass"), fail = Child(item, L"txtPassFail"), spin = Child(item, L"loadingSpin");
    if (spin)
        XuiElementSetShow(spin, result == -2);
    if (pass)
    {
        if (result == 1 && text)
            XuiControlSetText(pass, text);
        XuiElementSetShow(pass, result == 1);
    }
    if (fail)
    {
        if (result != 1 && result != -2)
            XuiControlSetText(fail, text ? text : result == -1 ? L"Not Run" : L"Failed");
        XuiElementSetShow(fail, result == 0 || result == -1 || (result == 1 && !pass));
        if (result == 1 && !pass && text)
            XuiControlSetText(fail, text);
    }
    if (HXUIOBJ d = Child(item, L"txtItemDescription"))
        XuiElementSetOpacity(d, result == -2 ? 0.5f : 1.0f);
}

void FillNetTest(HXUIOBJ obj)
{
    DWORD link = XNetGetEthernetLinkStatus();
    bool up = (link & XNET_ETHERNET_LINK_ACTIVE) != 0, wireless = (link & XNET_ETHERNET_LINK_WIRELESS) != 0;
    XNADDR addr;
    ZeroMemory(&addr, sizeof(addr));
    DWORD got = XNetGetTitleXnAddr(&addr);
    bool ip = got != XNET_GET_XNADDR_PENDING && addr.ina.s_addr;
    // (the adapter's row: its kind where it passed, else Failed; 1888 put the
    // kind in labDeviceType over the same spot)
    SetText(obj, L"labDeviceType", L"");
    TestRow(obj, 0, up ? 1 : 0, up ? (wireless ? L"Wireless" : L"Wired") : L"Failed");
    TestRow(obj, 1, wireless ? (up ? 1 : 0) : 1, wireless ? NULL : L"Not Used");
    TestRow(obj, 2, ip ? 1 : 0, ip ? Dotted(addr.ina).c_str() : NULL);
    bool done = g_netTest == 2;
    TestRow(obj, 3, done ? (g_dnsOk ? 1 : 0) : -2, done && g_dnsOk ? L"Confirmed" : NULL);
    TestRow(obj, 4, -1);
    TestRow(obj, 5, -1);
    TestRow(obj, 6, done ? (g_webOk ? 1 : 0) : -2, done && g_webOk ? L"Connected" : NULL);
    TestRow(obj, 7, -1);
    SetText(obj, L"labNatType", L"");
}

const DWORD kNetTestTimer = 0x4E544531;     // "NTE1"

HRESULT NetTestProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        XuiSetTimer(obj, kNetTestTimer, 30);
        if (g_netTest != 1)
        {
            g_netTest = 1;
            HANDLE t = CreateThread(NULL, 256 * 1024, NetTestThread, NULL, 0, NULL);
            if (t)
                CloseHandle(t);
            else
                g_netTest = 2;
        }
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kNetTestTimer)
    {
        XuiKillTimer(obj, kNetTestTimer);
        // Xbox Live's row is the internet's here; the per-row "More Info" and
        // the Xbox Dashboard / Start legends aren't used.
        if (HXUIOBJ live = Child(obj, L"scnTestItem6"))
        {
            Show(live, L"LiveLogo", false);
            Show(live, L"txtItemDescription", true);
            SetText(live, L"txtItemDescription", L"Internet");
        }
        SetText(obj, L"txt_Header", L"Test Connection");
        for (int i = 0; i < 8; i++)
        {
            wchar_t id[16];
            _snwprintf_s(id, 16, _TRUNCATE, L"btn_Test%d", i);
            Show(obj, id, false);
        }
        SetText(obj, L"legend_y", L"");
        SetText(obj, L"legend_x", L"");
        SetText(obj, L"legend_a", L"");
        FillNetTest(obj);
        XuiElementSetFocus(obj);
        if (g_netTest != 2)
            XuiSetTimer(obj, kNetTestTimer + 1, 250);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kNetTestTimer + 1)
    {
        FillNetTest(obj);
        if (g_netTest == 2)
            XuiKillTimer(obj, kNetTestTimer + 1);
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// Edit Settings (network#2012_TestInformation.xur's text page): the details.
HRESULT NetInfoProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kNetTestTimer + 2, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kNetTestTimer + 2)
    {
        XuiKillTimer(obj, kNetTestTimer + 2);
        SetText(obj, L"txt_Header", L"Network Details");
        if (HXUIOBJ edit = Child(obj, L"edit_TF"))
            XuiControlSetText(edit, NetworkDetails().c_str());
        Show(obj, L"cntlLoader", false);
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// Network Settings' list (network#ConnStatus.xur: Test Xbox Live Connection,
// Test Media Connection, Edit Settings, Windows Connect Now, Restore to Factory
// Defaults). Windows Connect Now (2005's wireless setup from a PC's USB stick,
// never supported here) is Join a Service: a placeholder until XLink Kai comes
// later. The order: Join a Service
// above Edit Settings, and no Restore to Factory Defaults. Renamed in place
// (setting its ItemsText at runtime crashed, 0.0.52), the last row deleted.
const int kVpnItem = 2, kEditItem = 3, kNetworkRows = 4;

void NetworkAction(HXUIOBJ obj, int item)
{
    switch (item)
    {
    case 0: OpenOver(obj, L"network", L"2010_TestingNetwork.xur"); break;
    case 1: SayBox(L"Test Media Connection", L"Cannot Test. Media Streaming Not Supported"); break;
    case kVpnItem: OpenJoinService(obj); break;          // (theme/services_screen.inc)
    case kEditItem: OpenOver(obj, L"network", L"2012_TestInformation.xur"); break;
    }
}

// --- Network Settings: the connection's status -----------------------------------------
// network#ConnStatus.xur (CConnStatus): link and activity lights, the network,
// and the console's address in the panel.
HRESULT ConnStatusProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, 0x434E5331, 50);           // "CNS1"
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == 0x434E5331)
    {
        XuiKillTimer(obj, 0x434E5331);
        DWORD link = XNetGetEthernetLinkStatus();
        bool up = (link & XNET_ETHERNET_LINK_ACTIVE) != 0;
        bool wireless = (link & XNET_ETHERNET_LINK_WIRELESS) != 0;
        Show(obj, L"img_Link_Connected", up);
        Show(obj, L"img_Link_Disconnected", !up);
        Show(obj, L"img_Act", up);
        Show(obj, L"img_inAct", !up);
        for (int i = 1; i <= 4; i++)
        {
            wchar_t id[16];
            _snwprintf_s(id, 16, _TRUNCATE, L"img_Signal%d", i);
            Show(obj, id, wireless && up);
        }
        SetText(obj, L"txt_SSID", up ? (wireless ? L"Network: Wireless" : L"Network: Wired") : L"Network: Not connected");
        // The panel: the connection's details (what Edit Settings shows, at a glance).
        HXUIOBJ main = Child(obj, L"scene_main");
        SetText(main, L"txt_MetaBody", NetworkDetails().c_str());
        Show(obj, L"ctlLoading", false);
        HXUIOBJ list = Find(obj, L"list_items");
        LPCWSTR was = list && XuiListGetItemCount(list) > kVpnItem ? XuiListGetText(list, kVpnItem) : NULL;
        if (was && wcscmp(was, L"Join a Service"))
        {
            HRESULT a = XuiListSetText(list, kVpnItem, L"Join a Service");
            HRESULT b = XuiListSetText(list, kEditItem, L"Edit Settings");
            HRESULT c = XuiListGetItemCount(list) > kNetworkRows
                            ? XuiListDeleteItems(list, kNetworkRows, XuiListGetItemCount(list) - kNetworkRows)
                            : S_OK;
            Log("1888 network: Join a Service, Edit Settings, no Restore: %08X %08X %08X (%d rows)", (unsigned)a,
                (unsigned)b, (unsigned)c, XuiListGetItemCount(list));
        }
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
    {
        HXUIOBJ list = Find(obj, L"list_items");
        bool fromList = false;
        for (HXUIOBJ o = ((XUINotify*)msg->pvData)->hObjSource; o && !fromList; XuiElementGetParent(o, &o))
            fromList = o == list;
        if (fromList && !HudOpen())
        {
            NetworkAction(obj, XuiListGetCurSel(list, NULL));
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// --- Achievements (games#402_Achievements.xur) ----------------------------------------
// CAchievementsGridScene: the profile's games on the left (TitleList, rows
// btn_Achievements: the game's picture, image data 0), the selected one's
// achievements in a grid (AchievementList, btn_Grid_64xImage: the picture,
// data 0), and above them the header (labGameTitle, labAchievementName,
// labAcquiredDate, labPoints with the GamerScore icon). 1888's dash code for it isn't on the
// console; the texts are its own (games#strings.xus: 0 "All Games", 1 "%u",
// 2 "%u achievements", 3 "%u / %u"; the date as 402_Achievements.xur's own
// sample, 11/30/2006). The data is the profile's (platform/achievements).
// First row: All Games (every earned achievement); then each game, the last
// played first.
const DWORD kAchLoadTimer = 0x41434831;        // "ACH1"
std::vector<size_t> g_achGrid;                  // the grid's achievements (AchievementData::items)
int g_achTitle = 0;                             // the TitleList row shown in the grid

std::wstring Fmt(const wchar_t* f, unsigned a, unsigned b = 0)
{
    wchar_t buf[64];
    _snwprintf_s(buf, 64, _TRUNCATE, f, a, b);
    return buf;
}

void FillGrid(int row)
{
    const AchievementData& d = CurrentAchievements();
    g_achTitle = row;
    g_achGrid.clear();
    if (row == 0)
    {
        int noPicture = 0;
        for (size_t i = 0; i < d.items.size(); i++)
            if (d.items[i].a.Earned())
            {
                g_achGrid.push_back(i);
                if (d.items[i].png.empty())
                    noPicture++;
            }
        Log("1888 achievements: %u earned in the titles' records, %u earned achievements found (%d without a picture)",
            (unsigned)d.unlocked, (unsigned)g_achGrid.size(), noPicture);
    }
    else if (row - 1 < (int)d.titles.size())
        g_achGrid = d.titles[row - 1].items;
}

void AchTitleHeader(HXUIOBJ scene, int row)
{
    const AchievementData& d = CurrentAchievements();
    if (row == 0)
    {
        SetText(scene, L"labGameTitle", L"All Games");
        SetText(scene, L"labAchievementName", Fmt(L"%u achievements", (unsigned)d.unlocked).c_str());
        SetText(scene, L"labPoints", Fmt(L"%u / %u", (unsigned)d.earned, (unsigned)d.gamerscore).c_str());
    }
    else if (row - 1 < (int)d.titles.size())
    {
        const xdbf::Title& t = d.titles[row - 1].info;
        SetText(scene, L"labGameTitle", t.name.c_str());
        SetText(scene, L"labAchievementName", Fmt(L"%u achievements", (unsigned)t.unlocked).c_str());
        SetText(scene, L"labPoints", Fmt(L"%u / %u", (unsigned)t.earned, (unsigned)t.gamerscore).c_str());
    }
    SetText(scene, L"labAcquiredDate", L"");
}

void AchItemHeader(HXUIOBJ scene, int cell)
{
    const AchievementData& d = CurrentAchievements();
    if (cell < 0 || cell >= (int)g_achGrid.size())
        return;
    const AchItem& it = d.items[g_achGrid[cell]];
    const xdbf::Achievement& a = it.a;
    bool known = a.Earned() || a.ShownLocked();
    SetText(scene, L"labGameTitle", d.titles[it.title].info.name.c_str());
    SetText(scene, L"labAchievementName", known ? a.name.c_str() : L"");
    SetText(scene, L"labPoints", Fmt(L"%u", (unsigned)a.gamerscore).c_str());
    std::wstring date;
    if (a.Earned() && a.unlocked)
    {
        FILETIME ft, local;
        ft.dwHighDateTime = (DWORD)(a.unlocked >> 32);
        ft.dwLowDateTime = (DWORD)a.unlocked;
        SYSTEMTIME st;
        if (FileTimeToLocalFileTime(&ft, &local) && FileTimeToSystemTime(&local, &st))
        {
            wchar_t b[32];
            _snwprintf_s(b, 32, _TRUNCATE, L"%u/%u/%u", st.wMonth, st.wDay, st.wYear);
            date = b;
        }
    }
    SetText(scene, L"labAcquiredDate", date.c_str());
    // No description here: metaPanelScene's visual (metaScene_Achievements) is
    // the piece joining the chosen game to the grid; its text sits over the
    // grid's top row (0.0.99), and 1888's screen showed none there.
}

void SetListCount(HXUIOBJ list, int now)
{
    int had = XuiListGetItemCount(list);
    if (had > 0)
        XuiListDeleteItems(list, 0, had);
    if (now > 0)
    {
        XuiListInsertItems(list, 0, now);
        XuiListSetCurSel(list, 0);
    }
}

void LoadAchievementScreen(HXUIOBJ scene)
{
    int user = SignedInUser();
    XUID xuid = 0;
    AchievementData& d = CurrentAchievements();
    bool ok = user >= 0 && XUserGetXUID(user, &xuid) == ERROR_SUCCESS && LoadAchievements(xuid, d);
    if (!ok)
        d = AchievementData();
    Show(scene, L"ctlLoading", false);
    Show(scene, L"labEmpty", d.titles.empty());
    HXUIOBJ gs = Child(scene, L"GamerScore");
    if (gs)
        XuiImageElementSetImagePath(gs, L"sharedres://GScore.png");
    HXUIOBJ titles = Child(scene, L"TitleList"), grid = Child(scene, L"AchievementList");
    if (titles && grid)
    {
        SetStringProperty(titles, L"NavRight", L"AchievementList");
        SetStringProperty(grid, L"NavLeft", L"TitleList");
    }
    FillGrid(0);
    if (titles)
        SetListCount(titles, d.titles.empty() ? 0 : (int)d.titles.size() + 1);
    if (grid)
        SetListCount(grid, (int)g_achGrid.size());
    AchTitleHeader(scene, 0);
    if (titles && !d.titles.empty())
        XuiElementSetFocus(titles);
}

HRESULT AchievementsSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        g_achGrid.clear();
        g_achTitle = 0;
        XuiSetTimer(obj, kAchLoadTimer, 50);        // the loading spinner shows meanwhile
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kAchLoadTimer)
    {
        XuiKillTimer(obj, kAchLoadTimer);
        LoadAchievementScreen(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY)
    {
        XUINotify* n = (XUINotify*)msg->pvData;
        std::wstring id = ElementId(n->hObjSource);
        if (id == L"TitleList" && (n->dwNotify == XN_SELCHANGED || n->dwNotify == XN_SET_FOCUS))
        {
            int row = XuiListGetCurSel(n->hObjSource, NULL);
            if (row >= 0 && row != g_achTitle)
            {
                FillGrid(row);
                HXUIOBJ grid = Child(obj, L"AchievementList");
                if (grid)
                    SetListCount(grid, (int)g_achGrid.size());
            }
            AchTitleHeader(obj, row < 0 ? 0 : row);
        }
        else if (id == L"AchievementList" && (n->dwNotify == XN_SELCHANGED || n->dwNotify == XN_SET_FOCUS))
            AchItemHeader(obj, XuiListGetCurSel(n->hObjSource, NULL));
    }
    return S_OK;
}

HRESULT ListImageAnswer(XUIMessage* msg, int count, std::wstring (*path)(int))
{
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = count;
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        static std::wstring held;           // XUI copies the path before the next ask
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = NULL;
            if (d->iData == 0 && d->iItem >= 0 && d->iItem < count)
            {
                held = path(d->iItem);
                d->szPath = held.empty() ? NULL : held.c_str();
            }
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData)
        {
            d->szText = L"";            // not the visual's "Item #1"
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

std::wstring TitleRowImage(int row)
{
    // All Games: xam's shared controller picture; then each game's own.
    if (row == 0)
        return L"sharedres://ico_64x_controller.png";
    const AchievementData& d = CurrentAchievements();
    return row - 1 < (int)d.titles.size() && !d.titles[row - 1].tile.empty() ? AchTitleLocator(row - 1) : L"";
}

std::wstring GridCellImage(int cell)
{
    const AchievementData& d = CurrentAchievements();
    if (cell >= (int)g_achGrid.size())
        return L"";
    const AchItem& it = d.items[g_achGrid[cell]];
    if (it.a.Earned() && !it.png.empty())
        return AchItemLocator(g_achGrid[cell]);
    // Locked: xam's own pictures for it (shrdres: unearnedAchievement.png, or
    // secretAchievement.png for one that stays hidden until it's earned).
    return it.a.ShownLocked() || it.a.Earned() ? L"sharedres://unearnedAchievement.png"
                                               : L"sharedres://secretAchievement.png";
}

HRESULT AchTitleListProc(HXUIOBJ, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    const AchievementData& d = CurrentAchievements();
    return ListImageAnswer(msg, d.titles.empty() ? 0 : (int)d.titles.size() + 1, TitleRowImage);
}

HRESULT AchGridListProc(HXUIOBJ, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return ListImageAnswer(msg, (int)g_achGrid.size(), GridCellImage);
}

// --- Pictures (pictures#900_PicturesMain, 905_IndividualDeviceMain, slideshowControls) ----
// 1888's own screens, which its code filled: the sources (PhotoAreaMainScene),
// a source's pictures in a grid (DeviceMainScene and its GridList, with
// "%u of %u" and "No pictures found.": pictures strings 8 and 10), and one
// picture full screen (FullScreenPhotoScene: its transport's Play, Stop,
// Previous, Next, Shuffle, Repeat and Rotate, LB / RB, and the help line in
// 1888's words, strings 12-27). The scene's hidden fifth source row shows
// SlopDash's Screenshots; Computer can't connect (as System > Computers).
struct PictureRoot
{
    const wchar_t* button;
    const wchar_t* name;
    const char* root;
    int depth;
    bool videos;                    // the recorder's clips (pictures::Find)
};
// Recordings (the Screen Recorder's clips, above Screenshots): SlopGuide
// records them to Hdd:\SlopDash\Recordings
// (plugin/Recorder.inc); the viewer plays them (ShowPicture). Screenshots:
// SlopGuide's, taken from the Guide (2026-10-22), one folder per game.
const PictureRoot kPictureRoots[] = {
    { L"btnOption2", L"Digital Camera", "usb:\\DCIM", 2, false },
    { L"btnOption3", L"Current Disc", "dvd:", 3, false },
    { L"btnOption4", L"Portable Device", "usb:", 2, false },
    { L"btnOption5", L"Recordings", "hdd:\\SlopDash\\Recordings", 0, true },
    { L"btnOption6", L"Screenshots", "hdd:\\SlopDash\\Screenshots", 1, false },
};
std::wstring g_pictureSource;
int g_pictureAt = 0;                // the picture the viewer shows
bool g_slideshow = false;           // the viewer moving on by itself
const DWORD kPicturesTimer = 0x50494331;    // "PIC1": once a pictures screen's parts exist
const DWORD kSlideTimer = 0x50494332;       // "PIC2": the slideshow's next picture
const DWORD kOsuiTimer = 0x50494333;        // "PIC3": the transport fades after a while

bool SetUint(HXUIOBJ obj, const wchar_t* name, unsigned value)
{
    DWORD id = 0;
    if (!obj || FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetUint(&v, value);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

void AddVideosRow(HXUIOBJ scene);     // (Media > Videos: theme/video1888.inc)
bool OpenVideos(HXUIOBJ from);

HRESULT PhotoMainProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kPicturesTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kPicturesTimer)
    {
        XuiKillTimer(obj, kPicturesTimer);
        // The Media blade's Media (Pictures and Video as one entry).
        if (HXUIOBJ header = Child(obj, L"Header_Text"))
            XuiControlSetText(header, L"Media");
        if (HXUIOBJ clips = Child(obj, L"btnOption5"))
        {
            XuiControlSetText(clips, L"Recordings");
            SetStringProperty(clips, L"ImagePath", L"common://ico_32x_DigitalCamera.png");
            XuiElementSetShow(clips, TRUE);
        }
        if (HXUIOBJ shots = Child(obj, L"btnOption6"))
        {
            XuiControlSetText(shots, L"Screenshots");
            SetStringProperty(shots, L"ImagePath", L"common://ico_32x_DigitalCamera.png");
            XuiElementSetShow(shots, TRUE);
        }
        AddVideosRow(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        if (id == L"btnOption1")
        {
            SayBox(L"Computer", L"Cannot Connect. Not Supported");
            msg->bHandled = TRUE;
        }
        else if (id == L"btnVideos")
        {
            OpenVideos(obj);
            msg->bHandled = TRUE;
        }
        for (size_t i = 0; i < ARRAYSIZE(kPictureRoots); i++)
            if (id == kPictureRoots[i].button)
            {
                pictures::Find(kPictureRoots[i].root, kPictureRoots[i].depth, kPictureRoots[i].videos);
                g_pictureSource = kPictureRoots[i].name;
                OpenOver(obj, L"pictures", L"905_IndividualDeviceMain.xur");
                msg->bHandled = TRUE;
            }
    }
    return S_OK;
}

void ShowPictureCount(HXUIOBJ scene, int at)
{
    wchar_t b[32];
    _snwprintf_s(b, 32, _TRUNCATE, L"%u of %u", (unsigned)(at + 1), (unsigned)pictures::Count());
    SetText(scene, L"labelHighlightedOfTotal", pictures::Count() ? b : L"");
}

HRESULT PictureDeviceProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ list = Child(obj, L"List");
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kPicturesTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kPicturesTimer)
    {
        XuiKillTimer(obj, kPicturesTimer);
        bool any = pictures::Count() > 0;
        SetText(obj, L"Header_Text", g_pictureSource.c_str());
        SetText(obj, L"labelMessage", any ? L"" : g_pictureSource == L"Recordings" ? L"No recordings found." :
                                                  L"No pictures found.");
        Show(obj, L"labelMessage", !any);
        Show(obj, L"labelWaitingAnimation", false);
        Show(obj, L"btnPlay", any);
        SetText(obj, L"navX", L"");             // (Apply as Background: later, with the blades' own)
        if (list)
        {
            XuiElementSetShow(list, any);
            if (any)
            {
                XuiListSetCurSel(list, 0);
                XuiElementSetFocus(list);
            }
        }
        ShowPictureCount(obj, 0);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_SELCHANGED && list)
        ShowPictureCount(obj, XuiListGetCurSel(list, NULL));
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        bool fromList = false;
        for (HXUIOBJ o = ((XUINotify*)msg->pvData)->hObjSource; o && list && !fromList; XuiElementGetParent(o, &o))
            fromList = o == list;
        if ((fromList || id == L"btnPlay") && pictures::Count())
        {
            g_pictureAt = fromList ? max(0, XuiListGetCurSel(list, NULL)) : 0;
            g_slideshow = id == L"btnPlay";
            OpenOver(obj, L"pictures", L"slideshowControls.xur");
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

std::wstring PictureThumb(int i)
{
    return pictures::Locator((size_t)i, true);
}

HRESULT PictureGridProc(HXUIOBJ, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return ListImageAnswer(msg, (int)pictures::Count(), PictureThumb);
}

// The viewer. Its picture: an image element of SlopDash's (1888's own
// ImageRenderControl drew it), over a black backdrop as wide as any screen.
bool g_shuffle = false, g_repeat = true;
int g_rotation = 0;                 // quarter turns, clockwise

// Where the picture goes: the part of the scene the screen shows. The 1888
// layer is scaled unevenly (main.cpp FitLaunchDash: x w/1120, y h/660, raised
// 64 px), so a picture fitted in the scene's own units came out cropped top
// and bottom and 5% too wide (the test of 2026-10-14). The picture sits in a
// holder that undoes the uneven part and covers the screen exactly; it turns
// inside the holder, in square units, so a turned picture keeps its shape.
void PlacePicture(HXUIOBJ scene)
{
    HXUIOBJ holder = Child(scene, L"slopPictureHolder"), img = Find(scene, L"slopPicture");
    if (!holder || !img)
        return;
    D3DXVECTOR3 s(1, 1, 1), p(0, 0, 0);
    if (g_layer)
    {
        XuiElementGetScale(g_layer, &s);
        XuiElementGetPosition(g_layer, &p);
    }
    if (s.x <= 0 || s.y <= 0)
        s.x = s.y = 1;
    // (the scene's own place in the layer, if it isn't at its corner)
    for (HXUIOBJ o = scene; o && o != g_layer; XuiElementGetParent(o, &o))
    {
        D3DXVECTOR3 at(0, 0, 0);
        XuiElementGetPosition(o, &at);
        p.x += at.x * s.x;
        p.y += at.y * s.y;
    }
    XVIDEO_MODE m;
    ZeroMemory(&m, sizeof(m));
    XGetVideoMode(&m);
    float screenW = m.dwDisplayWidth ? (float)m.dwDisplayWidth : 1280.0f;
    float screenH = m.dwDisplayHeight ? (float)m.dwDisplayHeight : 720.0f;
    float k = s.x / s.y, visW = screenW / s.x, visH = screenH / s.y;
    float cx = -p.x / s.x + visW / 2, cy = -p.y / s.y + visH / 2;
    float bw = visW * k, bh = visH;
    XuiElementSetBounds(holder, bw, bh);
    D3DXVECTOR3 at(cx - bw / 2, cy - bh / 2, 0), pivot(bw / 2, bh / 2, 0), squeeze(1 / k, 1, 1);
    XuiElementSetPosition(holder, &at);
    XuiElementSetPivot(holder, &pivot);
    XuiElementSetScale(holder, &squeeze);
    bool turned = g_rotation % 2 == 1;
    float iw = turned ? bh : bw, ih = turned ? bw : bh;
    XuiElementSetBounds(img, iw, ih);
    D3DXVECTOR3 iat((bw - iw) / 2, (bh - ih) / 2, 0), ipivot(iw / 2, ih / 2, 0);
    XuiElementSetPosition(img, &iat);
    XuiElementSetPivot(img, &ipivot);
    D3DXQUATERNION q;
    D3DXVECTOR3 axis(0, 0, 1);
    D3DXQuaternionRotationAxis(&q, &axis, g_rotation * D3DX_PI / 2);     // (positive: clockwise on screen)
    XuiElementSetRotation(img, &q);
}

// A recording plays (Media > Recordings: the Screen Recorder's Motion-JPEG
// clips): its frames shown in turn, each the one its time has come to (a
// slow one is skipped, not waited for: the clip keeps its length). Play
// pauses and goes on, Stop goes back to its start; it ends on its last frame.
const DWORD kVideoTimer = 0x50494334;       // "PIC4": a recording's next frame
bool g_videoPlaying = false;
unsigned g_videoFrame = 0, g_videoFrames = 0, g_videoFps = 30;
DWORD g_videoStart = 0;                     // GetTickCount at its frame 0 (paused: moved on)

void VideoFrame(HXUIOBJ scene, unsigned frame)
{
    HXUIOBJ img = Find(scene, L"slopPicture");
    g_videoFrame = frame;
    if (img)
        XuiImageElementSetImagePath(img, pictures::FrameLocator((size_t)g_pictureAt, frame).c_str());
}

void VideoPlay(HXUIOBJ scene, bool play)
{
    g_videoPlaying = play && g_videoFrames > 0;
    if (g_videoPlaying)
    {
        if (g_videoFrame + 1 >= g_videoFrames)
            g_videoFrame = 0;                   // (at its end: from the start)
        g_videoStart = GetTickCount() - g_videoFrame * 1000 / g_videoFps;
        XuiSetTimer(scene, kVideoTimer, max(15u, 1000 / g_videoFps / 2));
    }
    else
        XuiKillTimer(scene, kVideoTimer);
}

void VideoTick(HXUIOBJ scene)
{
    unsigned due = (GetTickCount() - g_videoStart) * g_videoFps / 1000;
    if (due >= g_videoFrames)
    {
        VideoFrame(scene, g_videoFrames - 1);
        VideoPlay(scene, false);                // (the end: its last frame stays)
        return;
    }
    if (due != g_videoFrame)
        VideoFrame(scene, due);
}

void ShowPicture(HXUIOBJ scene)
{
    HXUIOBJ img = Find(scene, L"slopPicture");
    if (!img || !pictures::Count())
        return;
    g_pictureAt = (g_pictureAt % (int)pictures::Count() + (int)pictures::Count()) % (int)pictures::Count();
    g_rotation = 0;
    PlacePicture(scene);
    VideoPlay(scene, false);
    if (pictures::IsVideo((size_t)g_pictureAt))
    {
        g_videoFrames = pictures::VideoFrames((size_t)g_pictureAt);
        g_videoFps = max(1u, pictures::VideoFps((size_t)g_pictureAt));
        VideoFrame(scene, 0);
        VideoPlay(scene, true);
        Log("pictures: playing %S, %u frames at %u a second", pictures::Name((size_t)g_pictureAt).c_str(),
            g_videoFrames, g_videoFps);
        return;
    }
    XuiImageElementSetImagePath(img, pictures::Locator((size_t)g_pictureAt, false).c_str());
    Log("pictures: showing %d of %u, %S", g_pictureAt + 1, (unsigned)pictures::Count(),
        pictures::Name((size_t)g_pictureAt).c_str());
}

void TurnPicture(HXUIOBJ scene, int quarters)
{
    g_rotation = (g_rotation + quarters + 4) % 4;
    PlacePicture(scene);
}

void StepPicture(HXUIOBJ scene, int by)
{
    int n = (int)pictures::Count();
    if (n == 0)
        return;
    if (g_shuffle && n > 1)
    {
        int next = g_pictureAt;
        while (next == g_pictureAt)
            next = rand() % n;
        g_pictureAt = next;
    }
    else if (!g_repeat && g_pictureAt + by >= n)
    {
        g_slideshow = false;                // (the end, with repeat off)
        XuiKillTimer(scene, kSlideTimer);
        return;
    }
    else
        g_pictureAt += by;
    ShowPicture(scene);
}

void ShowTransport(HXUIOBJ scene, bool show)
{
    if (HXUIOBJ t = Child(scene, L"TransportControlsScene"))
        XuiElementSetShow(t, show);
    if (show)
        XuiSetTimer(scene, kOsuiTimer, 4000);
}

void HelpLine(HXUIOBJ scene)
{
    std::wstring id = ElementId(XuiElementGetFocus());
    const wchar_t* text = L"";
    if (id == L"btn_Play")
        text = (pictures::IsVideo((size_t)g_pictureAt) ? g_videoPlaying : g_slideshow) ? L"Pause" : L"Play";
    else if (id == L"btn_Stop")
        text = L"Stop";
    else if (id == L"btn_NextTrack")
        text = L"Go to Next Image";
    else if (id == L"btn_PrevTrack")
        text = L"Go to Previous Image";
    else if (id == L"btn_Shuffle")
        text = g_shuffle ? L"Toggle Shuffle Off" : L"Toggle Shuffle On";
    else if (id == L"btn_Loop")
        text = g_repeat ? L"Toggle Repeat Off" : L"Toggle Repeat On";
    else if (id == L"btn_RotateRight")
        text = L"Rotate Image Clockwise";
    else if (id == L"btn_RotateLeft")
        text = L"Rotate Image Counter-Clockwise";
    if (HXUIOBJ t = Child(scene, L"TransportControlsScene"))
        SetText(t, L"lab_helpText", text);
}

HRESULT PhotoViewerProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kPicturesTimer, 30);
    else if (msg->dwMessage == XM_TIMER)
    {
        DWORD id = ((XUIMessageTimer*)msg->pvData)->nId;
        if (id == kPicturesTimer)
        {
            XuiKillTimer(obj, kPicturesTimer);
            // The backdrop and the picture, behind the transport.
            HXUIOBJ back = NULL, img = NULL;
            if (SUCCEEDED(XuiCreateObject(L"XuiControl", &back)) && back)
            {
                SetStringProperty(back, L"Visual", L"BlackSceneVisual");
                XuiElementSetBounds(back, 4000.0f, 4000.0f);
                D3DXVECTOR3 at(-1440.0f, -1615.0f, 0);
                XuiElementSetPosition(back, &at);
                XuiElementAddChild(obj, back);
                XuiElementSetShow(back, TRUE);
            }
            HXUIOBJ holder = NULL;
            if (SUCCEEDED(XuiCreateObject(L"XuiControl", &holder)) && holder &&
                SUCCEEDED(XuiCreateObject(L"XuiImage", &img)) && img)
            {
                SetStringProperty(holder, L"Id", L"slopPictureHolder");
                SetStringProperty(img, L"Id", L"slopPicture");
                SetUint(img, L"SizeMode", 0x10);         // fit, keeping its shape
                XuiElementAddChild(obj, holder);
                XuiElementAddChild(holder, img);
            }
            if (HXUIOBJ t = Child(obj, L"TransportControlsScene"))
            {
                XuiElementUnlink(t);                    // (the transport on top again)
                XuiElementAddChild(obj, t);
            }
            ShowPicture(obj);
            ShowTransport(obj, true);
            if (g_slideshow)
                XuiSetTimer(obj, kSlideTimer, 5000);
            HelpLine(obj);
        }
        else if (id == kSlideTimer && !g_videoPlaying)
            StepPicture(obj, 1);
        else if (id == kVideoTimer)
            VideoTick(obj);
        else if (id == kOsuiTimer)
        {
            XuiKillTimer(obj, kOsuiTimer);
            ShowTransport(obj, false);
        }
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && !HudOpen())
    {
        DWORD key = ((XUIMessageInput*)msg->pvData)->dwKeyCode;
        HXUIOBJ t = Child(obj, L"TransportControlsScene");
        bool shown = t && XuiElementIsShown(t);
        if (key == VK_PAD_LSHOULDER || key == VK_PAD_RSHOULDER)
        {
            StepPicture(obj, key == VK_PAD_RSHOULDER ? 1 : -1);
            msg->bHandled = TRUE;
        }
        else if (!shown && key != VK_PAD_B)
        {
            ShowTransport(obj, true);           // a press brings the controls back first
            msg->bHandled = TRUE;
        }
        else
            ShowTransport(obj, true);
        HelpLine(obj);
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        bool handled = true;
        if (id == L"btn_Play" && pictures::IsVideo((size_t)g_pictureAt))
            VideoPlay(obj, !g_videoPlaying);    // (a recording: pause and go on)
        else if (id == L"btn_Stop" && pictures::IsVideo((size_t)g_pictureAt))
        {
            VideoPlay(obj, false);
            VideoFrame(obj, 0);
        }
        else if (id == L"btn_Play")
        {
            g_slideshow = !g_slideshow;
            if (g_slideshow)
                XuiSetTimer(obj, kSlideTimer, 5000);
            else
                XuiKillTimer(obj, kSlideTimer);
        }
        else if (id == L"btn_Stop")
        {
            g_slideshow = false;
            XuiKillTimer(obj, kSlideTimer);
        }
        else if (id == L"btn_NextTrack" || id == L"btnRShoulder")
            StepPicture(obj, 1);
        else if (id == L"btn_PrevTrack" || id == L"btnLShoulder")
            StepPicture(obj, -1);
        else if (id == L"btn_Shuffle")
            g_shuffle = !g_shuffle;
        else if (id == L"btn_Loop")
            g_repeat = !g_repeat;
        else if (id == L"btn_RotateRight")
            TurnPicture(obj, 1);
        else if (id == L"btn_RotateLeft")
            TurnPicture(obj, -1);
        else
            handled = false;
        if (handled)
        {
            HelpLine(obj);
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

#include "theme/settings1888.inc"

#include "theme/music1888.inc"
#include "theme/video1888.inc"
#include "theme/memory1888.inc"

const StandInInfo kListClasses[] = {
    { L"CGameShowcaseListList", L"XuiList", 0, { 0 } },
    { L"DeviceSelectorList", L"XuiList", 0, { 0 } },
    { L"CAchievementsGridTitleList", L"XuiList", 0, { 0 } },
    { L"CAchievementsGridList", L"XuiList", 0, { 0 } },
    { L"GridList", L"XuiList", 0, { 0 } },
    { L"MediaContainerList", L"XuiList", 0, { 0 } },
    { L"SongList", L"XuiList", 0, { 0 } },
    { L"NowPlayingList", L"XuiList", 0, { 0 } },
    { L"EditNowPlayingQueueList", L"XuiList", 0, { 0 } },
    { L"VideoList", L"XuiList", 0, { 0 } },
    { L"CategoriesList", L"XuiCommonList", 0, { 0 } },
    { L"ItemsList", L"XuiList", 0, { 0 } },
};
const StandInProc kListProcs[] = { PlayedGamesListProc, DeviceListProc, AchTitleListProc, AchGridListProc,
                                    PictureGridProc, ContainerListProc, SongListProc, QueueListProc, QueueListProc,
                                    VideoListProc, MemoryCategoriesListProc, MemoryItemsListProc };
// The Marketplace hub (tools/marketplace5.py): the focused tile's highlight.
// 6717's banner buttons show focus by brightening their picture, and the
// tiles have none yet: each has a highlight bar (rdTileHi<n>, the links' own
// button) shown and played in its Focus range while the tile has the focus.
// Each tile's picture: 1888's own icon for its category, on the tile's right,
// clear of the label along its bottom. From the packages that hold them.
const DWORD kHubTimer = 0x48554231;             // "HUB1": once the tiles exist
const wchar_t* const kTileIcons[] = { L"common://ico_64x_games.png", L"section://0,pictures#ico_96x_folder.png",
                                      L"sharedres://ico_64x_controller.png", L"common://ico_96x_PC.png",
                                      L"sharedres://ico_64x_Themes.png",
                                      L"section://0,dashmark#ico_64x_FeaturedDownloads.png" };

void AddTileIcons(HXUIOBJ hub)
{
    for (int i = 0; i < (int)ARRAYSIZE(kTileIcons); i++)
    {
        wchar_t id[16];
        _snwprintf_s(id, 16, _TRUNCATE, L"btnTile%d", i);
        HXUIOBJ tile = Child(hub, id);
        HXUIOBJ img = NULL;
        if (!tile || FAILED(XuiCreateObject(L"XuiImage", &img)) || !img)
            continue;
        float w = 0, h = 0;
        XuiElementGetBounds(tile, &w, &h);
        D3DXVECTOR3 at(0, 0, 0);
        XuiElementGetPosition(tile, &at);
        const float size = 56.0f;
        at.x += w - size - 10.0f;
        at.y += (h - size) / 2;
        _snwprintf_s(id, 16, _TRUNCATE, L"rdTileIcon%d", i);
        SetStringProperty(img, L"Id", id);
        SetUint(img, L"SizeMode", 0x10);                // fit, keeping its shape
        XuiElementSetBounds(img, size, size);
        XuiElementSetPosition(img, &at);
        XuiElementAddChild(hub, img);
        XuiImageElementSetImagePath(img, kTileIcons[i]);
    }
}

// The hub's buttons play 1888's focus sound (btn_Focus) as 1888's own do. But a
// blade lands with its first button focused, and on 1888's blades that's the
// gamer card, which has none: landing is silent. The Marketplace has no card,
// so landing played its first tile's "boop". While the
// focus is outside the hub its buttons' sounds are muted; a moment after it
// lands inside they're back, for moving between them.
const DWORD kHubSoundTimer = 0x48554253;        // "HUBS"
const DWORD kHubSoundTick = 60, kHubUnmuteAfter = 300;

void MuteHubSounds(HXUIOBJ hub, bool mute)
{
    int found = 0;
    HXUIOBJ c = NULL;
    if (FAILED(XuiElementGetFirstChild(hub, &c)))
        c = NULL;
    // (the first child once: the first try asked for it in the loop's
    // condition, every pass, and never left it - the dashboard hung)
    for (int guard = 0; c && guard < 256; guard++)
    {
        HXUIOBJ visual = NULL;
        if (SUCCEEDED(XuiControlGetVisual(c, &visual)) && visual)
            if (HXUIOBJ sound = Find(visual, L"Xaudio_Sound01"))
                XuiSoundMute(sound, mute ? TRUE : FALSE), found++;
        HXUIOBJ next = NULL;
        if (FAILED(XuiElementGetNext(c, &next)))
            next = NULL;
        c = next;
    }
    Log("1888 marketplace: %d button sounds %s", found, mute ? "muted" : "back");
}

void HubSoundTick(HXUIOBJ hub)
{
    static bool muted = false;
    static DWORD inSince = 0;
    bool inside = false;
    for (HXUIOBJ f = XuiElementGetFocus(); f && !inside; XuiElementGetParent(f, &f))
        inside = f == hub;
    if (!inside)
    {
        inSince = 0;
        if (!muted)
            MuteHubSounds(hub, true), muted = true;
        return;
    }
    if (!inSince)
        inSince = GetTickCount();
    if (muted && GetTickCount() - inSince >= kHubUnmuteAfter)
        MuteHubSounds(hub, false), muted = false;
}

// The Xbox Live blade's billboard (botdBillboard, 345 x 240, under the Xbox Live
// logo: 1888's promotions from Xbox Live were shown there): a slideshow of
// xbox.com's own promotional tiles of 2006-08, the ones the user chose
// (2026-10-15; tools/pick_images.py, tools/slideshow.py), each whole as
// <theme folder>\slideshow\*.jpg, 339 x 213, centred in the box. Shuffled;
// each held, then crossfaded into the next. (First in the Marketplace hub's
// banner; the user moved it here, the same day.)
const DWORD kBannerTimer = 0x424E5253;          // "BNRS"
const DWORD kSlideHold = 10000, kSlideFade = 1200, kSlideTick = 40;     // (slower)
const float kSlideInset = 3.0f;

struct Slideshow
{
    HXUIOBJ owner, img[2];          // owner: the scene whose timer steps it
    std::vector<std::wstring> files;
    int front;                      // img[front] is the one showing
    size_t next;                    // files[next]: the next to show
    DWORD since;                    // when the hold or the fade began
    bool fading;
    // (Every field set here: the XDK's compiler doesn't zero the plain ones of
    // Slideshow() when the struct also holds a vector, and 0.0.111 crashed on
    // its first picture with img[front] read from garbage.)
    Slideshow() : owner(NULL), front(0), next(0), since(0), fading(false) { img[0] = img[1] = NULL; }
};
Slideshow g_slides;

void StartSlideshow(HXUIOBJ owner, HXUIOBJ box)
{
    if (g_slides.owner)
        XuiKillTimer(g_slides.owner, kBannerTimer);
    g_slides = Slideshow();
    if (!box)
        return;
    WIN32_FIND_DATAA fd;
    HANDLE f = FindFirstFileA((ThemeFolder("blades1888") + "\\slideshow\\*.jpg").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE)
    {
        Log("1888 slideshow: no pictures");
        return;
    }
    do
    {
        std::string n = fd.cFileName;
        g_slides.files.push_back(L"file://hdd:/SlopDash/Themes/blades1888/slideshow/" + std::wstring(n.begin(), n.end()));
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    srand(GetTickCount());
    for (size_t i = g_slides.files.size(); i > 1; i--)
        std::swap(g_slides.files[i - 1], g_slides.files[rand() % i]);
    float w = 0, h = 0;
    XuiElementGetBounds(box, &w, &h);
    // The pictures' own shape, as big as the box takes, centred.
    float sw = w - 2 * kSlideInset, sh = sw * 213.0f / 339.0f;
    if (sh > h - 2 * kSlideInset)
    {
        sh = h - 2 * kSlideInset;
        sw = sh * 339.0f / 213.0f;
    }
    for (int i = 0; i < 2; i++)
    {
        HXUIOBJ img = NULL;
        if (FAILED(XuiCreateObject(L"XuiImage", &img)) || !img)
            return;
        SetStringProperty(img, L"Id", i ? L"rdSlide1" : L"rdSlide0");
        SetUint(img, L"SizeMode", 0x10);                // fit, keeping its shape
        D3DXVECTOR3 at((w - sw) / 2, (h - sh) / 2, 0);
        XuiElementSetBounds(img, sw, sh);
        XuiElementSetPosition(img, &at);
        XuiElementSetOpacity(img, i ? 0.0f : 1.0f);
        XuiElementAddChild(box, img);
        g_slides.img[i] = img;
    }
    XuiImageElementSetImagePath(g_slides.img[0], g_slides.files[0].c_str());
    g_slides.owner = owner;
    g_slides.next = 1 % g_slides.files.size();
    g_slides.since = GetTickCount();
    if (g_slides.files.size() > 1)
        XuiSetTimer(owner, kBannerTimer, kSlideTick);
    Log("1888 slideshow: %u pictures", (unsigned)g_slides.files.size());
}

void StepSlideshow()
{
    Slideshow& s = g_slides;
    if (s.files.empty() || !s.img[0] || !s.img[1] || (s.front != 0 && s.front != 1))
        return;
    DWORD t = GetTickCount() - s.since;
    HXUIOBJ front = s.img[s.front], back = s.img[1 - s.front];
    if (!s.fading)
    {
        if (t < kSlideHold)
            return;
        XuiImageElementSetImagePath(back, s.files[s.next].c_str());
        s.fading = true;
        s.since = GetTickCount();
        return;
    }
    float a = t >= kSlideFade ? 1.0f : (float)t / kSlideFade;
    XuiElementSetOpacity(back, a);
    XuiElementSetOpacity(front, 1.0f - a);
    if (a < 1.0f)
        return;
    s.front = 1 - s.front;
    s.next = (s.next + 1) % s.files.size();
    s.fading = false;
    s.since = GetTickCount();
}

// Its owner's messages: true when the slideshow took it (its timer).
bool SlideshowMessage(HXUIOBJ obj, XUIMessage* msg)
{
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kBannerTimer)
    {
        if (obj == g_slides.owner)
            StepSlideshow();
        else
            XuiKillTimer(obj, kBannerTimer);
        msg->bHandled = TRUE;
        return true;
    }
    if (msg->dwMessage == XM_DESTROY && obj == g_slides.owner)
    {
        XuiKillTimer(obj, kBannerTimer);
        g_slides = Slideshow();
    }
    return false;
}

// The Marketplace hub's banner (scnBanner, 345 x 219, top right; empty since
// the slideshow moved out): one fixed picture, from the slideshow's tiles - 21.jpg, Dead Rising's
// "Demo available now in Marketplace". Fitted as the slideshow's were.
void ShowHubBanner(HXUIOBJ hub)
{
    HXUIOBJ box = Child(hub, L"scnBanner");
    if (!box || Child(box, L"rdBanner"))
        return;
    std::string file = ThemeFolder("blades1888") + "\\slideshow\\21.jpg";
    DWORD attrs = GetFileAttributesA(file.c_str());
    if (attrs == (DWORD)-1 || (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        Log("1888 marketplace: no banner picture (%s)", file.c_str());
        return;
    }
    float w = 0, h = 0;
    XuiElementGetBounds(box, &w, &h);
    float sw = w - 2 * kSlideInset, sh = sw * 213.0f / 339.0f;
    if (sh > h - 2 * kSlideInset)
    {
        sh = h - 2 * kSlideInset;
        sw = sh * 339.0f / 213.0f;
    }
    HXUIOBJ img = NULL;
    if (FAILED(XuiCreateObject(L"XuiImage", &img)) || !img)
        return;
    SetStringProperty(img, L"Id", L"rdBanner");
    SetUint(img, L"SizeMode", 0x10);                    // fit, keeping its shape
    D3DXVECTOR3 at((w - sw) / 2, (h - sh) / 2, 0);
    XuiElementSetBounds(img, sw, sh);
    XuiElementSetPosition(img, &at);
    XuiElementAddChild(box, img);
    XuiImageElementSetImagePath(img, L"file://hdd:/SlopDash/Themes/blades1888/slideshow/21.jpg");
}

HRESULT HubProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kHubTimer, 20);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kHubSoundTimer)
    {
        HubSoundTick(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kHubTimer)
    {
        XuiKillTimer(obj, kHubTimer);
        AddTileIcons(obj);
        ShowHubBanner(obj);
        HubSoundTick(obj);
        XuiSetTimer(obj, kHubSoundTimer, kHubSoundTick);
        msg->bHandled = TRUE;
    }
    if (msg->dwMessage != XM_NOTIFY)
        return S_OK;
    XUINotify* n = (XUINotify*)msg->pvData;
    if (n->dwNotify == XN_PRESS)
    {
        // The store (theme/store_screens.inc): a tile opens its shelf, the
        // links their screens.
        std::wstring pressed = ElementId(n->hObjSource);
        HRESULT hr = S_FALSE;
        if (pressed == L"btnFileManager")
            hr = OpenFileManager(n->hObjSource, NULL);
        else if (pressed == L"btnActiveDownloads")
            hr = OpenStoreDownloads(n->hObjSource);
        else if (pressed == L"btnAddRepo")
            hr = OpenStoreRepos(n->hObjSource);
        else if (!pressed.compare(0, 7, L"btnTile"))
            hr = OpenStoreShelf(n->hObjSource, _wtoi(pressed.c_str() + 7));
        if (hr != S_FALSE)
        {
            Log("1888 marketplace: %S: %08X", pressed.c_str(), (unsigned)hr);
            msg->bHandled = TRUE;
            return S_OK;
        }
    }
    if (n->dwNotify != XN_SET_FOCUS && n->dwNotify != XN_KILL_FOCUS)
        return S_OK;
    std::wstring id = ElementId(n->hObjSource);
    if (id.compare(0, 7, L"btnTile") != 0)
        return S_OK;
    wchar_t hi[16];
    _snwprintf_s(hi, 16, _TRUNCATE, L"rdTileHi%s", id.c_str() + 7);
    HXUIOBJ bar = Child(obj, hi);
    if (!bar)
        return S_OK;
    bool on = n->dwNotify == XN_SET_FOCUS;
    XuiElementSetShow(bar, on ? TRUE : FALSE);
    if (on)
        XuiControlPlayVisualRangeEx(bar, L"Focus", L"Focus", L"EndFocus", FALSE);
    return S_OK;
}

// Video (videos#VideosMain.xur, VideosMainScene) and the Music Player
// (music#1028_NowPlaying.xur, NowPlayingScene): 1888's code filled them; with
// nothing to list yet, 1888's own empty states (videos Strings.xus 0 "No
// videos found."; music 34 "To play music, select a music source.") in place
// of the files' placeholders ("<Message>", "<video title>", "<CURRENT ggg song
// name>"...), and no endless "loading" animation.
const DWORD kMediaFillTimer = 0x4D464C31;       // "MFL1"



// System > Computers (dashcomm#742_SelectNetworkDevice.xur): SlopDash streams
// from no PC.
HRESULT ComputersProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    // (its buttons' panel, 742_metaByDevice, is DashScene's: XUI passes the
    // message on to the base class's proc)
    if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        if (id == L"btnWindowsMedia" || id == L"btnMediaCenter")
        {
            SayBox(id == L"btnMediaCenter" ? L"Windows Media Center" : L"Windows-based PC",
                   L"Cannot Connect. Not Supported");
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// System > Family Settings (consoles#dashSysCslSetPControlSelect.xur) and its
// Console Controls (dashSysCslSetPControl.xur): 1888's code filled their
// rating lists and kept the pass code; none of it is here, and a wrong change
// could lock games away, so their entries say so instead of opening empty
// screens. Done goes back.
const DWORD kFamilyTimer = 0x46414D31;          // "FAM1"

HRESULT FamilyProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    static const wchar_t* const kOff[] = { L"btnLive", L"btnGame", L"btnMovie", L"btnLiveA", L"btnLiveC", L"btnXbox",
                                           L"btnPass", L"btnReset" };
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kFamilyTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kFamilyTimer)
    {
        XuiKillTimer(obj, kFamilyTimer);
        for (int i = 0; i < 8; i++)
            if (HXUIOBJ b = Child(obj, kOff[i]))
                SetStringProperty(b, L"PressPath", L"");      // (no empty screen behind them)
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        for (int i = 0; i < 8; i++)
            if (id == kOff[i])
            {
                SayBox(L"Family Settings", L"Not Available");
                msg->bHandled = TRUE;
            }
        if (id == L"btnDone")
        {
            Blades1888Back();
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}


// The Gamer Profile's View Games: the Achievements screen (games#402_
// Achievements.xur, as the Games blade's Achievements opens it) over the
// blades, the Guide closed.
void ViewGamesFromProfile()
{
    HXUIOBJ card = g_hudReturn;
    HudClose();
    std::wstring base;
    PackageLocator(L"dashmain", base);
    HXUIOBJ scene = NULL;
    HRESULT hr = XuiSceneCreate(base.c_str(), L"402_Achievements.xur", NULL, &scene);
    if (SUCCEEDED(hr))
        hr = XuiSceneNavigateForward(g_root, TRUE, scene, XUSER_INDEX_FOCUS);
    HXUIOBJ content = card ? ContentOf(card) : NULL;
    if (SUCCEEDED(hr) && content)
        HideBlade(content, card);
    Log("1888 gamer profile: View Games: %08X", (unsigned)hr);
}

// 1888's Gamer Profile (gamerprofile#GamerCardScene.xur, GamerCardUIScene):
// the offline profile's buttons (meOfflineButtonGroup; the online group needs
// Xbox Live), in 1888's own words (gamerprofile strings: "Auto Sign-In").
const DWORD kProfileTimer = 0x47505231;         // "GPR1": once its parts exist

HRESULT GamerProfileProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kProfileTimer, 20);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kProfileTimer)
    {
        XuiKillTimer(obj, kProfileTimer);
        // (the Guide's X and Y: nothing for them here, 1888's showed none)
        SetLegend(L"Legend_X", L"");
        SetLegend(L"Legend_Y", L"");
        Show(obj, L"meOnlineButtonGroup", false);
        if (HXUIOBJ group = Child(obj, L"meOfflineButtonGroup"))
        {
            XuiElementSetShow(group, TRUE);
            SetText(group, L"autoSignInButton", L"Auto Sign-In");
            if (HXUIOBJ first = Child(group, L"viewGamesButton"))
                XuiElementSetFocus(first);
        }
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_B)
    {
        // (the scene took B itself and nothing closed: the Guide closes)
        HudClose();
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
    {
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        if (id == L"viewGamesButton")
            ViewGamesFromProfile();
        else if (id == L"addLiveButton")
            SayOffline(L"Join Xbox Live");
        else if (id == L"editProfileButton")
            SayBox(L"Edit Gamer Profile", L"Coming Soon");
        else if (id == L"gamerPreferencesButton")
            SayBox(L"Game Defaults", L"Coming Soon");
        else if (id == L"autoSignInButton")
            SayBox(L"Auto Sign-In", L"Coming Soon");
        else
            return S_OK;
        msg->bHandled = TRUE;
    }
    return S_OK;
}

const StandInInfo kSceneClasses[] = {
    { L"GamerCardUIScene", L"HUDScene", 0, { 0 } },
    { L"CKeyboardScene", L"HUDScene", 0, { 0 } },
    { L"CGameShowcaseScene", L"XuiScene", 0, { 0 } },
    { L"DeviceSelectorScene", L"XuiScene", 0, { 0 } },
    { L"CConnStatus", L"XuiScene", 0, { 0 } },
    { L"dashSettingsRoot", L"DashScene", 0, { 0 } },
    { L"MetaPanelScene", L"XuiScene", 0, { 0 } },
    { L"CAchievementsGridScene", L"DashScene", 0, { 0 } },
    { L"SlopMarketplaceHub", L"XuiScene", 0, { 0 } },
    { L"VideosMainScene", L"DashScene", 0, { 0 } },
    { L"NowPlayingScene", L"XuiScene", 0, { 0 } },
    { L"C2010_TestingNetwork", L"XuiScene", 0, { 0 } },
    { L"C2012_TestInformation", L"XuiScene", 0, { 0 } },
    { L"NetworkDeviceSelectScene", L"DashScene", 0, { 0 } },
    { L"dashPControlSelect", L"DashScene", 0, { 0 } },
    { L"dashPControlSettingsMenu", L"DashScene", 0, { 0 } },
    { L"PhotoAreaMainScene", L"DashScene", 0, { 0 } },
    { L"DeviceMainScene", L"XuiScene", 0, { 0 } },
    { L"FullScreenPhotoScene", L"XuiScene", 0, { 0 } },
    { L"MusicAreaMainScene", L"DashScene", 0, { 0 } },
    { L"MusicLibraryScene", L"DashScene", 0, { 0 } },
    { L"MediaContainerListScene", L"XuiScene", 0, { 0 } },
    { L"SongListScene", L"XuiScene", 0, { 0 } },
    { L"MediaContainerOptionsScene", L"XuiScene", 0, { 0 } },
    { L"ArtistOptionsScene", L"XuiScene", 0, { 0 } },
    { L"SongOptionsScene", L"XuiScene", 0, { 0 } },
    { L"EditNowPlayingQueueScene", L"XuiScene", 0, { 0 } },
    { L"VideoScene", L"XuiScene", 0, { 0 } },
    { L"CategoriesScene", L"XuiScene", 0, { 0 } },
    { L"ItemsScene", L"XuiScene", 0, { 0 } },
    { L"ItemDetailsScene", L"XuiScene", 0, { 0 } },
    { L"OperationProgressScene", L"XuiScene", 0, { 0 } },
    { L"DeviceDetailsScene", L"XuiScene", 0, { 0 } },
};
const StandInProc kSceneProcs[] = { GamerProfileProc, KeyboardProc, PlayedGamesSceneProc, DeviceSceneProc, ConnStatusProc, SettingsRootProc,
                                     MetaPanelProc, AchievementsSceneProc, HubProc, VideosMainProc,
                                     NowPlayingProc, NetTestProc, NetInfoProc, ComputersProc,
                                     FamilyProc, FamilyProc, PhotoMainProc, PictureDeviceProc, PhotoViewerProc,
                                     MusicMainProc, MusicLibraryProc, ContainerListSceneProc, SongListSceneProc,
                                     GroupOptionsProc, GroupOptionsProc, SongOptionsProc, QueueEditorProc,
                                     VideoSceneProc, MemoryCategoriesProc, MemoryItemsProc, MemoryItemDetailsProc,
                                     MemoryProgressProc, MemoryDeviceDetailsProc };

}  // namespace launch

void SetBlades1888Layer(HXUIOBJ layer)
{
    launch::g_layer = layer;
}

HRESULT RegisterBlades1888Classes()
{
    // The contents' base first (with its side panel), then the classes built on it.
    Log("1888 DashScene: %s", RegisterStandInWithProc(launch::kDashScene, launch::DashSceneProc) ? "registered" : "FAILED");
    launch::RegisterConsoleSettings1888();
    // The Games Library and the list screens (Themes...): the 6717 theme's own, shared.
    RegisterSharedBladesClasses();
    SetScreenOpener(launch::OpenFromBlades);
    SetKeyboardShower(launch::ShowKeyboard1888);
    for (size_t i = 0; i < sizeof(launch::kListClasses) / sizeof(launch::kListClasses[0]); i++)
        if (!RegisterStandInWithProc(launch::kListClasses[i], launch::kListProcs[i]))
            Log("1888 %S: FAILED", launch::kListClasses[i].name);
    // The Guide's scenes' base before the classes built on it (the Gamer
    // Profile's); as blades1888_classes.inc has it, which then skips it.
    static const StandInInfo kHudScene = { L"HUDScene", L"XuiScene", 5, { 3, 5, 5, 5, 5 } };
    RegisterStandIns(&kHudScene, 1);
    for (size_t i = 0; i < sizeof(launch::kSceneClasses) / sizeof(launch::kSceneClasses[0]); i++)
        if (!RegisterStandInWithProc(launch::kSceneClasses[i], launch::kSceneProcs[i]))
            Log("1888 %S: FAILED", launch::kSceneClasses[i].name);
    for (size_t i = 0; i < sizeof(launch::kContentClasses) / sizeof(launch::kContentClasses[0]); i++)
        if (!RegisterStandInWithProc(launch::kContentClasses[i], launch::ContentProc))
            Log("1888 %S: FAILED", launch::kContentClasses[i].name);
    Log("1888 DashMainScene: %08X", (unsigned)launch::CDashMain::Register());
    Log("1888 HUDBkgndScene: %08X", (unsigned)launch::CHudBkgnd::Register());
    Log("1888 UserList: %s", RegisterStandInWithProc(launch::kUserList, launch::UserListProc) ? "registered" : "FAILED");

    Log("1888 CGamesBladeScene: %08X", (unsigned)launch::CGamesBlade::Register());
    Log("1888 DashMediaScene: %08X", (unsigned)launch::CMediaBlade::Register());
    Log("1888 DashLiveScene: %08X", (unsigned)launch::CLiveBlade::Register());
    Log("1888 DashMPScene: %08X", (unsigned)launch::CMarketplaceBlade::Register());
    // 6717's dashmain's other classes the 1888 theme didn't have (as 6717 has them).
    static const StandInInfo k6717[] = { { L"LiveVisionDashEffect", L"XuiControl", 0, { 0 } },
                                         { L"ScriptScene", L"XuiScene", 1, { 5 } },
                                         { L"XuiBOTDOfflineContainer", L"XuiScene", 2, { 3, 5 } } };
    Log("6717 stand-ins: %d", RegisterStandIns(k6717, 3));
    Log("1888 XuiGamerCard: %s", RegisterStandInWithProc(launch::kGamerCard, launch::GamerCardProc) ? "registered" : "FAILED");
    return S_OK;
}

void UnregisterBlades1888Classes()
{
    SetKeyboardShower(NULL);
    launch::CGamesBlade::Unregister();
    launch::CMediaBlade::Unregister();
    launch::CLiveBlade::Unregister();
    launch::CMarketplaceBlade::Unregister();
    launch::CDashMain::Unregister();
    launch::CHudBkgnd::Unregister();
}

void Blades1888Back()
{
    launch::BackFallback();
}

#ifdef SLOPDASH_LOCAL
#include "../../local/blades_cancelbox.inc"
#endif

void RefreshGamerCards1888()
{
    launch::RefreshCards();
}

// A screen opened from one already open over the blades (a game's menu from
// the Game Library): its full-screen frames now. Waiting for the focus to
// reach it (the depth check, every 100 ms) started them half a second after
// the screen showed.
void Blades1888ScreensOpened(HXUIOBJ first, HXUIOBJ second, HXUIOBJ focus)
{
    if (launch::g_dashMain)
        launch::g_dashMain->Opened(first, second);
    HXUIOBJ content = focus && launch::OnBlades(focus) ? launch::ContentOf(focus) : NULL;
    if (content && (first || second))
        launch::HideBlade(content, focus);
}

void Blades1888ScreenOpened()
{
    if (!launch::g_dashMain || launch::g_dashMain->Level() != 1)
        return;
    launch::g_dashMain->LevelTo(2);
    launch::g_levelHoldUntil = GetTickCount() + 1500;   // the focus gets there meanwhile
}
