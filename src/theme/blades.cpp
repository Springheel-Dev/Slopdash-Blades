#include "theme/blades.h"
#include "theme/blades1888.h"

#include <xui.h>
#include <xuiapp.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include "platform/store.h"
#include <stdio.h>
#include <math.h>
#include <xonline.h>

#include "platform/log.h"
#include "platform/resources.h"
#include "platform/fetcher.h"
#include "platform/launcher.h"
#include "platform/library_settings.h"
#include "platform/net.h"
#include "platform/profiles.h"
#include "platform/scanner.h"
#include "platform/shotcache.h"
#include "platform/stand_ins.h"
#include "platform/settings.h"
#include "platform/blade_colours.h"
#include "platform/themes.h"
#include "platform/retroachievements.h"
#include "platform/trainers.h"
#include "platform/xconfig.h"

// The kernel's drive tray control (xboxkrnl; not in the XDK's headers).
extern "C" VOID HalOpenCloseODDTray(DWORD open);

namespace {

// Loads `scene` from `package` and adds it as a child of `parent` at (x, y).
HRESULT AddScene(HXUIOBJ parent, const wchar_t* package, const wchar_t* scene, float x, float y,
                 HXUIOBJ* added = NULL)
{
    std::wstring base;
    if (!PackageLocator(package, base))
    {
        Log("no package %S for %S", package, scene);
        return E_FAIL;
    }
    HXUIOBJ child = NULL;
    HRESULT hr = XuiSceneCreate(base.c_str(), scene, NULL, &child);
    if (SUCCEEDED(hr))
        hr = XuiElementAddChild(parent, child);
    if (SUCCEEDED(hr))
    {
        D3DXVECTOR3 pos(x, y, 0);
        hr = XuiElementSetPosition(child, &pos);
    }
    Log("%S#%S at (%.0f, %.0f): %08X", package, scene, x, y, (unsigned)hr);
    if (added)
        *added = SUCCEEDED(hr) ? child : NULL;
    return hr;
}

// Plays the named-frame range `from` .. `to` on obj (and its children).
HRESULT PlayRange(HXUIOBJ obj, const wchar_t* from, const wchar_t* to, bool recurse = true)
{
    int start = -1, end = -1;
    HRESULT hr = XuiElementFindNamedFrame(obj, from, &start);
    if (SUCCEEDED(hr))
        hr = XuiElementFindNamedFrame(obj, to, &end);
    if (SUCCEEDED(hr))
        hr = XuiElementPlayTimeline(obj, start, start, end, FALSE, recurse ? TRUE : FALSE);
    Log("play %S (%d) .. %S (%d): %08X", from, start, to, end, (unsigned)hr);
    return hr;
}

// A string property as XUI holds it (e.g. a DashScene's "P0" button list).
std::wstring StringProperty(HXUIOBJ obj, const wchar_t* name)
{
    // Not on a stand-in (see IsStandIn): XUI's copy of its properties can be garbage.
    if (IsStandIn(obj))
        return L"";
    DWORD id = 0;
    XUIElementPropVal v;
    if (SUCCEEDED(XuiObjectGetPropertyId(obj, name, &id)) && SUCCEEDED(XuiObjectGetProperty(obj, id, 0, &v)) &&
        v.type == XUI_EPT_STRING && v.szVal)
        return v.szVal;
    return L"";
}

// Blades' lists in one string are separated by the two characters "\0".
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

std::wstring ElementId(HXUIOBJ obj)
{
    LPCWSTR id = NULL;
    if (obj && SUCCEEDED(XuiElementGetId(obj, &id)) && id)
        return id;
    return L"";
}

HXUIOBJ Child(HXUIOBJ parent, const wchar_t* id)
{
    HXUIOBJ h = NULL;
    if (parent)
        XuiElementGetChildById(parent, id, &h);
    return h;
}

void SetText(HXUIOBJ parent, const wchar_t* id, const wchar_t* text)
{
    HXUIOBJ h = Child(parent, id);
    HRESULT hr = h ? XuiControlSetText(h, text) : E_FAIL;
    if (FAILED(hr))
        Log("gamer card: can't set %S (%08X)", id, (unsigned)hr);
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

// --- the profile ---------------------------------------------------------------

// Whoever the console has signed in (auto sign-in, or the Guide): SlopDash
// has no sign-in of its own; it shows that profile and follows changes.

bool ReadGamertag(int user, wchar_t (&out)[XUSER_NAME_SIZE + 1])
{
    char name[XUSER_NAME_SIZE + 1] = { 0 };
    DWORD r = XUserGetName(user, name, sizeof(name));
    if (r != ERROR_SUCCESS)
    {
        Log("gamer card: XUserGetName %u", (unsigned)r);
        return false;
    }
    for (int i = 0; i <= XUSER_NAME_SIZE; i++)
        out[i] = (unsigned char)name[i];
    return true;
}

// The card's numbers: gamerscore, reputation, zone. 0.0.17 and 0.0.18 called
// XUserReadProfileSettings synchronously and the console switched off inside
// the call; 0.0.41 asked asynchronously (title ID 0, i.e. SlopDash's own,
// which has no settings in the profile) and the read never finished. These
// are the dashboard's settings, so this asks for title FFFE07D1, still
// asynchronously; the card's timer collects the answer or gives up.
const DWORD kDashTitleId = 0xFFFE07D1;
const int kProfileReadPolls = 100;          // x 100 ms

struct ProfileRead
{
    XOVERLAPPED ov;
    DWORD size;
    bool pending;
    int polls;
    BYTE buf[4096];
};
ProfileRead g_profileRead;

struct CardNumbers
{
    int gamerscore, zone;
    float rep;                  // 0..100 (5 stars)
};

bool StartProfileRead(int user)
{
    if (g_profileRead.pending && !XHasOverlappedIoCompleted(&g_profileRead.ov))
    {
        Log("gamer card: an earlier profile read is still running");
        return false;
    }
    static DWORD ids[3] = { XPROFILE_GAMERCARD_CRED, XPROFILE_GAMERCARD_REP, XPROFILE_GAMERCARD_ZONE };
    memset(&g_profileRead.ov, 0, sizeof(g_profileRead.ov));
    g_profileRead.size = sizeof(g_profileRead.buf);
    g_profileRead.polls = 0;
    Log("gamer card: reading profile settings of title %08X (asynchronous)", (unsigned)kDashTitleId);
    DWORD r = XUserReadProfileSettings(kDashTitleId, user, 3, ids, &g_profileRead.size,
                                       (XUSER_READ_PROFILE_SETTING_RESULT*)g_profileRead.buf, &g_profileRead.ov);
    Log("gamer card: profile read started: %u", (unsigned)r);
    g_profileRead.pending = r == ERROR_IO_PENDING || r == ERROR_SUCCESS;
    return g_profileRead.pending;
}

// True once the read has finished or been given up on; ok says whether it worked.
bool ProfileReadDone(CardNumbers& n, bool& ok)
{
    ok = false;
    n.gamerscore = n.zone = -1;
    n.rep = -1;
    if (!XHasOverlappedIoCompleted(&g_profileRead.ov))
    {
        if (++g_profileRead.polls < kProfileReadPolls)
            return false;
        Log("gamer card: profile read still running after %d s; giving up", kProfileReadPolls / 10);
        return true;
    }
    g_profileRead.pending = false;
    DWORD result = 0;
    DWORD r = XGetOverlappedResult(&g_profileRead.ov, &result, FALSE);
    Log("gamer card: profile read finished: %u (after %d polls)", (unsigned)r, g_profileRead.polls);
    ok = r == ERROR_SUCCESS;
    if (!ok)
        return true;
    const XUSER_READ_PROFILE_SETTING_RESULT* res = (const XUSER_READ_PROFILE_SETTING_RESULT*)g_profileRead.buf;
    for (DWORD i = 0; i < res->dwSettingsLen; i++)
    {
        const XUSER_PROFILE_SETTING& st = res->pSettings[i];
        if (st.dwSettingId == XPROFILE_GAMERCARD_CRED && st.data.type == XUSER_DATA_TYPE_INT32)
            n.gamerscore = (int)st.data.nData;
        else if (st.dwSettingId == XPROFILE_GAMERCARD_ZONE && st.data.type == XUSER_DATA_TYPE_INT32)
            n.zone = (int)st.data.nData;
        else if (st.dwSettingId == XPROFILE_GAMERCARD_REP && st.data.type == XUSER_DATA_TYPE_FLOAT)
            n.rep = st.data.fData;
    }
    return true;
}

const wchar_t* ZoneName(int zone)
{
    switch (zone)
    {
    case XPROFILE_GAMERCARD_ZONE_RR: return L"Recreation";
    case XPROFILE_GAMERCARD_ZONE_PRO: return L"Pro";
    case XPROFILE_GAMERCARD_ZONE_FAMILY: return L"Family";
    case XPROFILE_GAMERCARD_ZONE_UNDERGROUND: return L"Underground";
    default: return L"None";
    }
}

// The online card's numbers. Reputation: reputationScene's Starfull1..5
// (filled stars, hidden in the file) over the outline stars Star1..5.
void ShowCardNumbers(HXUIOBJ online, const CardNumbers& n)
{
    SetNumber(online, L"credText", n.gamerscore < 0 ? 0 : n.gamerscore);
    SetText(online, L"zoneText", ZoneName(n.zone));
    HXUIOBJ rep = Child(online, L"reputationScene");
    int stars = n.rep < 0 ? 0 : (int)(n.rep / 20.0f + 0.5f);
    for (int i = 1; rep && i <= 5; i++)
    {
        wchar_t id[16];
        _snwprintf_s(id, 16, _TRUNCATE, L"Starfull%d", i);
        HXUIOBJ full = Child(rep, id);
        if (full)
            XuiElementSetShow(full, i <= stars);
    }
}

// --- XuiGamerCard ----------------------------------------------------------------
// 0.0.17 and 0.0.18 switched the console off a few seconds after the card was
// built, so its parts come back one at a time.
#pragma warning(disable : 4127)     // constant conditions: these switches
const bool kGamerCardProfile = true;    // the gamertag (XUserGetName)
const bool kGamerCardNumbers = true;    // gamerscore, rep, zone (XUserReadProfileSettings, asynchronous)
const bool kGamerCardPicture = true;    // gamer picture (XUserReadGamerPicture) when the profile file gave none
// The blade open/close animations on the tab scene. The last animation played
// on it (the boot animation, 0.0.8) crashed, so this comes back on its own.
const bool kBladeAnimations = false;

// Blades' own control (P0 = mode: "Normal", "Macro" or "Mini"; P1 an unknown bool).
// Its look is gamercar#GamerCard.xur; the dashboard's code filled it in.
const StandInInfo kGamerCard = { L"XuiGamerCard", L"XuiControl", 2, { 5, 1 } };
const DWORD kProfileReadTimer = 0x50524F46;   // "PROF"

// Builds the card. Runs from a timer after XM_INIT: by then the card's own
// properties (its mode) are set and the scene around it is complete.
void BuildGamerCard(HXUIOBJ obj, const XUIElementPropVal* vals, const bool* set)
{
    int user = SignedInUser();
    if (user < 0)
    {
        Log("gamer card: nobody signed in");
        return;
    }
    HXUIOBJ card = NULL;
    if (FAILED(AddScene(obj, L"gamercar", L"GamerCard.xur", 0, 0, &card)) || !card)
        return;
    // The mode ("Macro" on the Games blade), asked of XUI itself: it may store
    // the property without calling our setter.
    // Our own copy of P0; XUI's isn't read (on the Xbox LIVE blade it came back
    // as garbage, and a bad pointer there crashed SlopDash on sign-out).
    std::wstring mode;
    const XUIElementPropVal* ownMode = StandInProperty(obj, L"XuiGamerCard", 0);
    if (ownMode && ownMode->type == XUI_EPT_STRING && ownMode->szVal)
        mode = ownMode->szVal;
    HRESULT hr = S_OK;
    XUIElementPropVal v;
    v.type = XUI_EPT_EMPTY;
    // On the Xbox LIVE blade (content root a plain XuiScene) this came back as
    // garbage (0.0.49/0.0.50: a blank log line, the card left in its big Normal
    // layout over the logo). Then: our own copy of the property, else Macro,
    // the mode every blade's card uses.
    if (mode != L"Normal" && mode != L"Macro" && mode != L"Mini")
    {
        const XUIElementPropVal* own = StandInProperty(obj, L"XuiGamerCard", 0);
        std::wstring stored = own && own->type == XUI_EPT_STRING && own->szVal ? own->szVal : L"";
        mode = stored == L"Normal" || stored == L"Mini" ? stored : L"Macro";
        Log("gamer card: unreadable mode; using %S", mode.c_str());
    }
    Log("gamer card: mode %S (XUI: %08X type %d; setter %s)", mode.c_str(), (unsigned)hr, SUCCEEDED(hr) ? (int)v.type : -1,
        set[0] ? "called" : "not called");
    (void)vals;
    // Not recursive: the card's frames (Macro = 2) aren't its parts' frames
    // (their own "Normal" 0, "Macro" 1, "Mini" 2); each part is set to the
    // mode itself below. Recursing put the online part in its Mini layout.
    PlayRange(card, mode.c_str(), (L"End" + mode).c_str(), false);

    // The Live layout (gamertag, Rep, Gamerscore, Zone), as the dashboard
    // showed it; the offline one (games played, achievements) isn't used.
    // Online-only parts stay hidden: membership tier, status.
    HXUIOBJ online = Child(card, L"onlineBaseScene"), offline = Child(card, L"offlineBaseScene");
    Log("gamer card: online %p, offline %p", online, offline);
    // The compact modes leave out the extended part (status, recent
    // achievements); its two panels otherwise show over the blade's buttons.
    HXUIOBJ extended = Child(card, L"extendedScene");
    if (extended)
        XuiElementSetShow(extended, FALSE);       // (kept hidden: see the card's focus timer)
    if (offline)
        XuiElementSetShow(offline, FALSE);
    if (!online)
        return;
    XuiElementSetShow(online, TRUE);
    PlayRange(online, mode.c_str(), mode.c_str());
    static const wchar_t* const kOnlineOnly[] = { L"membershipTierScene", L"Mark", L"onlineStatusText",
                                                  L"onlineStatusTitle", L"onlineStatusImage" };
    for (size_t i = 0; i < sizeof(kOnlineOnly) / sizeof(kOnlineOnly[0]); i++)
    {
        HXUIOBJ h = Child(online, kOnlineOnly[i]);
        if (h)
            XuiElementSetShow(h, FALSE);
    }
    if (!kGamerCardProfile)
    {
        Log("gamer card: layout only (profile reads switched off)");
        return;
    }
    wchar_t gamertag[XUSER_NAME_SIZE + 1];
    if (!ReadGamertag(user, gamertag))
        return;
    Log("gamer card: user %d is %S", user, gamertag);
    SetText(online, L"gamerTagText", gamertag);
    // Until the profile read answers: 0, no stars, "None".
    CardNumbers none = { -1, -1, -1.0f };
    ShowCardNumbers(online, none);
    if (kGamerCardNumbers && StartProfileRead(user))
        XuiSetTimer(obj, kProfileReadTimer, 100);
    HXUIOBJ tile = Child(online, L"gamerTileImage");
    // The picture the system keeps for the profile (its file is locked while signed in).
    XUID xuid = 0;
    if (tile && kGamerCardPicture && XUserGetXUID(user, &xuid) == ERROR_SUCCESS)
        Log("gamer card: picture %08X", (unsigned)XuiImageElementSetImagePath(tile, GamerPicLocator(xuid).c_str()));
    Log("gamer card: done");
}

// --- Arcade::CTitleListCtrl ----------------------------------------------------
// My Games' list (arcade#2502_TitleSelectorScene.xur, item visual
// btn_twoline_btmAlign): one item per game in the library. Line 1 the game's
// name, line 2 what kind of game it is (the packages' publisher field is
// empty on these installs), and the game's own icon.
const StandInInfo kTitleList = { L"Arcade::CTitleListCtrl", L"XuiList", 0, { 0 } };
std::vector<std::wstring> g_iconPaths;

const wchar_t* KindText(library::Kind k)
{
    switch (k)
    {
    case library::kXbox360: return L"Xbox 360";
    case library::kArcade: return L"Xbox LIVE Arcade";
    case library::kXboxOriginal: return L"Xbox";
    default: return L"";
    }
}

bool ViewKey(HXUIOBJ list, DWORD key);     // library_views.inc

HRESULT TitleListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    const std::vector<library::Game>& games = Games();
    if (msg->dwMessage == XM_INIT)
    {
        g_iconPaths.clear();                    // the list may be another scope's now
        size_t shown = 0;
        TakeLibraryReplaced(shown);             // (a new list counts afresh)
    }
    if (msg->dwMessage == XM_KEYDOWN && ViewKey(obj, ((XUIMessageInput*)msg->pvData)->dwKeyCode))
    {
        msg->bHandled = TRUE;
        return S_OK;
    }
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = (int)games.size();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData && d->iItem >= 0 && d->iItem < (int)games.size())
        {
            const library::Game& g = games[d->iItem];
            // The name alone, as 1888's list (the console column, "Xbox 360" /
            // "Xbox", went).
            d->szText = d->iData == 0 ? g.name.c_str() : L"";
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        // One icon per game, in the item's left slot (btn_twoline_btmAlign:
        // image_presenter at x = 13, data 0; the text starts at x = 53 for it).
        // The right slot (image_presenter1, data 1) stays empty.
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = NULL;
            if (d->iData == 0 && d->iItem >= 0 && d->iItem < (int)games.size())
            {
                if (g_iconPaths.size() != games.size())
                {
                    g_iconPaths.clear();
                    for (size_t i = 0; i < games.size(); i++)
                        g_iconPaths.push_back(IconLocator(i));
                }
                d->szPath = g_iconPaths[d->iItem].c_str();
            }
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_INIT)
        Log("title list: %u games", (unsigned)games.size());
    return S_OK;
}

// --- Arcade::CTitleSelectorScene ----------------------------------------------------
// The scene around the list: its info panel's title (scnTitle/lblTitle,
// "<game title>" in the file) and the "2 of 12" counter (lblTotals) follow the
// selected game. Its base is DashScene, so that is registered first.
const StandInInfo kDashScene = { L"DashScene", L"XuiScene", 3, { 5, 5, 5 } };
const StandInInfo kTitleSelector = { L"Arcade::CTitleSelectorScene", L"DashScene", 0, { 0 } };
const DWORD kSelectorTimer = 0x53454C31;   // "SEL1"

HXUIOBJ Descendant(HXUIOBJ parent, const wchar_t* id)
{
    for (HXUIOBJ c = NULL; SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(parent, &c)) && c;)
    {
        LPCWSTR cid = NULL;
        if (SUCCEEDED(XuiElementGetId(c, &cid)) && cid && !wcscmp(cid, id))
            return c;
        HXUIOBJ deeper = Descendant(c, id);
        if (deeper)
            return deeper;
    }
    return NULL;
}

bool SetStringProperty(HXUIOBJ obj, const wchar_t* name, const wchar_t* value)
{
    DWORD id = 0;
    if (FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetString(&v, value);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

bool SetUintProperty(HXUIOBJ obj, const wchar_t* name, unsigned value)
{
    DWORD id = 0;
    if (!obj || FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetUint(&v, value);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

bool SetBoolProperty(HXUIOBJ obj, const wchar_t* name, bool value)
{
    DWORD id = 0;
    if (!obj || FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetBool(&v, value ? TRUE : FALSE);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

bool SetFloatProperty(HXUIOBJ obj, const wchar_t* name, float value)
{
    DWORD id = 0;
    if (!obj || FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetFloat(&v, value);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

// Text sizes in the info panel (the skin's label styles are much larger).
const float kInfoPointSize = 14.0f, kDescriptionPointSize = 12.0f;

// List & Description with Cover Art (Library View "list", its X): the panel shows the cover alone.
bool g_listCoverMode = false;

// A child of `parent` with this id, created (class, visual, place, size) if absent.
HXUIOBJ Ensure(HXUIOBJ parent, const wchar_t* id, const wchar_t* cls, const wchar_t* visual, float x, float y,
               float w, float h)
{
    HXUIOBJ obj = Child(parent, id);
    if (obj)
        return obj;
    if (FAILED(XuiCreateObject(cls, &obj)) || !obj)
        return NULL;
    SetStringProperty(obj, L"Id", id);
    if (visual)
        SetStringProperty(obj, L"Visual", visual);
    XuiElementSetBounds(obj, w, h);
    D3DXVECTOR3 pos(x, y, 0);
    XuiElementSetPosition(obj, &pos);
    XuiElementAddChild(parent, obj);
    return obj;
}

// GOD conversions all say this; it isn't a description of the game.
bool IsBoilerplate(const std::wstring& d)
{
    return d.empty() || d.find(L"This is an installed game") == 0 || d == L"[Description]" || d == L"ENG - Desc";
}

void Place(HXUIOBJ obj, float x, float y, float w, float h, bool show)
{
    if (!obj)
        return;
    D3DXVECTOR3 pos(x, y, 0);
    XuiElementSetPosition(obj, &pos);
    XuiElementSetBounds(obj, w, h);
    XuiElementSetShow(obj, show);
}

// The info panel (scnTitle, 405 x 443, its title bar 40 high). With details
// from the catalog (fetched once, cached): a screenshot, then developer /
// genre / year, then the description, like the original "Select a Game"
// screen. Without: the game's icon, what it is, and any local description.
void ShowGameInfo(HXUIOBJ panel, size_t index)
{
    const library::Game& g = Games()[index];
    HXUIOBJ shot = Ensure(panel, L"rdShot", L"XuiImage", NULL, 12, 46, 381, 214);
    HXUIOBJ icon = Ensure(panel, L"rdIcon", L"XuiImage", NULL, 12, 52, 64, 64);
    HXUIOBJ kind = Ensure(panel, L"rdKind", L"XuiLabel", L"Label_Body", 88, 52, 305, 64);
    HXUIOBJ desc = Ensure(panel, L"rdDescription", L"XuiLabel", L"Label_BodySmall", 12, 128, 381, 300);
    // Screenshots are 1280 x 720 (box art other shapes): scale to fit, keep the shape.
    SetUintProperty(shot, L"SizeMode", 0x10);   // XUI_IMAGE_PRESENTER_MODE_STRETCH_CENTER_MAINTAIN_ASPECT
    SetUintProperty(icon, L"SizeMode", 0x10);
    SetFloatProperty(kind, L"PointSize", kInfoPointSize);
    SetFloatProperty(desc, L"PointSize", kDescriptionPointSize);
    catalog::Details d;
    std::wstring cover = CoverLocator(index);
    // The panel's own parts: its frame, title bar, title and the video icon.
    // List V2 hides them all; the other list shows them.
    static const wchar_t* const kChrome[] = { L"grfxBackground", L"grfxHeader", L"lblTitle", L"imgVideoEnabled" };
    for (int i = 0; i < 4; i++)
    {
        HXUIOBJ part = Child(panel, kChrome[i]);
        if (part)
            XuiElementSetShow(part, !g_listCoverMode);
    }
    // Arcade's own parts of the panel (the game's gamerscore, its achievements,
    // its leaderboard) aren't filled in here: their placeholders ("<#/#>",
    // "<# of #>"...) showed around SlopDash's own (0.0.109).
    static const wchar_t* const kArcadeParts[] = { L"scnGame", L"scnAchievements", L"scnTitleLeaderboard" };
    for (int i = 0; i < 3; i++)
    {
        HXUIOBJ part = Child(panel, kArcadeParts[i]);
        if (part)
            XuiElementSetShow(part, FALSE);
    }
    if (g.place != kPlaceGames)
    {
        // An application or emulator: its cover (XboxUnity has some homebrew's)
        // or its own icon (if it has one), what it is and where it lives (no
        // description: it isn't in the catalog).
        bool hasShot = !cover.empty();
        Place(shot, 12, 46, 110, 150, hasShot);
        if (shot && hasShot)
            XuiImageElementSetImagePath(shot, cover.c_str());
        for (int i = 0; i < 4; i++)
            if (HXUIOBJ part = Child(panel, kChrome[i]))
                XuiElementSetShow(part, i < 3);
        const bool hasIcon = true;      // (one of its own, fetched, found beside it, or 1888's placeholder)
        Place(icon, 12, 52, 64, 64, !hasShot && hasIcon);
        if (icon && !hasShot && hasIcon)
            XuiImageElementSetImagePath(icon, IconLocator(index).c_str());
        float x = hasShot ? 134.0f : hasIcon ? 88.0f : 12.0f;
        Place(kind, x, hasShot ? 46.0f : 52.0f, 393 - x, hasShot ? 150.0f : 64.0f, true);
        if (kind)
            XuiControlSetText(kind, g.place == kPlaceApps ? L"Application" : L"Emulator");
        Place(desc, 12, hasShot ? 206.0f : 128.0f, 381, hasShot ? 231.0f : 300.0f, true);
        std::string folder = g.path.substr(0, g.path.find_last_of('\\'));
        if (desc)
            XuiControlSetText(desc, std::wstring(folder.begin(), folder.end()).c_str());
        return;
    }
    if (g_listCoverMode)
    {
        // List V2: the cover alone, filling the whole panel (the game's icon, as
        // big as it goes well, if there's no cover).
        Place(kind, 88, 52, 305, 64, false);
        Place(desc, 12, 128, 381, 300, false);
        Place(shot, 0, 0, 405, 443, !cover.empty());
        if (shot && !cover.empty())
            XuiImageElementSetImagePath(shot, cover.c_str());
        Place(icon, (405 - 192) / 2, (443 - 192) / 2, 192, 192, cover.empty());
        if (icon && cover.empty())
            XuiImageElementSetImagePath(icon, IconLocator(index).c_str());
        return;
    }
    if (GameDetails(index, d))
    {
        // The front cover (219 x 300, shown at 110 x 150) top left, the
        // developer / genre / year beside it, the description underneath.
        bool hasShot = !cover.empty();
        Place(shot, 12, 46, 110, 150, hasShot);
        if (shot && hasShot)
            XuiImageElementSetImagePath(shot, cover.c_str());
        Place(icon, 12, 52, 64, 64, false);
        std::wstring line = d.developer.empty() ? d.publisher : d.developer;
        if (!d.genre.empty())
            line += (line.empty() ? L"" : L"\r\n") + d.genre;
        if (!d.released.empty())
            line += (line.empty() ? L"" : L"\r\n") + d.released;
        if (hasShot)
            Place(kind, 134, 46, 259, 150, true);
        else
            Place(kind, 12, 46, 381, 58, true);
        if (kind)
            XuiControlSetText(kind, line.c_str());
        float top = hasShot ? 206.0f : 110.0f;
        Place(desc, 12, top, 381, 443 - top - 6, true);
        if (desc)
            XuiControlSetText(desc, d.description.c_str());
        return;
    }
    // Not in the catalog: the cover the second look found (Original Xbox,
    // homebrew), as the catalog's is placed, else the game's icon.
    bool hasShot = !cover.empty();
    Place(shot, 12, 46, 110, 150, hasShot);
    if (shot && hasShot)
        XuiImageElementSetImagePath(shot, cover.c_str());
    Place(icon, 12, 52, 64, 64, !hasShot);
    if (icon && !hasShot)
        XuiImageElementSetImagePath(icon, IconLocator(index).c_str());
    Place(kind, hasShot ? 134.0f : 88.0f, hasShot ? 46.0f : 52.0f, hasShot ? 259.0f : 305.0f, hasShot ? 150.0f : 64.0f, true);
    if (kind)
    {
        wchar_t buf[96];
        _snwprintf_s(buf, 96, _TRUNCATE, L"%s\r\nTitle ID %08X", KindText(g.kind), g.titleId);
        XuiControlSetText(kind, buf);
    }
    Place(desc, 12, hasShot ? 206.0f : 128.0f, 381, hasShot ? 231.0f : 300.0f, true);
    if (desc)
        XuiControlSetText(desc, IsBoilerplate(g.description) ? L"" : g.description.c_str());
}

// Nothing in this list (no emulators installed yet...): the list's count and
// panel go, and a line says so ("No emulators found.", nothing about the Marketplace).
void ShowEmptyLibrary(HXUIOBJ scene)
{
    static const wchar_t* const kWhy[] = { L"No games found.", L"No Arcade games found.", L"No games found.",
                                           L"No applications found.", L"No emulators found." };
    if (HXUIOBJ panel = Descendant(scene, L"scnTitle"))
        XuiElementSetShow(panel, FALSE);
    if (HXUIOBJ totals = Descendant(scene, L"lblTotals"))
        XuiControlSetText(totals, L"");
    HXUIOBJ line = Ensure(scene, L"rdEmpty", L"XuiLabel", L"XuiLabelCenterJustify", 132, 290, 855, 75);
    if (line)
        XuiControlSetText(line, BackgroundScanBusy() ? L"Searching for games..." : kWhy[CurrentLibraryScope()]);
}

void ShowSelectedGame(HXUIOBJ scene, int item)
{
    const std::vector<library::Game>& games = Games();
    if (games.empty())
    {
        ShowEmptyLibrary(scene);
        return;
    }
    if (item < 0 || item >= (int)games.size())
        return;
    HXUIOBJ panel = Descendant(scene, L"scnTitle");
    if (HXUIOBJ empty = Child(scene, L"rdEmpty"))      // (found some after a Refresh)
    {
        XuiElementSetShow(empty, FALSE);
        if (panel)
            XuiElementSetShow(panel, TRUE);
    }
    HXUIOBJ title = Descendant(panel, L"lblTitle");
    if (title)
        XuiControlSetText(title, games[item].name.c_str());
    if (panel)
    {
        ShowGameInfo(panel, (size_t)item);
        // The panel's data is here: the original hid its loading spinner now.
        HXUIOBJ loading = Child(panel, L"ctrlLoading");
        if (loading)
            XuiElementSetShow(loading, FALSE);
    }
    HXUIOBJ totals = Descendant(scene, L"lblTotals");
    if (totals)
    {
        wchar_t buf[32];
        _snwprintf_s(buf, 32, _TRUNCATE, L"%d of %d", item + 1, (int)games.size());
        XuiControlSetText(totals, buf);
    }
}

#include "theme/library_views.inc"

const DWORD kRefreshTimer = 0x52465348;    // "RFSH"
const DWORD kPanelTimer = 0x504E4C31;      // "PNL1": the info panel, once the selection rests
const DWORD kLibraryPollTimer = 0x4C504F4C; // "LPOL": scan paths changed in Library Settings
HRESULT OpenLibrarySettings(HXUIOBJ from);  // system_settings.inc
DWORD g_lastSettingsOpen = 0;
HRESULT OpenGameMenu(HXUIOBJ from, int game);   // game_menu.inc

bool Within(HXUIOBJ obj, HXUIOBJ ancestor)
{
    for (HXUIOBJ p = obj; p; XuiElementGetParent(p, &p))
        if (p == ancestor)
            return true;
    return false;
}

// X: Refresh. Scans the drives again (games added, moved or deleted since
// start-up), then the list, its icons and the info panel follow. The fetcher is
// stopped first (its entries follow the game list) and started again after, so
// new games get their details and covers too. rescan false: the list was
// already replaced (a drive plugged in or out, scanned in the background),
// and showed `shown` entries before.
void RefreshLibrary(HXUIOBJ scene, bool rescan = true, int shown = 0)
{
    HXUIOBJ list = Descendant(scene, L"listTitles");
    int before = rescan ? (int)Games().size() : shown;
    if (rescan && (BackgroundScanBusy() || !StopFetcher()))
    {
        if (HXUIOBJ t = Descendant(scene, L"lblTotals")) XuiControlSetText(t, L"Busy: try again");
        return;
    }
    if (rescan)
        ScanLibrary();
    g_iconPaths.clear();
    if (rescan)
        StartFetcher();
    int after = (int)Games().size();
    if (list)
    {
        XuiListDeleteItems(list, 0, before);
        XuiListInsertItems(list, 0, after);
        if (after > 0)
            XuiListSetCurSel(list, 0);
    }
    Log("library refreshed: %d -> %d games", before, after);
    if (after > 0)
        ShowSelectedGame(scene, 0);
    else
        ShowEmptyLibrary(scene);                // ("No games found." in place of "Searching...")
    if (g_lv.scene == scene)
        ApplyView(scene);                       // the view shows the new list
}

// X: the highlighted game's menu (Launch Game, achievements, saves, DLC, trainers...).
void OpenSelectedGameMenu(HXUIOBJ obj)
{
    HXUIOBJ list = Descendant(obj, L"listTitles");
    int item = list ? XuiListGetCurSel(list, NULL) : -1;
    if (item < 0 || item >= (int)Games().size() || GetTickCount() - g_lastSettingsOpen <= 500)
        return;
    g_lastSettingsOpen = GetTickCount();        // (X arrives as a key and as the legend's press: one menu)
    OpenGameMenu(obj, item);
}

HRESULT TitleSelectorProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (ViewMessage(obj, msg))
        return S_OK;
    if (msg->dwMessage == XM_INIT)
    {
        XuiSetTimer(obj, kSelectorTimer, 50);
        // The file's header is "My Arcade Games"; the same screen is also the
        // Played Games list (the whole library) and the Game Library (all but
        // Arcade), opened from those buttons.
        HXUIOBJ title = Child(obj, L"labTitle");
        static const wchar_t* const kHeaders[] = { L"Played Games", NULL, L"Game Library", L"Applications",
                                                   L"Emulators" };     // (LibraryScope order)
        if (title && kHeaders[CurrentLibraryScope()])
            XuiControlSetText(title, kHeaders[CurrentLibraryScope()]);
        // The buttons: A
        // starts the game, X its menu (saves, DLC, trainers...), Back searches
        // the drives again (Refresh, on X before).
        HXUIOBJ x = Child(obj, L"btnX");
        if (x)
        {
            XuiControlSetText(x, L"Game Menu");
            XuiControlSetEnable(x, TRUE);
            XuiElementSetShow(x, TRUE);
        }
        HXUIOBJ y = Child(obj, L"btnY");
        if (y)
        {
            XuiControlSetText(y, L"Settings");
            XuiControlSetEnable(y, TRUE);
            XuiElementSetShow(y, TRUE);
        }
        XuiSetTimer(obj, kLibraryPollTimer, 500);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kRefreshTimer)
    {
        XuiKillTimer(obj, kRefreshTimer);
        RefreshLibrary(obj);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kPanelTimer)
    {
        XuiKillTimer(obj, kPanelTimer);
        HXUIOBJ list = Descendant(obj, L"listTitles");
        if (list)
            ShowSelectedGame(obj, XuiListGetCurSel(list, NULL));
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kLibraryPollTimer)
    {
        size_t shown = 0;
        if (TakeLibraryReplaced(shown))
            RefreshLibrary(obj, false, (int)shown);     // a drive plugged in or out
        else if (TakeLibraryChanged())
        {
            Log("library: scan paths changed; searching again");
            RefreshLibrary(obj);
        }
        else if (LibraryViewStamp() != g_lv.stamp)
            ApplyView(obj);                     // a view or its options chosen in Library Settings
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_Y)
    {
        if (GetTickCount() - g_lastSettingsOpen > 500)
        {
            g_lastSettingsOpen = GetTickCount();
            OpenLibrarySettings(obj);
        }
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_X)
    {
        // In case the legend button doesn't turn X into its own press.
        OpenSelectedGameMenu(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_BACK)
    {
        // A frame for "Refreshing..." to show; the scan runs on the next tick.
        HXUIOBJ totals = Descendant(obj, L"lblTotals");
        if (totals)
            XuiControlSetText(totals, L"Refreshing...");
        XuiSetTimer(obj, kRefreshTimer, 30);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kSelectorTimer)
    {
        XuiKillTimer(obj, kSelectorTimer);
        // The list's focus frame (metaScene_2lineFocus_Long): the original moved
        // it with the selection; static, it's just stray lines, so it's hidden.
        HXUIOBJ frame = Child(obj, L"metaPanelScene");
        if (frame)
            XuiElementSetShow(frame, FALSE);
        HXUIOBJ list = Descendant(obj, L"listTitles");
        ShowSelectedGame(obj, list ? XuiListGetCurSel(list, NULL) : 0);
        ApplyView(obj);
    }
    else if (msg->dwMessage == XM_NOTIFY)
    {
        XUINotify* n = (XUINotify*)msg->pvData;
        if (n->dwNotify == XN_SELCHANGED)
        {
            int item = ((XUINotifySelChanged*)n->pvData)->iItem;
            if (IsListView(g_lv.id))
            {
                // The list views: the count now, the panel (a cover JPEG
                // loaded and drawn the first time: 50-60 ms a move in the
                // crawl of 2026-10-14) once the selection rests, so holding
                // down scrolls the list alone.
                HXUIOBJ totals = Descendant(obj, L"lblTotals");
                if (totals && item >= 0 && item < (int)Games().size())
                {
                    wchar_t buf[32];
                    _snwprintf_s(buf, 32, _TRUNCATE, L"%d of %d", item + 1, (int)Games().size());
                    XuiControlSetText(totals, buf);
                }
                XuiKillTimer(obj, kPanelTimer);
                XuiSetTimer(obj, kPanelTimer, 120);
            }
            else
                ShowSelectedGame(obj, item);
            if (g_lv.scene == obj && !IsListView(g_lv.id) && item != g_lv.sel)
                ViewSelect(item, g_lv.sel);     // up/down on the list in Cover Flow etc.
        }
        else if (n->dwNotify == XN_PRESS && ElementId(n->hObjSource) == L"btnY")
        {
            // Y arrives as the legend's press and perhaps as a key too: one screen.
            if (GetTickCount() - g_lastSettingsOpen > 500)
            {
                g_lastSettingsOpen = GetTickCount();
                OpenLibrarySettings(obj);
            }
            msg->bHandled = TRUE;
        }
        else if (n->dwNotify == XN_PRESS && ElementId(n->hObjSource) == L"btnX")
        {
            OpenSelectedGameMenu(obj);
            msg->bHandled = TRUE;
        }
        else if (n->dwNotify == XN_PRESS &&
                 (Within(n->hObjSource, Descendant(obj, L"listTitles")) || ElementId(n->hObjSource) == L"btnA"))
        {
            // A on a game starts it (its menu is on X).
            HXUIOBJ list = Descendant(obj, L"listTitles");
            int item = list ? XuiListGetCurSel(list, NULL) : -1;
            const std::vector<library::Game>& games = Games();
            if (item >= 0 && item < (int)games.size())
            {
                msg->bHandled = TRUE;
                if (GetTickCount() - g_lastSettingsOpen > 500)
                {
                    g_lastSettingsOpen = GetTickCount();    // A arrives twice (list and legend): once
                    LaunchGame(games[item]);                // (returns only if it failed)
                }
            }
        }
    }
    return S_OK;
}

// --- CConnStatus (System > Network Settings, network#ConnStatus.xur) ---------
// Its status bar (network name, link and activity lights, Wi-Fi signal) and the
// settings screen's info pane ("<meta data>") were filled by the dashboard's
// code. Here they show the console's real network state.
const StandInInfo kConnStatus = { L"CConnStatus", L"XuiScene", 0, { 0 } };
const DWORD kConnTimer = 0x434F4E4E;   // "CONN"

void Show(HXUIOBJ parent, const wchar_t* id, bool show)
{
    HXUIOBJ h = Descendant(parent, id);
    if (h)
        XuiElementSetShow(h, show);
}

void FillNetworkStatus(HXUIOBJ scene)
{
    NetStartup();
    XNADDR xna;
    memset(&xna, 0, sizeof(xna));
    DWORD addr = XNetGetTitleXnAddr(&xna);
    DWORD link = XNetGetEthernetLinkStatus();
    bool active = (link & XNET_ETHERNET_LINK_ACTIVE) != 0, wireless = (link & XNET_ETHERNET_LINK_WIRELESS) != 0;

    // Status bar.
    HXUIOBJ ssid = Descendant(scene, L"txt_SSID");
    if (ssid)
        XuiControlSetText(ssid, !active ? L"Network: Not connected" : wireless ? L"Network: Wireless"
                                                                               : L"Network: Wired");
    Show(scene, L"img_Link_Connected", active);
    Show(scene, L"img_Link_Disconnected", !active);
    Show(scene, L"img_Act", active);
    Show(scene, L"img_inAct", !active);
    for (int i = 0; i <= 4; i++)
    {
        wchar_t id[16];
        _snwprintf_s(id, 16, _TRUNCATE, L"img_Signal%d", i);
        Show(scene, id, false);             // signal strength isn't available to a title
    }
    Show(scene, L"ctlLoading", false);

    // The info pane.
    wchar_t text[600];
    const wchar_t* how = (addr & XNET_GET_XNADDR_DHCP) ? L"DHCP" : (addr & XNET_GET_XNADDR_STATIC) ? L"static"
                       : (addr & XNET_GET_XNADDR_PPPOE) ? L"PPPoE" : L"none";
    const BYTE* ip = (const BYTE*)&xna.ina;
    int internet = LastRequestResult();
    _snwprintf_s(text, 600, _TRUNCATE,
                 L"Connection: %s%s%s\r\n"
                 L"IP address: %u.%u.%u.%u (%s)\r\n"
                 L"MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n"
                 L"Gateway: %s\r\n"
                 L"DNS: %s\r\n"
                 L"Internet: %s",
                 !active ? L"not connected" : wireless ? L"wireless" : L"wired",
                 !active || wireless ? L"" : (link & XNET_ETHERNET_LINK_100MBPS) ? L", 100 Mbps" : L", 10 Mbps",
                 !active || wireless ? L"" : (link & XNET_ETHERNET_LINK_FULL_DUPLEX) ? L", full duplex" : L", half duplex",
                 ip[0], ip[1], ip[2], ip[3], how,
                 xna.abEnet[0], xna.abEnet[1], xna.abEnet[2], xna.abEnet[3], xna.abEnet[4], xna.abEnet[5],
                 (addr & XNET_GET_XNADDR_GATEWAY) ? L"configured" : L"none",
                 (addr & XNET_GET_XNADDR_DNS) ? L"configured" : L"none",
                 internet > 0 ? L"reachable" : internet < 0 ? L"not reachable" : L"not checked yet");
    HXUIOBJ meta = Descendant(scene, L"txt_MetaBody");
    if (meta)
        XuiControlSetText(meta, text);
    Log("network status: link %08X, address %08X", (unsigned)link, (unsigned)addr);
}

HRESULT ConnStatusProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kConnTimer, 50);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kConnTimer)
    {
        // Refreshed every two seconds while the screen is open (a cable can be pulled).
        XuiKillTimer(obj, kConnTimer);
        FillNetworkStatus(obj);
        XuiSetTimer(obj, kConnTimer, 2000);
    }
    return S_OK;
}

// --- XuiBOTDOfflineContainer -------------------------------------------------
// The "billboard of the day" slots (Xbox LIVE and Marketplace blades). Online,
// the dashboard put the day's promotions in them; offline it showed the default
// banner its P1 names (botd#defaultbanner0.xur and friends), as here.
const StandInInfo kBotdContainer = { L"XuiBOTDOfflineContainer", L"XuiScene", 2, { 3, 5 } };
const DWORD kBotdTimer = 0x424F5444;        // "BOTD"

// The Xbox LIVE blade's billboard shows the game played last (dashcomm#
// TitleMetadata.xur: its tile, gamerscore and recent achievements), as the
// dashboard did; without one, its default banner. game_menu.inc fills it in.
bool StartRecentGame(HXUIOBJ container);
bool PollRecentGame(HXUIOBJ container);         // true when done
const DWORD kRecentTimer = 0x52434E54;          // "RCNT"

// A banner's looping animations ("loop" .. "endloop": the Xbox LIVE strip's
// rings), which the dashboard started; the elements play them themselves.
void PlayBannerLoops(HXUIOBJ obj, int depth)
{
    int start = -1, end = -1;
    if (SUCCEEDED(XuiElementFindNamedFrame(obj, L"loop", &start)) &&
        SUCCEEDED(XuiElementFindNamedFrame(obj, L"endloop", &end)))
    {
        // Banner ("loop" .. "endloop") holds the frames; its child group "loop"
        // the rings' timelines, and Banner doesn't pass playing down
        // (DisableTimelineRecursion): each child is started with its range.
        XuiElementPlayTimeline(obj, start, start, end, TRUE, FALSE);
        for (HXUIOBJ c = NULL; SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(obj, &c)) && c;)
            XuiElementPlayTimeline(c, start, start, end, TRUE, FALSE);
    }
    for (HXUIOBJ c = NULL; depth > 0 && SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(obj, &c)) && c;)
        PlayBannerLoops(c, depth - 1);
}

// The banner a slot names in its P1. On the Xbox LIVE blade (content root a
// plain XuiScene) XUI hands it back empty, as with the gamer card's mode: then
// our own copy of the property, else the slot's usual banner.
// (XUI's own copy isn't read at all: on the Xbox LIVE blade's signed-out
// content it came back as a bad pointer, and reading it crashed SlopDash on
// sign-out, 0.0.67.)
std::wstring BannerOf(HXUIOBJ obj)
{
    const XUIElementPropVal* own = StandInProperty(obj, L"XuiBOTDOfflineContainer", 1);
    std::wstring banner = own && own->type == XUI_EPT_STRING && own->szVal ? own->szVal : L"";
    if (banner.empty() || banner.find(L".xur") == std::wstring::npos)
    {
        std::wstring id = ElementId(obj);
        banner = id == L"btn_AdBanner" ? L"defaultbanner1.xur" : id == L"botdBillboard" ? L"defaultbanner0.xur" : L"";
    }
    return banner;
}

HRESULT BotdContainerProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kBotdTimer, 50);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kBotdTimer)
    {
        XuiKillTimer(obj, kBotdTimer);
        if (ElementId(obj) == L"btn_AdBanner")
            Log("banner slot look btn_AdBanner: %s", SetStringProperty(obj, L"Visual", L"btn_AdBanner") ? "set" : "FAILED");
        std::wstring banner = BannerOf(obj);
        Log("banner %S: %S", ElementId(obj).c_str(), banner.c_str());
        if (ElementId(obj) == L"botdBillboard" && StartRecentGame(obj))
            XuiSetTimer(obj, kRecentTimer, 250);
        else if (!banner.empty())
        {
            HXUIOBJ shown = NULL;
            AddScene(obj, L"botd", banner.c_str(), 0, 0, &shown);
            if (shown)
                PlayBannerLoops(shown, 4);
        }
        // The banners draw past their slot (the Xbox LIVE strip's ring pattern
        // is 406 x 152 in a 345 x 94 slot, with soft edges meant to be cut off):
        // the slot clips, as the dashboard's own did. Unclipped, the edges
        // showed as a blur round the banner.
        SetBoolProperty(obj, L"ClipChildren", true);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kRecentTimer)
    {
        if (PollRecentGame(obj))
            XuiKillTimer(obj, kRecentTimer);
    }
    return S_OK;
}

// --- The disc tray (TraySceneLoader: dashcomm#TrayScene.xur) -----------------------
// The bar at the bottom of each blade. Tab1's button (btn_Tray) says what's in
// the drive; A starts a game disc. The drive is looked at on a worker thread
// every few seconds (a disc spinning up takes a while).
const StandInInfo kTrayLoader = { L"TraySceneLoader", L"XuiScene", 0, { 0 } };
const DWORD kTrayTimer = 0x54524159;            // "TRAY"
CRITICAL_SECTION g_discLock;
std::wstring g_discText = L"No Disc";
std::string g_discLaunch;                       // "dvd:\\default.xex" for a game disc, else ""

bool FileThere(const char* path)
{
    return GetFileAttributesA(path) != (DWORD)-1;
}

HANDLE g_discEvent = NULL;                      // set: look at the drive again
volatile LONG g_trayOpen = 0;                   // opened from the bar: the drive is left alone

// Is there a drive at all? (Not every console has one.) An empty drive answers
// "not ready"; no drive, something else.
bool DrivePresent()
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("dvd:\\*", &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        FindClose(h);
        return true;
    }
    DWORD e = GetLastError();
    Log("disc drive: probe %u", (unsigned)e);
    return e == ERROR_NOT_READY || e == ERROR_FILE_NOT_FOUND || e == ERROR_NO_MORE_FILES;
}

// No drive: said once, and never looked at again. With one: a few looks, a
// second apart (a disc takes a moment to spin up), at start-up and after each
// storage change (a disc in or out); nothing in between.
DWORD WINAPI DiscThread(LPVOID)
{
    if (!DrivePresent())
    {
        EnterCriticalSection(&g_discLock);
        g_discText = L"No Disc Drive";
        g_discLaunch.clear();
        LeaveCriticalSection(&g_discLock);
        Log("disc tray: no disc drive");
        return 0;
    }
    for (;;)
    {
      for (int look = 0; look < 5 && !g_trayOpen; look++)
      {
        Log("disc tray: looking (%d)", look);
        std::wstring text;
        std::string launch;
        if (FileThere("dvd:\\default.xex"))
        {
            text = L"Xbox 360 Game";
            launch = "dvd:\\default.xex";
            HANDLE h = CreateFileA("dvd:\\default.xex", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE)
            {
                DWORD size = GetFileSize(h, NULL), got = 0;
                if (size > 0 && size <= 32 * 1024 * 1024)
                {
                    std::vector<unsigned char> data(size);
                    library::Game g;
                    if (ReadFile(h, &data[0], size, &got, NULL) && got == size &&
                        library::ReadXex(&data[0], size, g) && !g.name.empty())
                        text = g.name;
                }
                CloseHandle(h);
            }
        }
        else if (FileThere("dvd:\\default.xbe"))
        {
            text = L"Xbox Game";
            launch = "dvd:\\default.xbe";
        }
        else if (FileThere("dvd:\\VIDEO_TS"))
            text = L"DVD Video";
        else if (FileThere("dvd:\\"))
            text = L"Unplayable Disc";
        else
            text = L"No Disc";
        EnterCriticalSection(&g_discLock);
        if (text != g_discText)
            Log("disc tray: %S", text.c_str());
        g_discText = text;
        g_discLaunch = launch;
        LeaveCriticalSection(&g_discLock);
        Sleep(1000);
      }
      WaitForSingleObject(g_discEvent, INFINITE);
      Log("disc tray: asked to look again%s", g_trayOpen ? " (tray open: not now)" : "");
    }
}

void StartDiscWatch()
{
    static bool started = false;
    if (started)
        return;
    started = true;
    InitializeCriticalSection(&g_discLock);
    g_discEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, DiscThread, NULL, CREATE_SUSPENDED, NULL);
    if (t)
    {
        XSetThreadProcessor(t, 5);
        ResumeThread(t);
        CloseHandle(t);
    }
}

void ShowDisc(HXUIOBJ tray)
{
    HXUIOBJ btn = Descendant(tray, L"btn_Tray");
    if (!btn)
        return;
    EnterCriticalSection(&g_discLock);
    std::wstring text = g_discText;
    LeaveCriticalSection(&g_discLock);
    XuiControlSetText(btn, text.c_str());
}

HRESULT TrayLoaderProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        StartDiscWatch();
        XuiSetTimer(obj, kTrayTimer, 50);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kTrayTimer)
    {
        if (!Child(obj, L"TrayScene"))
        {
            // The bar itself (Tab1); the expanded drive panel (Tab2) isn't used.
            HXUIOBJ tray = NULL;
            AddScene(obj, L"dashcomm", L"TrayScene.xur", 0, 0, &tray);
            // The scene starts its own timeline on loading: "1To2", the bar
            // (Tab1) fading out for the expanded panel (Tab2). That faded the
            // bar away (0.0.64/0.0.65: seen for a moment, then gone). Stopped,
            // and held at "2To1End": the bar showing, the panel not.
            if (tray)
            {
                XuiElementStopTimeline(tray, TRUE);
                int frame = -1;
                if (SUCCEEDED(XuiElementFindNamedFrame(tray, L"2To1End", &frame)))
                    XuiElementPlayTimeline(tray, frame, frame, frame, FALSE, FALSE);
                HXUIOBJ bar = Child(tray, L"Tab1");
                if (bar)
                {
                    XuiElementSetOpacity(bar, 1.0f);
                    XuiElementSetShow(bar, TRUE);
                }
            }
            HXUIOBJ big = tray ? Child(tray, L"Tab2") : NULL;
            if (big)
                XuiElementSetShow(big, FALSE);
            XuiKillTimer(obj, kTrayTimer);
            XuiSetTimer(obj, kTrayTimer, 1000);
        }
        ShowDisc(obj);
    }
    else if (msg->dwMessage == XM_SET_FOCUS)
    {
        // Focus on the bar's slot goes to its button.
        HXUIOBJ btn = Descendant(obj, L"btn_Tray");
        if (btn && XuiElementGetFocus() != btn)
            XuiElementSetFocus(btn);
    }
    else if (msg->dwMessage == XM_KEYDOWN && (((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_DPAD_UP ||
                                              ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_LTHUMB_UP))
    {
        // Up leaves the bar for the content's item above it (the slot's NavUp;
        // the button inside the loaded bar has no link of its own).
        HXUIOBJ content = NULL;
        XuiElementGetParent(obj, &content);
        std::wstring up = StringProperty(obj, L"NavUp");
        HXUIOBJ target = content && !up.empty() ? Child(content, up.c_str()) : NULL;
        static const wchar_t* const kAbove[] = { L"navGames", L"navMCX", L"btn_AdBanner", L"navArcade", L"btnJoin" };
        for (int i = 0; !target && content && i < 5; i++)
            target = Child(content, kAbove[i]);
        if (target)
        {
            XuiElementSetFocus(target);
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS &&
             ElementId(((XUINotify*)msg->pvData)->hObjSource) == L"btn_Tray")
    {
        EnterCriticalSection(&g_discLock);
        std::string launch = g_discLaunch;
        LeaveCriticalSection(&g_discLock);
        Log("disc tray: pressed (%s)", launch.empty() ? "open/close" : launch.c_str());
        if (!launch.empty())
        {
            LogFlush();
            XLaunchNewImage(launch.c_str(), 0);
        }
        else
        {
            // Open, or close: the drive's tray (kernel HalOpenCloseODDTray). After
            // closing, the drive is looked at again for a few seconds.
            static bool open = false;
            open = !open;
            InterlockedExchange(&g_trayOpen, open ? 1 : 0);
            Log("disc tray: %s", open ? "opening" : "closing");
            HalOpenCloseODDTray(open ? 1 : 0);
            Log("disc tray: tray call returned");
            EnterCriticalSection(&g_discLock);
            g_discText = open ? L"Tray Open" : L"Reading...";
            g_discLaunch.clear();
            LeaveCriticalSection(&g_discLock);
            Log("disc tray: text set");
            ShowDisc(obj);
            Log("disc tray: shown");
            if (!open)
                RequestDiscCheck();
        }
        msg->bHandled = TRUE;
    }
    return S_OK;
}

const DWORD kGamerCardTimer = 0x47434152;   // "GCAR"
const DWORD kCardFocusTimer = 0x47434646;   // "GCFF": the card's highlight follows the focus
std::map<HXUIOBJ, bool> g_cardLit;

HRESULT GamerCardProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal* vals, const bool* set)
{
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kCardFocusTimer)
    {
        // The card's frame is its baseMetaPane button (visual btn_Gamercard). The
        // card, not the button, takes the focus, so the button never played its
        // highlight; it's played here, as the dashboard's card did. By the
        // focus itself, looked at four times a second: the focus messages don't
        // come when a blade hands the card the focus (0.0.65 lit up only once
        // the stick moved).
        bool focused = false;
        for (HXUIOBJ f = XuiElementGetFocus(); f; XuiElementGetParent(f, &f))
            if (f == obj)
                focused = true;
        bool& lit = g_cardLit[obj];
        // The card's extended part (status, recent achievements) showed over the
        // Xbox LIVE blade's buttons (0.0.67): kept hidden.
        HXUIOBJ extended = Descendant(obj, L"extendedScene");
        if (extended && XuiElementIsShown(extended))
            XuiElementSetShow(extended, FALSE);
        HXUIOBJ pane = Descendant(obj, L"baseMetaPane");
        if (pane && focused != lit)
        {
            if (focused)
                XuiControlPlayVisualRangeEx(pane, L"Focus", L"Focus", L"EndFocus", FALSE);
            else
                XuiControlPlayVisualRange(pane, L"KillFocus", L"KillFocus", L"EndNormal");
            lit = focused;
        }
        return S_OK;
    }
    if (msg->dwMessage == XM_DESTROY)
        g_cardLit.erase(obj);
    else if (msg->dwMessage == XM_INIT)
    {
        XuiSetTimer(obj, kGamerCardTimer, 100);
        XuiSetTimer(obj, kCardFocusTimer, 250);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kGamerCardTimer)
    {
        XuiKillTimer(obj, kGamerCardTimer);
        BuildGamerCard(obj, vals, set);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kProfileReadTimer)
    {
        CardNumbers n;
        bool ok;
        if (ProfileReadDone(n, ok))
        {
            XuiKillTimer(obj, kProfileReadTimer);
            Log("gamer card: gamerscore %d, rep %d/100, zone %d", n.gamerscore, (int)n.rep, n.zone);
            HXUIOBJ online = Descendant(obj, L"onlineBaseScene");
            if (ok && online)
                ShowCardNumbers(online, n);
        }
    }
    return S_OK;
}

// The Games blade. dashmain.xur leaves it empty (scBlade); the dashboard's own
// code filled it with gamesbla#gamesSignedIn.xur, or gamesSignedOut.xur when
// nobody is signed in.
//
// Where it goes: dashmain.xur gives each code-filled blade an empty slot,
// "scContainer" (700 x 369; the Games blade's is at (221, 151), just under its
// "Games" header). The content goes in there at (0, 0), and that slot is also
// what receives focus when the blade becomes current.
// What one blade shows: its content when someone is signed in and when nobody
// is, and any parts of it to hide. Games, Media and Xbox LIVE start with the
// gamer card; Marketplace has none. Xbox LIVE and Marketplace are shown as the
// original laid them out (their buttons need Xbox LIVE and do nothing yet).
struct BladeContent
{
    const wchar_t* name;            // for the log
    const wchar_t* package;
    const wchar_t* signedIn;
    const wchar_t* signedOut;
    const wchar_t* const* hide;     // NULL-terminated ids
};
const wchar_t* const kNoHidden[] = { NULL };
const wchar_t* const kMediaHidden[] = { L"navMediaMP", NULL };             // Video Store
const BladeContent kGamesContent = { L"Games", L"gamesbla", L"gamesSignedIn.xur", L"gamesSignedOut.xur", kNoHidden };
const BladeContent kMediaContent = { L"Media", L"mediabla", L"mediaSignedIn.xur", L"mediaSignedOut.xur", kMediaHidden };
const BladeContent kLiveContent = { L"Xbox LIVE", L"live", L"liveSignedInUI.xur", L"liveSignedOutUI.xur", kNoHidden };
const BladeContent kMarketplaceContent = { L"Marketplace", L"blademp", L"marketplaceSignedIn.xur",
                                           L"marketplaceSignedOut.xur", kNoHidden };

// Sent to a profile blade to open its profile list.
const DWORD kOpenProfilesMessage = XM_USER + 0x5244;

class CGamesBladeScene : public CXuiSceneImpl
{
public:
    XUI_IMPLEMENT_CLASS(CGamesBladeScene, L"CGamesBladeScene", XUI_CLASS_SCENE)

    XUI_BEGIN_MSG_MAP()
        XUI_ON_XM_INIT(OnInit)
        if (pMessage->dwMessage == kOpenProfilesMessage)
        {
            OpenPicker();           // A on the "Sign In" card (from the main loop)
            pMessage->bHandled = TRUE;
            return S_OK;
        }
        XUI_ON_XM_TIMER(OnTimer)
        XUI_ON_XM_KEYDOWN(OnKeyDown)
        XUI_ON_XM_SET_FOCUS(OnSetFocus)
        XUI_ON_XM_NOTIFY_SET_FOCUS(OnNotifySetFocus)
        XUI_ON_XM_NOTIFY_PRESS(OnNotifyPress)
    XUI_END_MSG_MAP()

    CGamesBladeScene() : m_cfg(&kGamesContent), m_container(NULL), m_content(NULL), m_picker(NULL), m_pickerFocus(NULL), m_open(false), m_closePending(false), m_who(0) {}

    HRESULT OnInit(XUIMessageInit*, BOOL& bHandled)
    {
        m_container = Child(m_hObj, L"scContainer");
        Log("%S blade: slot %s", m_cfg->name, m_container ? "found" : "MISSING");
        m_who = WhoIsSignedIn();
        ScanProfiles();
        AddContent();
        XuiSetTimer(m_hObj, kSigninTimer, 1000);
        bHandled = FALSE;           // let XuiScene do its own init too
        return S_OK;
    }

    // The signed-in profile's XUID; 0 for nobody.
    static XUID WhoIsSignedIn()
    {
        int user = SignedInUser();
        if (user < 0)
            return 0;
        XUID xuid = 0;
        if (XUserGetXUID(user, &xuid) != ERROR_SUCCESS || xuid == 0)
            xuid = 1;               // signed in, XUID unknown: still someone
        return xuid;
    }

    void AddContent()
    {
        bool signedIn = m_who != 0;
        Log("%S blade: profile %s", m_cfg->name, signedIn ? "signed in" : "not signed in");
        AddScene(m_container ? m_container : m_hObj, m_cfg->package, signedIn ? m_cfg->signedIn : m_cfg->signedOut,
                 0, 0, &m_content);
        if (!m_content)
            return;
        for (const wchar_t* const* h = m_cfg->hide; *h; h++)
        {
            HXUIOBJ part = Descendant(m_content, *h);
            if (part)
                XuiElementSetShow(part, FALSE);
        }
        // Media: Video Store (navMediaMP, hidden: it needs Xbox LIVE) sat between
        // Videos and Media Center, leaving a gap. Media Center takes its place.
        HXUIOBJ store = Child(m_content, L"navMediaMP"), mcx = Child(m_content, L"navMCX");
        HXUIOBJ videos = Child(m_content, L"navVideo");
        if (store && mcx && videos && !XuiElementIsShown(store))
        {
            D3DXVECTOR3 at;
            XuiElementGetPosition(store, &at);
            XuiElementSetPosition(mcx, &at);
            SetStringProperty(videos, L"NavDown", L"navMCX");
            SetStringProperty(mcx, L"NavUp", L"navVideo");
        }
        // The Xbox LIVE logo has no size mode in its file, so it drew at the
        // picture's own size over the gamer card; fitted to its box like the
        // Games blade's logo (SizeMode 16).
        HXUIOBJ logo = Child(m_content, L"brandLogo");
        if (logo)
            SetUintProperty(logo, L"SizeMode", 0x10);
        Log("%S blade: content %S, list \"%S\"", m_cfg->name, ElementId(m_content).c_str(),
            StringProperty(m_content, L"P0").c_str());
        if (!signedIn)
            FillSignedOut();
    }

    // The signed-out card: how many profiles the console has (the dashboard
    // filled labFoundProfiles in; the card itself is the "Sign In" button).
    void FillSignedOut()
    {
        int profiles = (int)Profiles().size();
        HXUIOBJ found = Descendant(m_content, L"labFoundProfiles");
        if (found)
        {
            wchar_t buf[64];
            _snwprintf_s(buf, 64, _TRUNCATE, profiles == 1 ? L"%d Profile Found" : L"%d Profiles Found", profiles);
            XuiControlSetText(found, buf);
        }
        Log("%S blade: %d profiles on the console", m_cfg->name, profiles);
    }

    // --- the profile picker ------------------------------------------------------
    // SlopDash's own way to switch profile (the Guide's sign-in screen still
    // works too): pressing the gamer card, or the signed-out card, swaps the
    // blade's content for a list of the console's profiles; A signs that one
    // in, B goes back.
    void OpenPicker()
    {
        if (m_picker || !m_content)
            return;
        ScanProfiles();
        HXUIOBJ parent = m_container ? m_container : m_hObj;
        if (FAILED(XuiCreateObject(L"XuiScene", &m_picker)) || !m_picker)
        {
            m_picker = NULL;
            Log("profile picker: can't create it");
            return;
        }
        SetStringProperty(m_picker, L"Id", L"rdProfilePicker");
        XuiElementSetBounds(m_picker, 345, 445);
        XuiElementAddChild(parent, m_picker);
        HXUIOBJ title = Ensure(m_picker, L"rdPickerTitle", L"XuiLabel", L"Label_Body_1line", 8, 0, 330, 35);
        if (title)
            XuiControlSetText(title, L"Select Profile");

        const std::vector<ProfileEntry>& ps = Profiles();
        std::vector<std::wstring> ids;
        for (size_t i = 0; i < ps.size(); i++)
        {
            wchar_t id[24];
            _snwprintf_s(id, 24, _TRUNCATE, L"rdProfile%u", (unsigned)i);
            ids.push_back(id);
        }
        if (m_who)
            ids.push_back(L"rdSignOut");
        HXUIOBJ first = NULL, current = NULL;
        for (size_t i = 0; i < ids.size(); i++)
        {
            // XuiButton: the runtime-made XuiNavButtons of 0.0.48 took focus but sent no press.
            HXUIOBJ b = Ensure(m_picker, ids[i].c_str(), L"XuiButton", L"btn_1line_icon", 0, 40 + 47.0f * i,
                               330, 47);
            if (!b)
                continue;
            if (i < ps.size())
            {
                std::wstring text = ps[i].gamertag;
                if (ps[i].signedInAs >= 0)
                    text += L" (signed in)";
                XuiControlSetText(b, text.c_str());
                std::wstring pic = ProfilePictureLocator(ps[i].xuid);
                SetStringProperty(b, L"ImagePath", pic.empty() ? L"sharedres://ico_64x_DefaultPicture.png" : pic.c_str());
                if (ps[i].xuid == m_who)
                    current = b;
            }
            else
            {
                XuiControlSetText(b, L"Sign Out");
                SetStringProperty(b, L"ImagePath", L"sharedres://ico_32x_Friend.png");
            }
            if (i > 0)
                SetStringProperty(b, L"NavUp", ids[i - 1].c_str());
            if (i + 1 < ids.size())
                SetStringProperty(b, L"NavDown", ids[i + 1].c_str());
            if (!first)
                first = b;
        }
        XuiElementSetShow(m_content, FALSE);
        m_pickerFocus = current ? current : first;
        if (m_pickerFocus)
            XuiElementSetFocus(m_pickerFocus);
        m_pickerOpenedAt = GetTickCount();
        Log("profile picker: open, %u profiles", (unsigned)ps.size());
    }

    void ClosePicker(bool refocus)
    {
        if (!m_picker)
            return;
        // Focus goes back to the blade's buttons first: destroying the focused
        // button left nothing focused (0.0.45), and the next B then left SlopDash.
        HXUIOBJ picker = m_picker;
        m_picker = m_pickerFocus = NULL;
        if (m_content)
            XuiElementSetShow(m_content, TRUE);
        if (refocus)
        {
            BOOL handled = FALSE;
            FocusFirstItem(handled);
        }
        XuiElementUnlink(picker);
        XuiDestroyObject(picker);
        Log("profile picker: closed");
    }

    bool PickerPress(const std::wstring& id)
    {
        if (id.compare(0, 9, L"rdProfile") && id != L"rdSignOut")
            return false;
        // The A that opened the list may arrive at it too: not a choice.
        if (GetTickCount() - m_pickerOpenedAt < 400)
            return true;
        int slot = SignedInUser();
        if (slot < 0)
            slot = 0;
        XUID target = 0;
        if (id != L"rdSignOut")
        {
            size_t i = (size_t)_wtoi(id.c_str() + 9);
            if (i >= Profiles().size())
                return true;
            target = Profiles()[i].xuid;
            if (target == m_who)
            {
                RequestClosePicker();   // already signed in
                return true;
            }
        }
        bool started = StartProfileSwitch(slot, target);
        Log("profile picker: %s slot %d: %s", target ? "switching" : "signing out", slot,
            started ? "started" : "FAILED");
        RequestClosePicker();
        return true;
    }

    // A on the gamer card opens the picker (XuiGamerCard is a plain control:
    // it sends no press); B leaves the picker.
    HRESULT OnKeyDown(XUIMessageInput* in, BOOL& bHandled)
    {
        if (m_closePending)
            bHandled = TRUE;            // the picker is going: nothing more for it
        else if (m_picker && in->dwKeyCode == VK_PAD_B)
        {
            RequestClosePicker();
            bHandled = TRUE;
        }
        else if (m_picker && (in->dwKeyCode == VK_PAD_A || in->dwKeyCode == VK_PAD_START))
        {
            // In case the button doesn't turn A into a press itself.
            HXUIOBJ f = XuiElementGetFocus();
            if (f && PickerPress(ElementId(f)))
                bHandled = TRUE;
        }
        else if (!m_picker && in->dwKeyCode == VK_PAD_A && m_content && XuiElementGetFocus() &&
                 ElementId(XuiElementGetFocus()) == L"fakeGamerCard")
        {
            // Signed out: the "Sign In" card is a nav button with nowhere to go,
            // and those send no press (0.0.69): A opens the profile list here.
            OpenPicker();
            bHandled = TRUE;
        }
        else if (!m_picker && in->dwKeyCode == VK_PAD_A && m_content)
        {
            HXUIOBJ card = Child(m_content, L"ctlGamerCard");
            for (HXUIOBJ f = XuiElementGetFocus(); card && f; XuiElementGetParent(f, &f))
                if (f == card)
                {
                    OpenPicker();
                    bHandled = TRUE;
                    break;
                }
        }
        return S_OK;
    }

    // The picker can't be destroyed while XUI is still delivering one of its
    // buttons' messages (0.0.45 locked up on B): it goes on the next tick.
    void RequestClosePicker()
    {
        if (!m_picker || m_closePending)
            return;
        m_closePending = true;
        XuiSetTimer(m_hObj, kCloseTimer, 1);
    }

    // Every second: has someone signed in or out (the Guide, or a controller's
    // auto sign-in)? Then the blade swaps its content, as the dashboard did.
    HRESULT OnTimer(XUIMessageTimer* t, BOOL& bHandled)
    {
        if (t->nId == kCloseTimer)
        {
            XuiKillTimer(m_hObj, kCloseTimer);
            m_closePending = false;
            ClosePicker(true);
            bHandled = TRUE;
            return S_OK;
        }
        if (t->nId != kSigninTimer)
            return S_OK;
        bHandled = TRUE;
        XUID who = WhoIsSignedIn();
        if (who == m_who)
            return S_OK;
        Log("%S blade: sign-in changed", m_cfg->name);
        m_who = who;
        if (m_closePending)
        {
            XuiKillTimer(m_hObj, kCloseTimer);
            m_closePending = false;
        }
        ClosePicker(false);
        ScanProfiles();
        bool hadFocus = m_content && XuiElementTreeHasFocus(m_content);
        if (m_content)
        {
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

    // Focus landing on the slot (or the blade): move it into the content.
    HRESULT OnNotifySetFocus(HXUIOBJ source, XUINotifyFocus*, BOOL& bHandled)
    {
        if (m_open)
        {
            // Back from a full-screen scene: fold the blade back in.
            m_open = false;
            HXUIOBJ root = RootScene();
            if (root && kBladeAnimations)
                PlayRange(root, L"3Close", L"3CloseEnd");
        }
        if (source == m_container || source == m_hObj)
            FocusFirstItem(bHandled);
        else if (m_picker)
        {
            // Remember where in the picker focus is (to put it back there).
            for (HXUIOBJ p = source; p; XuiElementGetParent(p, &p))
                if (p == m_picker)
                {
                    m_pickerFocus = source;
                    break;
                }
        }
        return S_OK;
    }

    // dashmain.xur's tab scene ("RootScene", class DashMainScene): blade -> tab -> root.
    HXUIOBJ RootScene()
    {
        HXUIOBJ tab = NULL, root = NULL;
        XuiElementGetParent(m_hObj, &tab);
        if (tab)
            XuiElementGetParent(tab, &root);
        return root;
    }

    // The blade itself got focus (its tab became current): pass it on to the
    // first item of the content's own button list (DashScene P0), as the
    // dashboard's code did. dashmain.xur's built-in blades have DefaultFocus;
    // this one is added at runtime and has none.
    HRESULT OnSetFocus(HXUIOBJ, BOOL& bHandled)
    {
        FocusFirstItem(bHandled);
        return S_OK;
    }

    void FocusFirstItem(BOOL& bHandled)
    {
        if (m_picker)
        {
            // The picker is up: focus stays in it (0.0.42 lost it to the slot).
            if (m_pickerFocus && SUCCEEDED(XuiElementSetFocus(m_pickerFocus)))
                bHandled = TRUE;
            return;
        }
        if (!m_content)
            return;
        std::vector<std::wstring> ids = SplitList(StringProperty(m_content, L"P0"));
        // P0 comes back cut short on these stand-ins (one id, unusable), so the
        // two contents' own button ids follow it, in their order.
        static const wchar_t* const kKnown[] = { L"ctlGamerCard", L"fakeGamerCard", L"navArcade",
                                                 L"navCreateProfile", L"btnSignIn", L"btnJoin" };
        for (size_t k = 0; k < sizeof(kKnown) / sizeof(kKnown[0]); k++)
            ids.push_back(kKnown[k]);
        for (size_t i = 0; i < ids.size(); i++)
        {
            HXUIOBJ target = Child(m_content, ids[i].c_str());
            if (!target)
                continue;
            HRESULT hr = XuiElementSetFocus(target);
            Log("%S blade: focus -> %S: %08X", m_cfg->name, ids[i].c_str(), (unsigned)hr);
            if (SUCCEEDED(hr))
            {
                bHandled = TRUE;
                return;
            }
        }
        Log("%S blade: no focusable item in its list (%u ids)", m_cfg->name, (unsigned)ids.size());
    }

    // A button with a PressPath opens a full-screen (1120 x 770) scene, e.g.
    // Games Library -> arcade#2500_LiveArcadeHome.xur. XUI's own navigation
    // would put it inside this blade's 700 x 369 slot; the dashboard opened it
    // over everything and expanded the blade ("3Open" on the tab scene).
    HRESULT OnNotifyPress(HXUIOBJ source, BOOL& bHandled)
    {
        std::wstring path = StringProperty(source, L"PressPath");
        Log("%S blade: pressed %S (press path \"%S\")", m_cfg->name, ElementId(source).c_str(), path.c_str());
        std::wstring id = ElementId(source);
        if (PickerPress(id))
        {
            bHandled = TRUE;
            return S_OK;
        }
        if (id == L"fakeGamerCard")
        {
            OpenPicker();
            bHandled = TRUE;
            return S_OK;
        }
        if (id == L"navCreateProfile")
        {
            // Making a profile is the console's own Guide screen.
            DWORD r = XShowSigninUI(1, XSSUI_FLAGS_LOCALSIGNINONLY);
            Log("%S blade: sign-in screen: %u", m_cfg->name, (unsigned)r);
            bHandled = TRUE;
            return S_OK;
        }
        HXUIOBJ root = RootScene();
        if (path.empty() || !root)
            return S_OK;            // nothing to open: let the button do its own thing
        std::wstring base;
        PackageLocator(L"gamesbla", base);  // the resolver finds the file in whichever package has it
        HXUIOBJ scene = NULL;
        HRESULT hr = XuiSceneCreate(base.c_str(), path.c_str(), NULL, &scene);
        if (SUCCEEDED(hr))
            hr = XuiSceneNavigateForward(root, TRUE, scene, XUSER_INDEX_FOCUS);
        if (SUCCEEDED(hr))
        {
            // Same placement as the tab scene (main.cpp scales it to the screen).
            D3DXVECTOR3 scale, pos, pivot;
            XuiElementGetScale(root, &scale);
            XuiElementGetPosition(root, &pos);
            XuiElementGetPivot(root, &pivot);
            XuiElementSetPivot(scene, &pivot);
            XuiElementSetScale(scene, &scale);
            XuiElementSetPosition(scene, &pos);
            if (kBladeAnimations)
                PlayRange(root, L"3Open", L"3OpenEnd");
            m_open = true;
        }
        Log("%S blade: opened %S: %08X", m_cfg->name, path.c_str(), (unsigned)hr);
        bHandled = SUCCEEDED(hr);
        return S_OK;
    }

protected:
    const BladeContent* m_cfg;

private:
    HXUIOBJ m_container, m_content, m_picker, m_pickerFocus;
    bool m_open;                   // a full-screen scene is open over the blades
    XUID m_who;                     // who the content was built for (0: nobody)
    static const DWORD kSigninTimer = 0x5349474E;   // "SIGN"
    static const DWORD kCloseTimer = 0x50434C53;    // "PCLS"
    bool m_closePending;
    DWORD m_pickerOpenedAt;
};

// The Media and Xbox LIVE blades: the same, with their own content.
class CMediaBladeScene : public CGamesBladeScene
{
public:
    XUI_IMPLEMENT_CLASS(CMediaBladeScene, L"DashMediaScene", XUI_CLASS_SCENE)
    CMediaBladeScene() { m_cfg = &kMediaContent; }
};

class CLiveBladeScene : public CGamesBladeScene
{
public:
    XUI_IMPLEMENT_CLASS(CLiveBladeScene, L"DashLiveScene", XUI_CLASS_SCENE)
    CLiveBladeScene() { m_cfg = &kLiveContent; }
};

class CMarketplaceBladeScene : public CGamesBladeScene
{
public:
    XUI_IMPLEMENT_CLASS(CMarketplaceBladeScene, L"DashMPScene", XUI_CLASS_SCENE)
    CMarketplaceBladeScene() { m_cfg = &kMarketplaceContent; }
};

#include "theme/system_settings.inc"

}  // namespace

bool OpenGameFromBladesSetting(HXUIOBJ from)
{
    return OpenGameFromSetting(from);
}

// Blades' options (System > Themes > Y): the Homebrew blade takes the
// Marketplace blade's place, left of Xbox LIVE. For now only its name changes:
// the tab's label (blade_0_txt) and its header (Tab1 > scMarketplace > txt_Header).
bool BladesHomebrewOn()
{
    return GetSetting("blades.homebrew") == "on";
}

void ApplyBladesOptions(HXUIOBJ any)
{
    // The main scene, from anything in the dashboard (its canvas holds it).
    HXUIOBJ top = any, up = NULL, canvas = NULL;
    while (top && SUCCEEDED(XuiElementGetParent(top, &up)) && up)
    {
        canvas = up;
        top = up;
    }
    HXUIOBJ root = NULL;
    if (canvas)
        XuiElementGetChildById(canvas, L"RootScene", &root);
    if (!root && any)
        XuiElementGetChildById(any, L"RootScene", &root);
    if (!root)
        root = any;
    bool on = BladesHomebrewOn();
    HXUIOBJ label = Descendant(root, L"blade_0_txt");
    if (label)
        XuiTextElementSetText(label, on ? L"homebrew" : L"marketplace");
    HXUIOBJ tab = Child(root, L"Tab1");
    HXUIOBJ header = tab ? Descendant(tab, L"txt_Header") : NULL;
    if (header)
        XuiControlSetText(header, on ? L"Homebrew" : L"Marketplace");
    Log("blades: Marketplace blade %s (label %s, header %s)", on ? "is Homebrew" : "as it is", label ? "found" : "MISSING",
        header ? "found" : "MISSING");
}

bool SetBladesHomebrew(bool on, HXUIOBJ any)
{
    bool ok = SetSetting("blades.homebrew", on ? "on" : "");
    ApplyBladesOptions(any);
    return ok;
}

// The "Sign In" cards are XUI nav buttons with nowhere to go: they take A
// themselves and send no press, so the blade never hears it (0.0.70-0.0.73).
// The main loop sees A first and calls this: if the focus is on such a card,
// the blade around it opens its profile list.
bool PressSignInCard(HXUIOBJ focus)
{
    std::wstring id = focus ? ElementId(focus) : L"";
    if (id != L"fakeGamerCard" && id != L"btnSignIn")
        return false;
    static const wchar_t* const kBlades[] = { L"CGamesBladeScene", L"DashMediaScene", L"DashLiveScene", L"DashMPScene" };
    for (HXUIOBJ o = focus; o; XuiElementGetParent(o, &o))
    {
        XUIClass info;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        if (FAILED(XuiGetClass(XuiGetObjectClass(o), &info)) || !info.szClassName)
            continue;
        for (int i = 0; i < 4; i++)
            if (!wcscmp(info.szClassName, kBlades[i]))
            {
                XUIMessage msg;
                XuiMessage(&msg, kOpenProfilesMessage);
                XuiSendMessage(o, &msg);
                Log("sign-in card: profile list asked of %S", kBlades[i]);
                return true;
            }
    }
    return false;
}

void RequestDiscCheck()
{
    if (g_discEvent)
        SetEvent(g_discEvent);
}

int SignedInUser()
{
    for (int i = 0; i < XUSER_MAX_COUNT; i++)
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn)
            return i;
    return -1;
}

void RegisterSharedBladesClasses()
{
    Log("shared Arcade::CTitleListCtrl: %s", RegisterStandInWithProc(kTitleList, TitleListProc) ? "registered" : "FAILED");
    Log("shared Arcade::CTitleSelectorScene: %s",
        RegisterStandInWithProc(kTitleSelector, TitleSelectorProc) ? "registered" : "FAILED");
    Log("shared dashLanguageSettings: %s", RegisterStandInWithProc(kLanguageScene, LanguageSceneProc) ? "registered" : "FAILED");
    Log("shared dashCLanguage: %s", RegisterStandInWithProc(kLanguageList, LanguageListProc) ? "registered" : "FAILED");
}

HRESULT OpenThemesScreen(HXUIOBJ from)
{
    return OpenListScreen(from, new ThemesScreen());
}

// The File Manager over whole drives: the drives first (the Marketplace's File
// Manager link), or one drive's root (System > Memory, A on a device). Its
// folders copy, move, rename and delete (game_menu.inc FilesScreen).
class DrivesScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"File Manager"; }
    const wchar_t* CurrentHeading() { return L"Drives"; }
    std::wstring Current()
    {
        wchar_t b[32];
        _snwprintf_s(b, 32, _TRUNCATE, m_roots.size() == 1 ? L"%u drive" : L"%u drives", (unsigned)m_roots.size());
        return b;
    }
    void Rebuild()
    {
        static const struct { const char* root; const wchar_t* name; } kDrives[] = {
            { "hdd:", L"Hard Drive" }, { "usb:", L"USB Storage Device" }, { "dvd:", L"Disc" } };
        m_roots.clear();
        items.clear();
        m_about.clear();
        for (size_t i = 0; i < ARRAYSIZE(kDrives); i++)
        {
            ULARGE_INTEGER freeBytes, total, totalFree;
            if (!GetDiskFreeSpaceExA((std::string(kDrives[i].root) + "\\").c_str(), &freeBytes, &total, &totalFree))
                continue;
            m_roots.push_back(kDrives[i].root);
            items.push_back(kDrives[i].name);
            wchar_t b[96];
            _snwprintf_s(b, 96, _TRUNCATE, L"%s\r\n%.1f GB free of %.1f GB", kDrives[i].name,
                         freeBytes.QuadPart / 1073741824.0, total.QuadPart / 1073741824.0);
            m_about.push_back(b);
        }
        if (items.empty())
            items.push_back(L"No drives found");
    }
    std::wstring About(int i) { return i >= 0 && i < (int)m_about.size() ? m_about[i] : L""; }
    void Press(HXUIOBJ scene, int i)
    {
        if (i >= 0 && i < (int)m_roots.size())
            OpenListScreen(scene, new FilesScreen(m_roots[i]));
    }

private:
    std::vector<std::string> m_roots;
    std::vector<std::wstring> m_about;
};

HRESULT OpenFileManager(HXUIOBJ from, const char* root)
{
    if (root)
        return OpenListScreen(from, new FilesScreen(root));
    return OpenListScreen(from, new DrivesScreen());
}

HRESULT OpenStoreShelf(HXUIOBJ from, int shelf)
{
    if (shelf < 0 || shelf >= store::kShelfCount)
        return E_INVALIDARG;
    return OpenListScreen(from, new storeui::ShelfScreen((store::Shelf)shelf));
}

HRESULT OpenStoreDownloads(HXUIOBJ from)
{
    return storeui::OpenDownloads(from);
}

HRESULT OpenStoreRepos(HXUIOBJ from)
{
    return storeui::OpenRepos(from);
}

HRESULT OpenRetroAchievements(HXUIOBJ from)
{
    return OpenListScreen(from, new RetroAchievementsScreen());
}

HRESULT OpenJoinService(HXUIOBJ from)
{
    return OpenListScreen(from, new ServicesScreen());
}


void SetScreenOpener(ScreenOpener opener)
{
    g_screenOpener = opener;
}

KeyboardShower g_keyboardShower = NULL;

void SetKeyboardShower(KeyboardShower shower)
{
    g_keyboardShower = shower;
}

DWORD ShowKeyboard(DWORD user, DWORD flags, LPCWSTR defaultText, LPCWSTR title, LPCWSTR description, LPWSTR result,
                   DWORD maxChars, PXOVERLAPPED overlapped)
{
    if (g_keyboardShower)
        return g_keyboardShower(user, flags, defaultText, title, description, result, maxChars, overlapped);
    return XShowKeyboardUI(user, flags, defaultText, title, description, result, maxChars, overlapped);
}

HRESULT RegisterBladesClasses()
{
    HRESULT hr = CGamesBladeScene::Register();
    Log("CGamesBladeScene: %08X", (unsigned)hr);
    Log("DashMediaScene: %08X", (unsigned)CMediaBladeScene::Register());
    Log("DashLiveScene: %08X", (unsigned)CLiveBladeScene::Register());
    Log("DashMPScene: %08X", (unsigned)CMarketplaceBladeScene::Register());
    Log("XuiBOTDOfflineContainer: %s", RegisterStandInWithProc(kBotdContainer, BotdContainerProc) ? "registered" : "FAILED");
    Log("TraySceneLoader: %s", RegisterStandInWithProc(kTrayLoader, TrayLoaderProc) ? "registered" : "FAILED");
    Log("XuiGamerCard: %s", RegisterStandInWithProc(kGamerCard, GamerCardProc) ? "registered" : "FAILED");
    Log("Arcade::CTitleListCtrl: %s", RegisterStandInWithProc(kTitleList, TitleListProc) ? "registered" : "FAILED");
    Log("CConnStatus: %s", RegisterStandInWithProc(kConnStatus, ConnStatusProc) ? "registered" : "FAILED");
    RegisterStandIns(&kDashScene, 1);       // the selector's base, ahead of the generated table
    RegisterSystemClasses();
    Log("Arcade::CTitleSelectorScene: %s",
        RegisterStandInWithProc(kTitleSelector, TitleSelectorProc) ? "registered" : "FAILED");
    return hr;
}

void UnregisterBladesClasses()
{
    CMarketplaceBladeScene::Unregister();
    CLiveBladeScene::Unregister();
    CMediaBladeScene::Unregister();
    CGamesBladeScene::Unregister();
}
