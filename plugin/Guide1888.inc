// The 1888 Guide's behaviour, inside the console's own Guide (included by
// SlopGuide.cpp). Everything here is what 2.0.1888's hud.xex and xam.xex did,
// read from their code (the 1888 files in SlopDash\Assets); the console's code
// is only where it plugs in: its XUI, reached by export number (1888 xam's
// export table names them: they're the same numbers on 17559), and a message
// filter (XuiSetMessageFilter) that sees every Guide message before the
// element does - so the 1888 screens in blades1888.xzp behave as they did
// without registering any classes.
//
//   Personal Settings > Themes (SkinSelect.xur, list SkinsListId; hud SkinsList,
//     913EC1A8 / 913EC250 / 913EBE00): "Customize", "Xbox 360 (Default)",
//     "Carbon", "Glass". A theme is written to the profile (SkinRoot:\DashStyle,
//     4 bytes: 1 Carbon, 2 Glass; none for the default), the Guide background
//     follows it (Carbon: Carbon.png, Glass: Pearl.png, default: none) and the
//     dashboard is told (XNotifyBroadcast 0x8000000F).
//   Customize (OptionsPersonalization.xur, CustomizeList; hud 913EC6B0):
//     "Dashboard Trim" -> DashStyleSelect.xur, "Guide Background" -> TileSelect.xur.
//   Dashboard Trim (DashStyleSelect.xur, StyleListId; hud StyleList 913EDBE8):
//     "Default Trim", "Carbon Trim", "Glass Trim": the DashStyle alone (no
//     background). 1888's dash.xex loads dashskn<DashStyle>#BladeSkin.xur over
//     its skin (920A43C4); SlopDash does from SlopDashPics:\Trims\<XUID>.txt.
//   Guide Background (TileSelect.xur, TileListId, PreviewId; hud TileList,
//     913EC750 / 913EC830 / 913ECF18, tiles at 913E1488): "Custom Color",
//     "Default", then Pearl, Smoke, Dew, Tangerine, Bubblegum, Stone, Wood.
//   Custom Color (ColorSelect.xur; hud ColorSelect 913ECC00 / 913EBA70):
//     colour = FF | red << 16 | green << 8 | blue from sliderRed / sliderGreen /
//     sliderBlue, previewed on PreviewId, applied with A (btnA).
//   The background (1888 xam 818D1830 / 818D12D0): stored in the signed-in
//     profile as the dashboard title's tile 0x8003 (XamWriteTile type 6): a PNG,
//     or 4 bytes of colour, or nothing for the default; shown on the
//     "background" figure of the visuals of graphic-base, graphic-blade,
//     graphic-base-error and graphic-blade-error.

#include <xui.h>
#include <stdlib.h>
#include <string>

void RunTask(PVOID fn);     // SlopGuide.cpp: runs fn as a xam task, on the Guide's side

namespace g1888 {

// --- the console's XUI and xam, by export number -----------------------------------
struct Export { DWORD ordinal; PVOID* slot; };
PVOID p_SetMessageFilter, p_SendMessage, p_GetChildById, p_GetId, p_GetParent, p_ControlGetVisual,
      p_FigureSetTexture, p_FigureSetFill, p_NavigateForward, p_SliderGetValue, p_SliderSetValue,
      p_ResourceLoadAll, p_Free, p_ReadTile, p_WriteTile, p_ProfileOpen, p_ProfileClose, p_NotifyBroadcast, p_ShowSignin, p_GetOuter, p_GetPropId, p_SetProp, p_FindFrame, p_PlayTimeline, p_SetImagePath, p_FirstChild, p_CreateProfile, p_ProcessInput,
      p_XenonButton, p_NotifyCreateListener, p_NotifyGetNext, p_SetTimer, p_LaunchTitleEx, p_SetLaunchData,
      p_Unlink, p_AddChild, p_SetBounds, p_GetUserFocus, p_SetUserFocus, p_KillTimer, p_CurrentTitleId,
      p_ShowGuide, p_DashLaunch, p_ReadSettings, p_NotifyQueue, p_DeviceStats,
      p_WriteSettings, p_NotifySetOptions, p_NotifyGetOptions, p_SetPresence, p_GetPresence, p_SliderGetRange,
      p_UserLogon;
const Export kExports[] = {
    { 934, &p_SetMessageFilter }, { 863, &p_SendMessage },     { 810, &p_GetChildById },
    { 814, &p_GetId },            { 817, &p_GetParent },       { 917, &p_ControlGetVisual },
    { 912, &p_FigureSetTexture }, { 910, &p_FigureSetFill },   { 858, &p_NavigateForward },
    { 898, &p_SliderGetValue },   { 897, &p_SliderSetValue },  { 914, &p_ResourceLoadAll },
    { 874, &p_Free },             { 751, &p_ReadTile },        { 753, &p_WriteTile },
    { 567, &p_ProfileOpen },      { 568, &p_ProfileClose },    { 654, &p_NotifyBroadcast },   { 700, &p_ShowSignin },     { 840, &p_GetPropId },        { 892, &p_SetProp },
    { 809, &p_FindFrame },        { 821, &p_PlayTimeline },    { 915, &p_SetImagePath },
    { 811, &p_FirstChild },       { 719, &p_CreateProfile },     { 841, &p_ProcessInput },     { 831, &p_GetOuter },
    // XamInputSendXenonButtonPress (the XDK's xav.lib names it): the Xbox button,
    // as xam's input code takes it (17559 816F2E60, one argument: the controller).
    { 1286, &p_XenonButton },
    // XamUserReadProfileSettings (gamerprofile 17559 imports it as 537 and
    // calls it as (title, user, 0, NULL, count, ids, &size, results, NULL):
    // 9010C308): the profile's gamer zone, for the gamer card.
    { 537, &p_ReadSettings },
    // XamInputGetDeviceStats (user, stats): the controller's battery, for the
    // main menu's battery icon (BatteryIcon).
    { 407, &p_DeviceStats },
    // Personal Settings (PersonalLoad / PersonalSave), as 1888's hud calls
    // them: XamUserWriteProfileSettings (538: title, user, count, settings,
    // NULL; 913EAA84); XNotifyUIGetOptions / SetOptions (659 / 658: show,
    // sound and two more, kept; 17559 hud 913F1D08 / 913F1F18);
    // XamGetUserPresetPresenceState / Set (533 / 532: user [, state]);
    // XuiSliderGetRange (911).
    { 538, &p_WriteSettings },    { 658, &p_NotifySetOptions }, { 659, &p_NotifyGetOptions },
    { 532, &p_SetPresence },      { 533, &p_GetPresence },      { 911, &p_SliderGetRange },
    // XamUserLogon (XUID per slot, flags 0x23 sign out; SlopDash's profiles.cpp).
    { 0x20B, &p_UserLogon },
    // XNotifyQueueUI (xam 0x290: type, user, priority, text, context; type 7
    // "signed in", as SlopDash's profiles.cpp Toast): Create Profile's held
    // sign-in toast, shown at its Done.
    { 0x290, &p_NotifyQueue },
    // XamNotifyCreateListener, XNotifyGetNext: the system's notifications, for
    // XN_SYS_UI (the Guide opening and closing).
    { 650, &p_NotifyCreateListener }, { 651, &p_NotifyGetNext },
    { 868, &p_SetTimer },         // XuiSetTimer (obj, id, ms)
    // XamLoaderLaunchTitleEx, XamLoaderSetLaunchData: the Guide's "Xbox
    // Dashboard" (1888 hud 913E3530 imports them as xam 421 and 422).
    { 421, &p_LaunchTitleEx },    { 422, &p_SetLaunchData },
    // Message boxes moved onto 1888's error blade: XuiElementUnlink,
    // XuiElementAddChild, XuiElementSetBounds, XuiElementGetUserFocus,
    // XuiElementSetUserFocus, XuiKillTimer; XamGetCurrentTitleId.
    { 900, &p_Unlink },           { 808, &p_AddChild },        { 822, &p_SetBounds },
    { 818, &p_GetUserFocus },     { 824, &p_SetUserFocus },    { 881, &p_KillTimer },
    { 463, &p_CurrentTitleId },
    // XamShowGuideUI (772: the Guide, as its button opens it), and the Guide's
    // Xbox Dashboard launch (2105: 17559 hud 913E6228 hands it the 1020-byte
    // launch data, xam 816A1330).
    { 772, &p_ShowGuide },        { 2105, &p_DashLaunch },
};

extern "C" LONG XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID* address);

bool Resolve()
{
    HANDLE xam = NULL;
    if (!NT_OK(XexGetModuleHandle("xam.xex", &xam)) || !xam)
        return false;
    for (size_t i = 0; i < ARRAYSIZE(kExports); i++)
    {
        PVOID p = NULL;
        if (!NT_OK(XexGetProcedureAddress(xam, kExports[i].ordinal, &p)) || !p)
        {
            Log("1888 Guide: xam export %u missing", (unsigned)kExports[i].ordinal);
            return false;
        }
        *kExports[i].slot = p;
    }
    return true;
}

typedef PFN_MESSAGE_FILTER (APIENTRY *SetMessageFilterFn)(PFN_MESSAGE_FILTER);
typedef HRESULT (APIENTRY *GetIdFn)(HXUIOBJ, LPCWSTR*);
typedef HRESULT (APIENTRY *GetParentFn)(HXUIOBJ, HXUIOBJ*);
typedef HRESULT (APIENTRY *GetChildFn)(HXUIOBJ, LPCWSTR, HXUIOBJ*);
typedef HRESULT (APIENTRY *GetVisualFn)(HXUIOBJ, HXUIOBJ*);
typedef HRESULT (APIENTRY *SetTextureFn)(HXUIOBJ, LPCWSTR);
typedef HRESULT (APIENTRY *SetFillFn)(HXUIOBJ, XUI_FILL_TYPE, DWORD, XUIGradientStop*, int, float, const D3DXVECTOR2*,
                                      const D3DXVECTOR2*);
typedef HRESULT (APIENTRY *NavigateFn)(HXUIOBJ, BOOL, HXUIOBJ, BYTE);
typedef HRESULT (APIENTRY *SliderGetFn)(HXUIOBJ, int*);
typedef HRESULT (APIENTRY *SliderSetFn)(HXUIOBJ, int);
typedef HRESULT (APIENTRY *LoadAllFn)(LPCWSTR, BYTE**, UINT*);
typedef void (APIENTRY *FreeFn)(void*);
// 1888 xam 818C0598 / 818C0778, as 818D1354 and 818D18EC call them.
typedef DWORD (*ReadTileFn)(DWORD type, DWORD title, ULONGLONG tile, DWORD user, BYTE* buffer, DWORD* size, PVOID ov);
typedef DWORD (*WriteTileFn)(DWORD type, DWORD title, ULONGLONG tile, DWORD user, const BYTE* data, DWORD size, PVOID ov);
// hud 913EBE50 / 913EBEE8: XamProfileOpen(xuid, drive, 0, 0), XamProfileClose(drive).
typedef DWORD (*ProfileOpenFn)(XUID, const char*, DWORD, DWORD);
typedef DWORD (*ProfileCloseFn)(const char*);
typedef void (*NotifyBroadcastFn)(DWORD, DWORD);

const DWORD kDashTitle = 0xFFFE07D1;
const ULONGLONG kGuideTile = 0x8003;
const DWORD kDefaultColour = 0xFF708499;        // hud 913ED280: ColorSelect with nothing stored

std::wstring Id(HXUIOBJ obj)
{
    LPCWSTR id = NULL;
    if (obj && SUCCEEDED(((GetIdFn)p_GetId)(obj, &id)) && id)
        return id;
    return L"";
}

HXUIOBJ Parent(HXUIOBJ obj)
{
    HXUIOBJ up = NULL;
    ((GetParentFn)p_GetParent)(obj, &up);
    return up;
}

HXUIOBJ Child(HXUIOBJ parent, LPCWSTR id)
{
    HXUIOBJ c = NULL;
    if (parent)
        ((GetChildFn)p_GetChildById)(parent, id, &c);
    return c;
}

int User()
{
    for (DWORD i = 0; i < 4; i++)
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn)
            return (int)i;
    return -1;
}

}  // namespace g1888

// The XDK's XUI helpers (XuiListGetCurSel, XuiElementSetShow...) send messages:
// here, through the console's XUI.
HRESULT APIENTRY XuiSendMessage(HXUIOBJ hObj, XUIMessage* pMessage)
{
    typedef HRESULT (APIENTRY *SendFn)(HXUIOBJ, XUIMessage*);
    return ((SendFn)g1888::p_SendMessage)(hObj, pMessage);
}

// XuiControlSetText sends to the control's outer (XuiGetOuter, export 831).
HXUIOBJ APIENTRY XuiGetOuter(HXUIOBJ hObj)
{
    typedef HXUIOBJ (APIENTRY *GetOuterFn)(HXUIOBJ);
    return ((GetOuterFn)g1888::p_GetOuter)(hObj);
}

namespace g1888 {

// --- the background --------------------------------------------------------------
const WCHAR kGlass[] = L"file://SlopGuide:/blades1888.xzp#glass.png";
BYTE* g_tilePng = NULL;     // the stored PNG, kept while it's shown (a memory:// locator)
DWORD g_tilePngSize = 0;

// The frame's root scene (HUDRootScene), as xam made it again (Rebuild1888).
HXUIOBJ FrameRoot()
{
    DWORD frame = *(volatile DWORD*)kFrameInstance;
    frame = Pointer(frame) ? *(volatile DWORD*)(frame + 12) : 0;
    return Pointer(frame) ? Child(*(HXUIOBJ*)(frame + 8), L"HUDRootScene") : NULL;
}

// Where the Guide sits across the screen. 1888's own place (xam 818D0248: 40
// pixels left of the edge at 720 lines, -26.67 in the 852-wide canvas,
// tools/guide1888.py FIT_AT) moved further left: by
// SlopGuide:\guide_x.txt (canvas units, read each time the Guide opens, so it
// can be tuned without a rebuild), else 75 (about a fifth of the panel).
const float kFitX = -40.0f * 480.0f / 720.0f;
struct RawVector { DWORD type; float x, y, z; DWORD pad[2]; };      // an XUIElementPropVal holding a vector
typedef HRESULT (APIENTRY *GetPropIdFn)(HXUIOBJ, LPCWSTR, DWORD*);
typedef HRESULT (APIENTRY *SetPropFn)(HXUIOBJ, DWORD, DWORD, const void*);

float GuideShift()
{
    float shift = 75.0f;
    HANDLE h = CreateFileA("SlopGuide:\\guide_x.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        char text[16] = { 0 };
        DWORD got = 0;
        if (ReadFile(h, text, sizeof(text) - 1, &got, NULL) && got)
            shift = (float)atof(text);
        CloseHandle(h);
    }
    return shift;
}

HXUIOBJ g_frameRoot = NULL;         // for the frame's message log (Filter)
volatile LONG g_frameMsgs = 0;

void PlaceFrame()
{
    HXUIOBJ root = FrameRoot();
    g_frameRoot = root;
    DWORD id = 0;
    if (!root || FAILED(((GetPropIdFn)p_GetPropId)(root, L"Position", &id)))
        return;
    float shift = GuideShift();
    RawVector v = { XUI_EPT_VECTOR, kFitX - shift, 0.0f, 0.0f, { 0, 0 } };
    HRESULT hr = ((SetPropFn)p_SetProp)(root, id, 0, &v);
    Log("1888 Guide: placed at x %.1f (moved left %.1f): %08X", v.x, shift, (unsigned)hr);
}

// The panels' "background" figures in the Guide's frame (1888 xam 818D0AE0, 818D10C8).
int Figures(HXUIOBJ* out)
{
    int n = 0;
    HXUIOBJ root = FrameRoot();
    if (!root)
        return 0;
    HXUIOBJ error = Child(root, L"ErrorHUD");
    HXUIOBJ controls[4] = { Child(root, L"graphic-base"), Child(root, L"graphic-blade"),
                            Child(error, L"graphic-base-error"), Child(error, L"graphic-blade-error") };
    for (int i = 0; i < 4; i++)
    {
        HXUIOBJ visual = NULL;
        if (controls[i] && SUCCEEDED(((GetVisualFn)p_ControlGetVisual)(controls[i], &visual)) && visual)
        {
            HXUIOBJ fig = Child(visual, L"background");
            if (fig)
                out[n++] = fig;
        }
    }
    return n;
}

void FillColour(HXUIOBJ fig, DWORD colour)
{
    D3DXVECTOR2 scale(1, 1), trans(0, 0);
    ((SetFillFn)p_FigureSetFill)(fig, XUI_FILL_SOLID, colour, NULL, 0, 0.0f, &scale, &trans);
}

// A texture, and the figure's fill made a texture fill (hud 913ECFC0 then
// 913ECA40(figure, 4): Fill.FillType = 4), so it shows over a solid fill.
void FillTexture(HXUIOBJ fig, LPCWSTR locator)
{
    D3DXVECTOR2 scale(1, 1), trans(0, 0);
    ((SetFillFn)p_FigureSetFill)(fig, XUI_FILL_TEXTURE, 0xFFFFFFFF, NULL, 0, 0.0f, &scale, &trans);
    ((SetTextureFn)p_FigureSetTexture)(fig, locator);
}

// The Guide's current background (1888 xam keeps it in the frame object, +6C):
// shown again whenever the Guide opens, signed in or not.
enum { kBgDefault, kBgColour, kBgFile, kBgTile };
int g_bgKind = kBgDefault;
DWORD g_bgColour = 0;
WCHAR g_bgFile[32] = { 0 };
bool g_panelsChanged = false;           // the panels have been given a background
bool g_panelsColoured = false;          // ... and it's a solid colour now

void ShowBackground()
{
    HXUIOBJ figs[4];
    int n = Figures(figs);
    WCHAR locator[96];
    if (g_bgKind == kBgTile)
        _snwprintf_s(locator, ARRAYSIZE(locator), _TRUNCATE, L"memory://%x,%x", (unsigned)(DWORD)g_tilePng,
                     (unsigned)g_tilePngSize);
    else if (g_bgKind == kBgFile)
        _snwprintf_s(locator, ARRAYSIZE(locator), _TRUNCATE, L"file://SlopGuide:/blades1888.xzp#%s", g_bgFile);
    // Nothing chosen yet: the skin's own panels, untouched.
    if (!g_panelsChanged && g_bgKind == kBgDefault)
        return;
    // The panels' own fill is a texture fill tinted with the skin's colour
    // (huduiskin HUD_background / HUD_blade "background": 0xff9bb482 for the
    // base, 0xff708499 for the blade); Figures gives base, blade, base, blade.
    static const DWORD kTint[4] = { 0xff9bb482, 0xff708499, 0xff9bb482, 0xff708499 };
    for (int i = 0; i < n; i++)
    {
        LPCWSTR texture = g_bgKind == kBgDefault ? kGlass : locator;
        if (g_bgKind == kBgColour)
            FillColour(figs[i], g_bgColour);
        else if (g_panelsColoured)
        {
            // Back from a colour: the texture fill again, with its own tint.
            D3DXVECTOR2 scale(1, 1), trans(0, 0);
            ((SetFillFn)p_FigureSetFill)(figs[i], XUI_FILL_TEXTURE, kTint[i], NULL, 0, 0.0f, &scale, &trans);
            ((SetTextureFn)p_FigureSetTexture)(figs[i], texture);
        }
        else
            ((SetTextureFn)p_FigureSetTexture)(figs[i], texture);     // as 0.21: the picture only
    }
    g_panelsColoured = g_bgKind == kBgColour;
    g_panelsChanged = true;
    Log("1888 Guide background shown on %d panels: kind %d", n, g_bgKind);
}

// As 818D12D0 (the Guide opening): the signed-in profile's tile when it has
// one (a PNG or 4 bytes of colour), else the current one again.
void ApplyBackground()
{
    int user = User();
    DWORD size = 0;
    if (user >= 0 && ((ReadTileFn)p_ReadTile)(6, kDashTitle, kGuideTile, (DWORD)user, NULL, &size, NULL) == 0 && size)
    {
        if (size == 4)
        {
            DWORD colour = 0;
            if (((ReadTileFn)p_ReadTile)(6, kDashTitle, kGuideTile, (DWORD)user, (BYTE*)&colour, &size, NULL) == 0)
            {
                g_bgKind = kBgColour;
                g_bgColour = colour;
            }
        }
        else
        {
            BYTE* png = (BYTE*)malloc(size);
            if (png && ((ReadTileFn)p_ReadTile)(6, kDashTitle, kGuideTile, (DWORD)user, png, &size, NULL) == 0)
            {
                if (g_tilePng)
                    free(g_tilePng);
                g_tilePng = png;
                g_tilePngSize = size;
                g_bgKind = kBgTile;
            }
            else if (png)
                free(png);
        }
    }
    ShowBackground();
}

// As 818D1830: becomes the current background straight away, and is stored in
// the signed-in profile when there is one (a PNG from the package, a colour, or
// nothing for the default).
void StoreBackground(LPCWSTR png, const DWORD* colour)
{
    if (png)
    {
        g_bgKind = kBgFile;
        wcsncpy_s(g_bgFile, ARRAYSIZE(g_bgFile), png, _TRUNCATE);
    }
    else if (colour)
    {
        g_bgKind = kBgColour;
        g_bgColour = *colour;
    }
    else
        g_bgKind = kBgDefault;
    ShowBackground();
    int user = User();
    if (user < 0)
        return;
    DWORD r;
    if (png)
    {
        WCHAR locator[96];
        _snwprintf_s(locator, ARRAYSIZE(locator), _TRUNCATE, L"file://SlopGuide:/blades1888.xzp#%s", png);
        BYTE* data = NULL;
        UINT size = 0;
        if (FAILED(((LoadAllFn)p_ResourceLoadAll)(locator, &data, &size)) || !data)
            return;
        r = ((WriteTileFn)p_WriteTile)(6, kDashTitle, kGuideTile, (DWORD)user, data, size, NULL);
        ((FreeFn)p_Free)(data);
    }
    else if (colour)
        r = ((WriteTileFn)p_WriteTile)(6, kDashTitle, kGuideTile, (DWORD)user, (const BYTE*)colour, 4, NULL);
    else
        r = ((WriteTileFn)p_WriteTile)(6, kDashTitle, kGuideTile, (DWORD)user, NULL, 0, NULL);
    Log("1888 Guide background stored for user %d: %u", user, (unsigned)r);
}

// --- screens ---------------------------------------------------------------------
void Open(HXUIOBJ from, LPCWSTR file)
{
    HXUIOBJ scene = Parent(from);       // the list's screen
    PVOID created = NULL;
    if (FAILED(((SceneCreateExFn)kSceneCreateEx)(k1888Base, file, NULL, 0, 0, &created)) || !created)
        return;
    HRESULT hr = ((NavigateFn)p_NavigateForward)(scene, FALSE, (HXUIOBJ)created, XUSER_INDEX_FOCUS);
    Log("1888 Guide: %S: %08X", file, (unsigned)hr);
}

// The trim for SlopDash too (it can't open a signed-in profile itself):
// SlopDashPics:\Trims\<XUID>.txt, "0" default, "1" Carbon, "2" Glass.
void WriteTrimFile(XUID xuid, DWORD style)
{
    CreateDirectoryA("SlopDashPics:\\Trims", NULL);
    char path[64], text[4];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "SlopDashPics:\\Trims\\%016I64X.txt", xuid);
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%u", (unsigned)style);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD put = 0;
    WriteFile(h, text, (DWORD)strlen(text), &put, NULL);
    CloseHandle(h);
}

// Themes and Dashboard Trim (hud 913EBE00: flag 2 the profile's DashStyle and
// the dashboard told, flag 1 the matching background). Themes passes both;
// Dashboard Trim (StyleList, 913EC628) only the style.
void SetTheme(DWORD style, bool background = true)
{
    int user = User();
    XUID xuid = 0;
    if (user >= 0 && XUserGetXUID((DWORD)user, &xuid) == ERROR_SUCCESS &&
        ((ProfileOpenFn)p_ProfileOpen)(xuid, "SkinRoot", 0, 0) == 0)
    {
        WriteTrimFile(xuid, style);
        if (style)
        {
            HANDLE h = CreateFileA("SkinRoot:\\DashStyle", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            if (h != INVALID_HANDLE_VALUE)
            {
                DWORD put = 0;
                WriteFile(h, &style, 4, &put, NULL);
                CloseHandle(h);
            }
        }
        else
            DeleteFileA("SkinRoot:\\DashStyle");
        ((ProfileCloseFn)p_ProfileClose)("SkinRoot");
        ((NotifyBroadcastFn)p_NotifyBroadcast)(0x8000000F, 1u << user);
    }
    // The background changes either way (913EBF0C).
    if (background)
        StoreBackground(style == 1 ? L"Carbon.png" : style == 2 ? L"Pearl.png" : NULL, NULL);
    Log("1888 Guide: theme %u%s", (unsigned)style, background ? "" : " (trim only)");
}

// The Guide Background tiles (hud 913E1488): image and name (hud strings 35, 1, 17, 16, 0, 37, 46).
struct Tile { LPCWSTR png, name; };
const Tile kTiles[] = { { L"Pearl.png", L"Pearl" }, { L"Carbon.png", L"Smoke" }, { L"H2O.png", L"Dew" },
                        { L"Fiber.png", L"Tangerine" }, { L"Bubbles.png", L"Bubblegum" },
                        { L"Sahara.png", L"Stone" }, { L"Wood.png", L"Wood" } };
const int kTileCount = ARRAYSIZE(kTiles);
LPCWSTR const kThemes[] = { L"Customize", L"Xbox 360 (Default)", L"Carbon", L"Glass" };
// Dashboard Trim (StyleList, 913EC570: hud strings 11, 8, 10; item = DashStyle).
// 1888 then listed other profiles' trims to copy; not here.
LPCWSTR const kTrims[] = { L"Default Trim", L"Carbon Trim", L"Glass Trim" };

DWORD StoredColour()
{
    int user = User();
    DWORD size = 4, colour = 0;
    if (user >= 0 && ((ReadTileFn)p_ReadTile)(6, kDashTitle, kGuideTile, (DWORD)user, NULL, &size, NULL) == 0 &&
        size == 4 && ((ReadTileFn)p_ReadTile)(6, kDashTitle, kGuideTile, (DWORD)user, (BYTE*)&colour, &size, NULL) == 0)
        return colour;
    return kDefaultColour;
}

void Preview(HXUIOBJ list, int i)
{
    HXUIOBJ preview = Child(Parent(list), L"PreviewId");
    if (!preview)
        return;
    if (i == 0)
        FillColour(preview, StoredColour());
    else if (i == 1)
        FillTexture(preview, kGlass);
    else if (i - 2 < kTileCount)
    {
        WCHAR locator[96];
        _snwprintf_s(locator, ARRAYSIZE(locator), _TRUNCATE, L"file://SlopGuide:/blades1888.xzp#%s", kTiles[i - 2].png);
        FillTexture(preview, locator);
    }
}

DWORD ColourFromSliders(HXUIOBJ scene)
{
    int r = 0, g = 0, b = 0;
    ((SliderGetFn)p_SliderGetValue)(Child(scene, L"sliderRed"), &r);
    ((SliderGetFn)p_SliderGetValue)(Child(scene, L"sliderGreen"), &g);
    ((SliderGetFn)p_SliderGetValue)(Child(scene, L"sliderBlue"), &b);
    return 0xFF000000 | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

bool Answer(XUIMessage* msg, const std::wstring& id)
{
    int count = id == L"TileListId" ? 2 + kTileCount : id == L"SkinsListId" ? ARRAYSIZE(kThemes) :
                id == L"StyleListId" ? ARRAYSIZE(kTrims) : -1;
    if (count < 0)
        return false;
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = count;
            msg->bHandled = TRUE;
        }
        return true;
    }
    XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
    if (d->bItemData && d->iData == 0 && d->iItem >= 0 && d->iItem < count)
    {
        if (id == L"SkinsListId")
            d->szText = kThemes[d->iItem];
        else if (id == L"StyleListId")
            d->szText = kTrims[d->iItem];
        else
            d->szText = d->iItem == 0 ? L"Custom Color" : d->iItem == 1 ? L"Default" : kTiles[d->iItem - 2].name;
        msg->bHandled = TRUE;
    }
    return true;
}

// --- Sign In --------------------------------------------------------------------
// The main menu's Sign In card (btnSignIn) or X (btnX: "Sign in" / "Switch
// Profile") opens the console's own sign-in, as 1888's MainMenuSignedOut/In
// opened its (xam's XamShowSigninUI, export 700: panes, flags); its scenes are
// 1888's (SlopSceneCreate: SI_*.xur).
typedef DWORD (*ShowSigninFn)(DWORD panes, DWORD flags);

void OpenSignIn()
{
    DWORD r = ((ShowSigninFn)p_ShowSignin)(1, 0);
    Log("1888 Guide: sign in: %u", (unsigned)r);
    FlushLog();
}

// Create New Profile (the signed-out menu's btnNewProfile, Sign In's Y): the
// console's own (xam's XamShowCreateProfileUI, export 719: user, flags; 1888
// xam 818E6A08 took the user), its scenes 1888's (SlopSceneCreate: CP_*.xur).
typedef DWORD (*ShowCreateProfileFn)(DWORD user, DWORD flags);

// The sign-in toast while a profile is made: the console signs the new profile
// in before Save Gamer Profile (createprofile 90103990, XamUserLogon: Save
// Gamer Profile and Edit Gamer Profile work on the signed-in profile, so the
// sign-in itself stays), but its toast is held - every notification screen
// made meanwhile is hidden - and shown when the profile is done: Done on Save
// Gamer Profile or Edit Gamer Profile, the Guide's main menu, or the Guide
// closing.
int GuideUser();
volatile LONG g_cpActive = 0;           // Create Profile running (its first screen to its end)
volatile LONG g_toastHeld = 0;

void CreateProfileStarted()
{
    g_cpActive = 1;
    g_toastHeld = 0;
}

void HoldToast(HXUIOBJ scene)
{
    if (!g_cpActive || !scene)
        return;
    XuiElementSetShow(scene, FALSE);
    g_toastHeld = 1;
    Log("1888 Guide: toast held until the profile is done");
}

void CreateProfileOver(const char* why)
{
    if (!InterlockedExchange(&g_cpActive, 0))
        return;
    bool held = InterlockedExchange(&g_toastHeld, 0) != 0;
    Log("1888 Guide: create profile over (%s)%s", why, held ? ": the sign-in toast now" : "");
    int user = GuideUser();
    if (!held || user < 0 || !p_NotifyQueue)
        return;
    typedef void (*NotifyQueueFn)(DWORD type, DWORD user, ULONGLONG priority, WCHAR* text, PVOID context);
    static WCHAR name[XUSER_NAME_SIZE + 1];     // (xam may read it after the call)
    char n[XUSER_NAME_SIZE + 1] = { 0 };
    name[0] = 0;
    if (XUserGetName((DWORD)user, n, sizeof(n)) == ERROR_SUCCESS)
        for (int i = 0; n[i] && i < XUSER_NAME_SIZE; i++)
            name[i] = (WCHAR)(unsigned char)n[i], name[i + 1] = 0;
    ((NotifyQueueFn)p_NotifyQueue)(7, (DWORD)user, 2, name, NULL);
}

void OpenCreateProfile()
{
    DWORD r = ((ShowCreateProfileFn)p_CreateProfile)(0, 0);
    Log("1888 Guide: create profile: %u", (unsigned)r);
    FlushLog();
}

// The console's Sign In lists every profile; SlopDash's hides the BadAvatar
// exploit's (it starts the hack, it isn't for signing in). Here the list's
// rows are seen through a window without it: the count one less, and rows
// from it on asked of the next one. The list moves its own selection (through
// XM_SET_CURSEL / XM_GET_CURSEL too), so the selection is left alone, except
// the one read that follows A (the console's code asking which profile): told
// in its own numbering. (Translating every move kept the selection from
// passing the hidden row: the list couldn't scroll, 0.29.)
int g_hiddenRow = -1;
bool g_listInner = false;           // a message sent to the list from here
bool g_pickArmed = false;           // A pressed on the list: the next selection read is the pick

// --- The gamer zones (Edit Gamer Profile > Gamer Zone) ---------------------------
// 17559's zone list (gamerprofile's SelectGamerZoneScene) showed 17559's art
// (sharedres://ico_32x_Zone_*.png, its own redraw) and words. Its pictures:
// 1888's, in our package (tools/guide1888.py
// ZONE_ICONS, from 1888 xam's shrdres.bin). Its side panel: 1888's text for
// each zone (1888 xam shrdres.bin's English strings, word for word).
struct Zone { LPCWSTR icon, ours, about; };
const Zone kZones[] = {
    { L"ico_32x_Zone_RR.png", L"file://SlopGuide:/blades1888.xzp#ico_32x_Zone_RR.png",
      L"It's not whether you win or lose, but that everyone enjoys the game.\r\n\r\n"
      L"Kick back, relax, be considerate, and have fun." },
    { L"ico_32x_Zone_Pro.png", L"file://SlopGuide:/blades1888.xzp#ico_32x_Zone_Pro.png",
      L"You aren't going to settle for anything less than first place, but you respect the game and "
      L"your competitors.\r\n\r\nPlay clean, play hard, play to win." },
    { L"ico_32x_Zone_Family.png", L"file://SlopGuide:/blades1888.xzp#ico_32x_Zone_Family.png",
      L"You want a zone you and your kids can enjoy. No mature language, no trash talk, no "
      L"super-competitive egos.\r\n\r\nIn short, fun for the whole family!" },
    { L"ico_32x_Zone_Underground.png", L"file://SlopGuide:/blades1888.xzp#ico_32x_Zone_Underground.png",
      L"If you aren't afraid of trash talk, the Underground is for you. Not for the faint of heart."
      L"\r\n\r\nYour battle cry: take no prisoners!" },
};
int g_zoneOfRow[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };   // the list's rows, by their pictures
HXUIOBJ g_zoneList = NULL;
int g_zoneFrames = 0;               // frames left to put 1888's text in the side panel
int g_zoneRows = -1;                // the console's own rows; "Custom" is the one after them
bool g_cardStale = false;
int g_closeFrames = 0;              // frames until the Guide closes after Done
// Edit Gamer Profile's gamer card (CardZoneInto keeps it): filled when made,
// so a zone picked after showed the old one there;
// filled again 1 and 3 seconds after a pick.
HXUIOBJ g_zoneCard = NULL;
int g_zoneCardFrames = 0;           // the gamer card to be read from the profile again (FillCard)

// A zone of the user's own: the profile keeps only one
// of the four (XPROFILE_GAMERCARD_ZONE), so its name is SlopDash's, kept with
// the profile's card (SlopDashCache:\profiles\<XUID>.czone, UTF-16) and
// shown on the Guide's and the dashboard's gamer cards in the zone's place.
// The row is SlopGuide's: the console's code never sees it (its tables have
// the four).
const WCHAR kCustomZone[] = L"New Zone...";
const WCHAR kCustomAbout[] = L"Name a zone of your own.\r\n\r\nIt joins this list and shows on your gamer card.";
const WCHAR kSavedAbout[] = L"A zone of your own.\r\n\r\nIt shows on your gamer card in place of the four above.";
// The zones made so far, for every profile (SlopDashCache:\zones.txt:
// UTF-16, one per line), listed after the four as rows of their own, then
// "New Zone...".
const int kMaxZones = 12;
WCHAR g_zoneNames[kMaxZones][32];
int g_zoneNameCount = 0;
XOVERLAPPED g_czOv;
WCHAR g_czText[32];
XUID g_czXuid = 0;
bool g_czPending = false;

void CacheWrite(XUID xuid, const char* ext, const void* data, DWORD size);
int GuideUser();

bool CustomZoneName(XUID xuid, WCHAR* out, DWORD count)
{
    char path[96];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X.czone", (unsigned __int64)xuid);
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD got = 0;
    ReadFile(h, out, (count - 1) * sizeof(WCHAR), &got, NULL);
    CloseHandle(h);
    out[got / sizeof(WCHAR)] = 0;
    return out[0] != 0;
}

void ForgetCustomZone(XUID xuid)
{
    char path[96];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X.czone", (unsigned __int64)xuid);
    DeleteFileA(path);
}

void LoadZoneNames()
{
    g_zoneNameCount = 0;
    HANDLE h = CreateFileA("SlopDashCache:\\zones.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    static WCHAR text[kMaxZones * 33];
    DWORD got = 0;
    ReadFile(h, text, sizeof(text) - sizeof(WCHAR), &got, NULL);
    CloseHandle(h);
    text[got / sizeof(WCHAR)] = 0;
    for (WCHAR* line = text; *line && g_zoneNameCount < kMaxZones;)
    {
        WCHAR* end = wcschr(line, L'\n');
        if (end)
            *end = 0;
        if (*line)
            wcsncpy_s(g_zoneNames[g_zoneNameCount++], 32, line, _TRUNCATE);
        if (!end)
            break;
        line = end + 1;
    }
}

void SaveZoneNames()
{
    HANDLE h = CreateFileA("SlopDashCache:\\zones.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    for (int i = 0; i < g_zoneNameCount; i++)
    {
        DWORD put = 0;
        WriteFile(h, g_zoneNames[i], (DWORD)(wcslen(g_zoneNames[i]) * sizeof(WCHAR)), &put, NULL);
        WriteFile(h, L"\n", sizeof(WCHAR), &put, NULL);
    }
    CloseHandle(h);
}

// "New Zone..." pressed: the keyboard.
void AskCustomZone()
{
    int user = GuideUser();
    if (user < 0 || g_czPending || XUserGetXUID((DWORD)user, &g_czXuid) != ERROR_SUCCESS)
        return;
    ZeroMemory(&g_czOv, sizeof(g_czOv));
    g_czText[0] = 0;
    DWORD r = XShowKeyboardUI((DWORD)user, VKBD_DEFAULT, L"", L"New Gamer Zone",
                              L"Name a gamer zone of your own.", g_czText, 16, &g_czOv);
    g_czPending = r == ERROR_IO_PENDING || r == ERROR_SUCCESS;
    Log("1888 Guide: custom zone keyboard: %u", (unsigned)r);
}

// The keyboard's answer (checked once a frame): the name kept, or none.
void CustomZoneDone()
{
    if (!g_czPending || !XHasOverlappedIoCompleted(&g_czOv))
        return;
    g_czPending = false;
    DWORD r = XGetOverlappedExtendedError(&g_czOv);
    if (r != ERROR_SUCCESS)
    {
        Log("1888 Guide: custom zone: cancelled (%u)", (unsigned)r);
        return;
    }
    // (trimmed: an empty name is no custom zone)
    size_t n = wcslen(g_czText);
    while (n && g_czText[n - 1] == L' ')
        g_czText[--n] = 0;
    if (!n)
        return;
    // A new name joins the list (before "New Zone..."), and is the profile's.
    int at = -1;
    for (int i = 0; i < g_zoneNameCount; i++)
        if (!_wcsicmp(g_zoneNames[i], g_czText))
            at = i;
    if (at < 0 && g_zoneNameCount < kMaxZones)
    {
        at = g_zoneNameCount++;
        wcsncpy_s(g_zoneNames[at], 32, g_czText, _TRUNCATE);
        SaveZoneNames();
        if (g_zoneList && g_zoneRows >= 0 && SUCCEEDED(XuiListInsertItems(g_zoneList, g_zoneRows + at, 1)))
        {
            XuiListSetText(g_zoneList, g_zoneRows + at, g_zoneNames[at]);
            XuiListSetCurSel(g_zoneList, g_zoneRows + at);
        }
    }
    CacheWrite(g_czXuid, "czone", g_czText, (DWORD)(n * sizeof(WCHAR)));
    g_cardStale = true;
    g_zoneCardFrames = 90;
    Log("1888 Guide: custom zone \"%S\" (%d made)", g_czText, g_zoneNameCount);
}

// The zone list's own messages: its rows' pictures 1888's (17559's answer,
// then ours for it), and the "Custom" row after the console's.
bool ZoneListMsg(HXUIOBJ list, XUIMessage* msg)
{
    if (g_listInner)
        return false;
    DWORD m = msg->dwMessage;
    g_zoneList = list;
    if (m == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        g_listInner = true;
        XuiSendMessage(list, msg);
        g_listInner = false;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            g_zoneRows = d->cItems;
            d->cItems++;
        }
        msg->bHandled = TRUE;
        return true;
    }
    if (m == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (!d->bItemData || g_zoneRows < 0 || d->iItem != g_zoneRows)
            return false;
        static WCHAR name[32];
        XUID xuid = 0;
        int user = GuideUser();
        if (d->iData == 0)
        {
            if (user < 0 || XUserGetXUID((DWORD)user, &xuid) != ERROR_SUCCESS ||
                !CustomZoneName(xuid, name, ARRAYSIZE(name)))
                wcscpy_s(name, ARRAYSIZE(name), kCustomZone);
            d->szText = name;
        }
        else
            d->szText = L"";
        msg->bHandled = TRUE;
        return true;
    }
    if (m == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData && g_zoneRows >= 0 && d->iItem == g_zoneRows)
        {
            d->szPath = NULL;           // (no picture: none of 1888's is the user's own)
            msg->bHandled = TRUE;
            return true;
        }
        g_listInner = true;
        XuiSendMessage(list, msg);
        g_listInner = false;
        if (d->szPath)
            for (int i = 0; i < ARRAYSIZE(kZones); i++)
            {
                const WCHAR* tail = wcsrchr(d->szPath, L'/');
                if (tail && !_wcsicmp(tail + 1, kZones[i].icon))
                {
                    d->szPath = kZones[i].ours;
                    if (d->iItem >= 0 && d->iItem < ARRAYSIZE(g_zoneOfRow))
                        g_zoneOfRow[d->iItem] = i;
                    g_zoneFrames = 30;
                    break;
                }
            }
        msg->bHandled = TRUE;
        return true;
    }
    return false;
}

// The list's notifications at its screen: "Custom" is SlopGuide's alone (the
// console's code would read its tables past their four); a built-in zone
// pressed ends any custom name. True if handled here.
// The zone list, known by its notifications (17559 fills it once, when its
// screen starts: rows, names and pictures put in, 9011C008 - it never asks
// for them, so ZoneListMsg never saw it). Its rows are the zones in kZones'
// order (gamerprofile's table at 90103240: Recreation, Pro, Family,
// Underground); "Custom" goes in after them, once per list.
void ZoneListSeen(HXUIOBJ list)
{
    static HXUIOBJ s_added = NULL;
    g_zoneList = list;
    g_zoneFrames = 30;
    for (int i = 0; i < ARRAYSIZE(kZones); i++)
        g_zoneOfRow[i] = i;
    if (s_added == list)
        return;
    s_added = list;
    // 17559 puts its four rows in at the top (9011C038: insert 4 at 0), so
    // 1888's list keeps its own design-time row after them, empty: the log,
    // 2026-10-17, "5 rows". That row is Custom; with none, one is added.
    int rows = XuiListGetItemCount(list);
    const int custom = ARRAYSIZE(kZones);
    if (rows < custom)
    {
        Log("1888 Guide: zone list has %d rows: no Custom row", rows);
        return;
    }
    LoadZoneNames();
    int want = custom + g_zoneNameCount + 1;
    HRESULT hr = S_OK;
    if (rows < want)
        hr = XuiListInsertItems(list, rows, want - rows);
    else if (rows > want)
        XuiListDeleteItems(list, want, rows - want);
    if (FAILED(hr))
    {
        Log("1888 Guide: zone list: own rows not added (%08X)", (unsigned)hr);
        return;
    }
    g_zoneRows = custom;
    for (int i = 0; i < g_zoneNameCount; i++)
        XuiListSetText(list, custom + i, g_zoneNames[i]);
    XuiListSetText(list, custom + g_zoneNameCount, kCustomZone);
    Log("1888 Guide: zone list: %d of your own, New Zone at row %d", g_zoneNameCount, custom + g_zoneNameCount);
}

bool ZoneListNotify(XUINotify* n, XUIMessage* msg)
{
    if (!n->hObjSource)
        return false;
    if (n->hObjSource != g_zoneList || !g_zoneList)
    {
        LPCWSTR raw = NULL;
        if (FAILED(((GetIdFn)p_GetId)(n->hObjSource, &raw)) || !raw || wcscmp(raw, L"zoneList"))
            return false;
        ZoneListSeen(n->hObjSource);
    }
    int row = XuiListGetCurSel(g_zoneList, NULL);
    bool custom = g_zoneRows >= 0 && row >= g_zoneRows;          // a zone of the user's, or New Zone
    bool newZone = custom && row == g_zoneRows + g_zoneNameCount;
    if (n->dwNotify == XN_SELCHANGED)
    {
        g_zoneFrames = 30;
        if (custom)
        {
            msg->bHandled = TRUE;
            return true;
        }
        return false;
    }
    if (n->dwNotify != XN_PRESS)
        return false;
    if (newZone)
    {
        AskCustomZone();
        msg->bHandled = TRUE;
        return true;
    }
    XUID xuid = 0;
    if (custom)
    {
        // One of the user's own: the profile's zone on the cards.
        const WCHAR* name = g_zoneNames[row - g_zoneRows];
        int user = GuideUser();
        if (user >= 0 && XUserGetXUID((DWORD)user, &xuid) == ERROR_SUCCESS && xuid)
            CacheWrite(xuid, "czone", name, (DWORD)(wcslen(name) * sizeof(WCHAR)));
        g_cardStale = true;
        g_zoneCardFrames = 90;
        Log("1888 Guide: zone \"%S\" picked", name);
        msg->bHandled = TRUE;
        return true;
    }
    int user = GuideUser();
    if (user >= 0 && XUserGetXUID((DWORD)user, &xuid) == ERROR_SUCCESS && xuid)
        ForgetCustomZone(xuid);
    g_cardStale = true;
    g_zoneCardFrames = 90;              // Edit Gamer Profile's card, once the zone is written
    return false;
}

// The zone list's pictures, as 17559 puts them in (gamerprofile 9011BF48: an
// XM_SET_SOURCE_IMAGE to the list's outer per row, its path from the table at
// 90103240, sharedres://ico_32x_Zone_*.png): 1888's own in its place, on the
// way in. Known by the path alone (the message goes to the list's outer,
// which needn't be named zoneList).
void ZoneIconSwap(XUIMessage* msg)
{
    XUIMessageSetSourceImage* d = (XUIMessageSetSourceImage*)msg->pvData;
    if (!d || !d->szPath || wcsncmp(d->szPath, L"sharedres://", 12))
        return;
    for (int i = 0; i < ARRAYSIZE(kZones); i++)
        if (!_wcsicmp(d->szPath + 12, kZones[i].icon))
        {
            d->szPath = kZones[i].ours;
            if (d->iItem >= 0 && d->iItem < ARRAYSIZE(g_zoneOfRow))
                g_zoneOfRow[d->iItem] = i;
            g_zoneFrames = 30;
            Log("1888 Guide: zone row %d: 1888's %S", d->iItem, kZones[i].icon);
            return;
        }
}

void SetChildText(HXUIOBJ parent, LPCWSTR id, LPCWSTR text);

// 1888's text for the selected zone in the side panel (both its labels: the
// panel crossfades between them), over what 17559's code put there.
void ZonePanel()
{
    if (!g_zoneFrames || !g_zoneList)
        return;
    g_zoneFrames--;
    int row = XuiListGetCurSel(g_zoneList, NULL);
    LPCWSTR about = NULL;
    if (g_zoneRows >= 0 && row == g_zoneRows + g_zoneNameCount)
        about = kCustomAbout;
    else if (g_zoneRows >= 0 && row >= g_zoneRows && row < g_zoneRows + g_zoneNameCount)
        about = kSavedAbout;
    else if (row >= 0 && row < ARRAYSIZE(g_zoneOfRow) && g_zoneOfRow[row] >= 0)
        about = kZones[g_zoneOfRow[row]].about;
    if (!about)
        return;
    HXUIOBJ panel = Child(Parent(g_zoneList), L"metaPanelScene");
    SetChildText(panel, L"Text1", about);
    SetChildText(panel, L"Text2", about);
}

// --- The user's own gamer pictures (Change Gamer Picture) ----------------------------
// PNG or JPG files in Hdd:\SlopDash\GamerPictures (SlopDashPics:, made at
// start: SlopGuide.cpp), put there over FTP or with SlopDash's File Manager
//, listed after
// 1888's pictures in Change Gamer Picture's grid. The profile's gamer picture
// itself is one of the console's keys (XPROFILE_GAMERCARD_PICTURE_KEY), so a
// photo is SlopDash's: kept with the profile's card (SlopDashCache:\profiles\
// <XUID>.cpic, its file name; .png its bytes) and shown on the Guide's and the
// dashboard's gamer cards. A picture of the console's picked ends it.
const int kMaxPics = 32;
WCHAR g_picFile[kMaxPics][128];     // the files' paths (SlopDashPics:\GamerPictures\a.png)
WCHAR g_picPath[kMaxPics][160];     // and as XUI loads them
int g_picCount = 0;
int g_tileRows = -1;                // the console's own pictures; ours after them
HXUIOBJ g_tileList = NULL;

// The folder: the hard drive's (SlopDashPics: is Hdd:\SlopDash). Not the USB
// stick's too (0.88-0.98): listed from the notification thread, the stick's
// file system held FindFirstFile, and that thread with it, from the Guide's
// second opening on - the Guide's open and closed (2026-10-18), as 0.4-0.8's log writes to the stick did.
const char* const kPicDirs[] = { "SlopDashPics:\\GamerPictures" };

// A file's path as XUI loads it: "SlopDashPics:\GamerPictures\a.png" ->
// "file://SlopDashPics:/GamerPictures/a.png".
void PicXuiPath(const WCHAR* file, WCHAR* out, DWORD count)
{
    _snwprintf_s(out, count, _TRUNCATE, L"file://%s", file);
    for (WCHAR* c = out; *c; c++)
        if (*c == L'\\')
            *c = L'/';
}

void LoadPics()
{
    g_picCount = 0;
    for (int d = 0; d < ARRAYSIZE(kPicDirs) && g_picCount < kMaxPics; d++)
    {
        char pattern[64];
        _snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s\\*", kPicDirs[d]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            const char* dot = strrchr(fd.cFileName, '.');
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !dot ||
                (_stricmp(dot, ".png") && _stricmp(dot, ".jpg") && _stricmp(dot, ".jpeg")))
                continue;
            int i = g_picCount;
            _snwprintf_s(g_picFile[i], ARRAYSIZE(g_picFile[i]), _TRUNCATE, L"%S\\%S", kPicDirs[d], fd.cFileName);
            PicXuiPath(g_picFile[i], g_picPath[i], ARRAYSIZE(g_picPath[i]));
            g_picCount = i + 1;         // (counted once whole: the Guide's thread may be reading)
        } while (g_picCount < kMaxPics && FindNextFileA(h, &fd));
        FindClose(h);
    }
}

// The profile's own photo (its file's path), if it has one that's still there.
bool OwnPicture(XUID xuid, WCHAR* path, DWORD count)
{
    char cpic[96];
    _snprintf_s(cpic, sizeof(cpic), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X.cpic", (unsigned __int64)xuid);
    HANDLE h = CreateFileA(cpic, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    WCHAR kept[128] = { 0 };
    DWORD got = 0;
    ReadFile(h, kept, sizeof(kept) - sizeof(WCHAR), &got, NULL);
    CloseHandle(h);
    kept[got / sizeof(WCHAR)] = 0;
    if (!kept[0])
        return false;
    WCHAR file[160];
    if (wcschr(kept, L':'))
        wcscpy_s(file, ARRAYSIZE(file), kept);
    else                                // (0.83-0.87 kept the name alone: the hard drive's folder)
        _snwprintf_s(file, ARRAYSIZE(file), _TRUNCATE, L"SlopDashPics:\\GamerPictures\\%s", kept);
    char narrow[160];
    _snprintf_s(narrow, sizeof(narrow), _TRUNCATE, "%S", file);
    if (GetFileAttributesA(narrow) == (DWORD)-1)
        return false;
    PicXuiPath(file, path, count);
    return true;
}

void ForgetOwnPicture(XUID xuid)
{
    char cpic[96];
    _snprintf_s(cpic, sizeof(cpic), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X.cpic", (unsigned __int64)xuid);
    DeleteFileA(cpic);
}

// The grid's own messages (its class asks for its rows): ours after its own.
bool TileListMsg(HXUIOBJ list, XUIMessage* msg)
{
    if (g_listInner)
        return false;
    DWORD m = msg->dwMessage;
    if (m == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        g_listInner = true;
        XuiSendMessage(list, msg);
        g_listInner = false;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            if (g_tileList != list)
            {
                g_tileList = list;          // (the pictures: listed as the Guide opened, NotifyThread)
                Log("1888 Guide: gamer pictures: %d of the console's, %d of your own", d->cItems, g_picCount);
            }
            g_tileRows = d->cItems;
            d->cItems += g_picCount;
        }
        msg->bHandled = TRUE;
        return true;
    }
    if (g_tileList != list || g_tileRows < 0)
        return false;
    if (m == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (!d->bItemData || d->iItem < g_tileRows || d->iItem >= g_tileRows + g_picCount)
            return false;
        d->szPath = g_picPath[d->iItem - g_tileRows];
        msg->bHandled = TRUE;
        return true;
    }
    if (m == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (!d->bItemData || d->iItem < g_tileRows || d->iItem >= g_tileRows + g_picCount)
            return false;
        d->szText = L"";
        msg->bHandled = TRUE;
        return true;
    }
    return false;
}

// The grid's notifications at its screen: our rows are ours alone (the
// console's code would read past its own pictures); one pressed is the
// profile's photo, and the screen goes back as after a pick of its own.
bool TileListNotify(XUINotify* n, XUIMessage* msg)
{
    if (!n->hObjSource || n->hObjSource != g_tileList || g_tileRows < 0)
        return false;
    int row = XuiListGetCurSel(g_tileList, NULL);
    bool own = row >= g_tileRows && row < g_tileRows + g_picCount;
    XUID xuid = 0;
    int user = GuideUser();
    if (user < 0 || XUserGetXUID((DWORD)user, &xuid) != ERROR_SUCCESS || !xuid)
        return false;
    if (n->dwNotify == XN_SELCHANGED)
    {
        if (own)
            msg->bHandled = TRUE;
        return own;
    }
    if (n->dwNotify != XN_PRESS)
        return false;
    if (!own)
    {
        ForgetOwnPicture(xuid);         // one of the console's: its own picture again
        g_cardStale = true;
        return false;
    }
    const WCHAR* name = g_picFile[row - g_tileRows];
    CacheWrite(xuid, "cpic", name, (DWORD)(wcslen(name) * sizeof(WCHAR)));
    BYTE* data = NULL;
    UINT size = 0;
    if (SUCCEEDED(((LoadAllFn)p_ResourceLoadAll)(g_picPath[row - g_tileRows], &data, &size)) && data)
    {
        CacheWrite(xuid, "png", data, size);
        ((FreeFn)p_Free)(data);
    }
    g_cardStale = true;
    Log("1888 Guide: your own gamer picture: %S (%u bytes)", name, (unsigned)size);
    HXUIOBJ back = Child(Parent(g_tileList), L"backButton");
    if (back)
    {
        XUIMessage key;
        XUIMessageInput in = { 0 };
        XuiMessageInput(&key, &in, XUI_KEYDOWN, VK_PAD_B, 0, 0, (BYTE)user);
        XuiSendMessage(back, &key);
    }
    msg->bHandled = TRUE;
    return true;
}

int RealRow(int row)
{
    return g_hiddenRow >= 0 && row >= g_hiddenRow ? row + 1 : row;
}

void FindHiddenRow(HXUIOBJ list, int count)
{
    g_hiddenRow = -1;
    g_listInner = true;
    for (int i = 0; i < count; i++)
    {
        XUIMessage m;
        XUIMessageGetSourceText d;
        XuiMessageGetSourceText(&m, &d, i, 0, TRUE);
        XuiSendMessage(list, &m);
        if (d.szText && !_wcsicmp(d.szText, L"ABadAvatar"))
        {
            g_hiddenRow = i;
            break;
        }
    }
    g_listInner = false;
}

// True if handled here.
bool HideBadAvatar(HXUIOBJ list, XUIMessage* msg)
{
    if (g_listInner)
        return false;
    DWORD m = msg->dwMessage;
    if (m == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType != XUI_ITEMCOUNT_ALL)
            return false;
        g_listInner = true;
        XuiSendMessage(list, msg);
        g_listInner = false;
        int real = d->cItems;
        FindHiddenRow(list, real);
        if (g_hiddenRow >= 0)
            d->cItems = real - 1;
        msg->bHandled = TRUE;
        return true;
    }
    if (g_hiddenRow < 0)
        return false;
    if (m == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData)
            d->iItem = RealRow(d->iItem);
    }
    else if (m == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
            d->iItem = RealRow(d->iItem);
    }
    else if (m == XM_GET_CURSEL && g_pickArmed)
    {
        g_pickArmed = false;
        g_listInner = true;
        XuiSendMessage(list, msg);
        g_listInner = false;
        XUIMessageGetCurSel* d = (XUIMessageGetCurSel*)msg->pvData;
        if (d->iItem >= 0)
            d->iItem = RealRow(d->iItem);
        msg->bHandled = TRUE;
        return true;
    }
    return false;
}

// --- The signed-in main menu (MainMenuSignedIn.xur) ------------------------------------
// 1888's MainMenuSignedIn code (hud 913E82F8) isn't on the console: it showed
// the gamer card filled and in its "Mini" size (ctlGamerCard: P0 Mini, 420 x
// 94), and one of each pair laid out in the same place: on Xbox Live the
// community bar (scnCommunity) and Private Chat (btnQuickChat), offline "Not
// connected to Xbox Live" (txtSignInLive) and Connect to Xbox Live
// (btnSignInLive). The console's own card code clears the 1888 card's texts
// without filling them, so the card is filled here as SlopDash fills its own
// (src/theme/blades1888.cpp BuildCard): the Live base, the gold banner, Rep
// five stars, Zone "Homebrew"; the gamertag from the system, the gamerscore
// and picture from SlopDash's profile cache (SlopDashCache: =
// game:\cache: profiles\<XUID>.card "gamerscore games achievements", .png).
typedef HRESULT (APIENTRY *PlayTimelineFn)(HXUIOBJ, int, int, int, BOOL, BOOL);
typedef HRESULT (APIENTRY *FindFrameFn)(HXUIOBJ, LPCWSTR, int*);
typedef HRESULT (APIENTRY *SetImagePathFn)(HXUIOBJ, LPCWSTR);

void ShowChild(HXUIOBJ parent, LPCWSTR id, bool show)
{
    HXUIOBJ c = Child(parent, id);
    if (c)
        XuiElementSetShow(c, show ? TRUE : FALSE);
}

void SetChildText(HXUIOBJ parent, LPCWSTR id, LPCWSTR text)
{
    HXUIOBJ c = Child(parent, id);
    if (c)
        XuiControlSetText(c, text);
}

struct RawString { DWORD type; const WCHAR* sz; DWORD pad[3]; };     // an XUIElementPropVal holding a string

void SetNav(HXUIOBJ obj, LPCWSTR prop, LPCWSTR target)
{
    DWORD id = 0;
    if (!obj || FAILED(((GetPropIdFn)p_GetPropId)(obj, prop, &id)))
        return;
    RawString v = { XUI_EPT_STRING, target, { 0, 0, 0 } };
    ((SetPropFn)p_SetProp)(obj, id, 0, &v);
}

void PlayNamed(HXUIOBJ obj, LPCWSTR from, LPCWSTR to)
{
    int a = -1, b = -1;
    if (!obj || FAILED(((FindFrameFn)p_FindFrame)(obj, from, &a)) || FAILED(((FindFrameFn)p_FindFrame)(obj, to, &b)))
        return;
    ((PlayTimelineFn)p_PlayTimeline)(obj, a, a, b, FALSE, FALSE);
}

int GuideUser()
{
    for (DWORD i = 0; i < 4; i++)
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn)
            return (int)i;
    return -1;
}

// --- The gamer card, from the profile itself ---------------------------------------
// Read from the dashboard's settings as Edit Gamer Profile reads them (xam 537,
// title FFFE07D1): the gamer zone (XPROFILE_GAMERCARD_ZONE, xonline.h: 0 Xbox
// 1, 1 Recreation, 2 Pro, 3 Family, 4 Underground), gamerscore, games played,
// achievements and the gamer picture's key (XPROFILE_GAMERCARD_PICTURE_KEY,
// "fffe07d10002000c0001000c": 1888's own pictures are 64_<key>.png, in our
// package). A new profile, signed in since it was made, never reached
// SlopDash's cache (it reads a profile's file while it's signed out), so the
// card had no gamerscore or picture. What's read is
// kept for SlopDash's card too (SlopDashCache:\profiles\<XUID>: .card
// "score games achievements", .zone the number, .png the picture).
struct CardInfo
{
    int zone, score, games, earned;
    WCHAR key[48];                      // the picture's key, or empty
    WCHAR motto[32];                    // the motto (XPROFILE_GAMERCARD_MOTTO), or empty
};

bool ReadCard(int user, CardInfo& c)
{
    c.zone = c.score = c.games = c.earned = -1;
    c.key[0] = 0;
    c.motto[0] = 0;
    typedef DWORD (*ReadSettingsFn)(DWORD, DWORD, DWORD, const XUID*, DWORD, const DWORD*, DWORD*,
                                    PXUSER_READ_PROFILE_SETTING_RESULT, PXOVERLAPPED);
    if (user < 0 || !p_ReadSettings)
        return false;
    const DWORD ids[] = { XPROFILE_GAMERCARD_ZONE, XPROFILE_GAMERCARD_CRED, XPROFILE_GAMERCARD_TITLES_PLAYED,
                          XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED, XPROFILE_GAMERCARD_PICTURE_KEY,
                          XPROFILE_GAMERCARD_MOTTO };
    static BYTE buffer[1024];
    DWORD size = sizeof(buffer);
    PXUSER_READ_PROFILE_SETTING_RESULT r = (PXUSER_READ_PROFILE_SETTING_RESULT)buffer;
    DWORD hr = ((ReadSettingsFn)p_ReadSettings)(kDashTitle, (DWORD)user, 0, NULL, ARRAYSIZE(ids), ids, &size, r, NULL);
    if (hr != ERROR_SUCCESS || !r->pSettings)
    {
        Log("1888 Guide: gamer card settings: %08X", (unsigned)hr);
        return false;
    }
    for (DWORD i = 0; i < r->dwSettingsLen; i++)
    {
        const XUSER_PROFILE_SETTING& s = r->pSettings[i];
        if (s.data.type == XUSER_DATA_TYPE_INT32)
        {
            int v = (int)s.data.nData;
            if (s.dwSettingId == XPROFILE_GAMERCARD_ZONE) c.zone = v;
            else if (s.dwSettingId == XPROFILE_GAMERCARD_CRED) c.score = v;
            else if (s.dwSettingId == XPROFILE_GAMERCARD_TITLES_PLAYED) c.games = v;
            else if (s.dwSettingId == XPROFILE_GAMERCARD_ACHIEVEMENTS_EARNED) c.earned = v;
        }
        else if (s.dwSettingId == XPROFILE_GAMERCARD_MOTTO && s.data.type == XUSER_DATA_TYPE_UNICODE &&
                 s.data.string.pwszData && s.data.string.cbData >= 2)
            wcsncpy_s(c.motto, ARRAYSIZE(c.motto), s.data.string.pwszData,
                      min(s.data.string.cbData / 2, (DWORD)ARRAYSIZE(c.motto) - 1));
        else if (s.dwSettingId == XPROFILE_GAMERCARD_PICTURE_KEY && s.data.type == XUSER_DATA_TYPE_UNICODE &&
                 s.data.string.pwszData && s.data.string.cbData >= 2)
        {
            // The key is the 24 characters of title, kind and picture
            // ("fffe07d10002000c0001000c"); the setting pads it out (with
            // zeros: the log, 2026-10-17), which matched no picture.
            DWORD n = min(s.data.string.cbData / 2, (DWORD)24);
            wcsncpy_s(c.key, ARRAYSIZE(c.key), s.data.string.pwszData, n);
            for (WCHAR* k = c.key; *k; k++)
                if (*k >= L'A' && *k <= L'Z')
                    *k += L'a' - L'A';
        }
    }
    Log("1888 Guide: gamer card: zone %d, gamerscore %d, games %d, achievements %d, picture %S, motto \"%S\"",
        c.zone, c.score, c.games, c.earned, c.key, c.motto);
    return true;
}

void CacheWrite(XUID xuid, const char* ext, const void* data, DWORD size)
{
    char path[96];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X.%s", (unsigned __int64)xuid, ext);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD put = 0;
    WriteFile(h, data, size, &put, NULL);
    CloseHandle(h);
}

LPCWSTR ZoneName(int zone)
{
    static const LPCWSTR kNames[] = { NULL, L"Recreation", L"Pro", L"Family", L"Underground" };
    return zone >= 1 && zone < ARRAYSIZE(kNames) ? kNames[zone] : L"None";
}

// The card's look, SlopDash's choice (System > Themes > X > Gamer Card:
// SlopDashCache:\gamercard.txt, "" Gold, "Silver", or
// "Offline"): 1888's Xbox Live Gold or Silver banner on the Live card, or its
// offline card (whose Gamerscore row is the Zone here: tools/guide1888.py
// zone_row), filled as the Live card is.
int CardStyleNow()
{
    char text[16] = { 0 };
    HANDLE h = CreateFileA("SlopDashCache:\\gamercard.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    DWORD got = 0;
    ReadFile(h, text, sizeof(text) - 1, &got, NULL);
    CloseHandle(h);
    return !strcmp(text, "Silver") ? 1 : !strcmp(text, "Offline") ? 2 : 0;
}

void CardLook(HXUIOBJ card, int style, LPCWSTR tag, LPCWSTR games, LPCWSTR zone, LPCWSTR pic)
{
    HXUIOBJ online = Child(card, L"onlineBaseScene"), offline = Child(card, L"offlineBaseScene");
    HXUIOBJ tier = Child(online, L"membershipTierScene");
    ShowChild(tier, L"graphic_gold", style == 0);
    ShowChild(tier, L"graphic_silver", style == 1);
    ShowChild(tier, L"graphic_mem", false);
    if (online)
        XuiElementSetShow(online, style != 2);
    if (offline)
        XuiElementSetShow(offline, style == 2);
    if (style != 2 || !offline)
        return;
    SetChildText(offline, L"gamerTagText", tag);
    SetChildText(offline, L"titlesPlayedText", games);
    SetChildText(offline, L"zoneValue", zone);
    HXUIOBJ tile = Child(offline, L"gamerTileImage");
    if (tile && pic && pic[0])
        ((SetImagePathFn)p_SetImagePath)(tile, pic);
}

void FillCard(HXUIOBJ card)
{
    int user = GuideUser();
    if (!card || user < 0)
        return;
    PlayNamed(card, L"Mini", L"EndMini");
    HXUIOBJ base = Child(card, L"onlineBaseScene");
    ShowChild(card, L"offlineBaseScene", false);
    ShowChild(card, L"extendedScene", false);
    if (!base)
        return;
    XuiElementSetShow(base, TRUE);
    PlayNamed(base, L"Mini", L"Mini");
    HXUIOBJ tier = Child(base, L"membershipTierScene");
    ShowChild(tier, L"graphic_gold", true);
    ShowChild(tier, L"graphic_silver", false);
    ShowChild(tier, L"graphic_mem", false);
    HXUIOBJ rep = Child(base, L"reputationScene");
    WCHAR star[16];
    for (int i = 1; i <= 5; i++)
    {
        _snwprintf_s(star, ARRAYSIZE(star), _TRUNCATE, L"Starfull%d", i);
        ShowChild(rep, star, true);
    }
    char name[XUSER_NAME_SIZE + 1] = { 0 };
    WCHAR tag[XUSER_NAME_SIZE + 1] = { 0 };
    if (XUserGetName((DWORD)user, name, sizeof(name)) == ERROR_SUCCESS)
        for (int i = 0; name[i] && i < XUSER_NAME_SIZE; i++)
            tag[i] = (WCHAR)(unsigned char)name[i];
    SetChildText(base, L"gamerTagText", tag);
    XUID xuid = 0;
    // The texts are put back on every focus change (the console's own card
    // code clears them), but the profile is read and the picture set once per
    // card and profile: done each time, every move in the menu read the USB
    // stick and reloaded the picture, and the Guide crawled.
    static HXUIOBJ s_card = NULL;
    static XUID s_xuid = 0;
    static WCHAR score[16] = { 0 };
    static LPCWSTR zone = L"None";
    static int style = 0;
    static WCHAR games[16] = { 0 };
    XUserGetXUID((DWORD)user, &xuid);
    if (card == s_card && xuid == s_xuid && !g_cardStale)
    {
        SetChildText(base, L"credText", score);
        SetChildText(base, L"zoneText", zone);
        CardLook(card, style, tag, games, zone, NULL);
        return;
    }
    style = CardStyleNow();
    games[0] = 0;
    g_cardStale = false;
    s_card = card;
    s_xuid = xuid;
    score[0] = 0;
    zone = L"None";
    CardInfo c = { -1, -1, -1, -1 };
    if (xuid && ReadCard(user, c))
    {
        zone = ZoneName(c.zone);
        static WCHAR custom[32];
        if (CustomZoneName(xuid, custom, ARRAYSIZE(custom)))
            zone = custom;              // the user's own (Gamer Zone > Custom)
        if (c.score >= 0)
            _snwprintf_s(score, ARRAYSIZE(score), _TRUNCATE, L"%d", c.score);
        _snwprintf_s(games, ARRAYSIZE(games), _TRUNCATE, L"%d", max(c.games, 0));
        char text[48];
        if (c.zone >= 0)
            CacheWrite(xuid, "zone", text, (DWORD)_snprintf_s(text, sizeof(text), _TRUNCATE, "%d", c.zone));
        if (c.score >= 0)
            CacheWrite(xuid, "card", text, (DWORD)_snprintf_s(text, sizeof(text), _TRUNCATE, "%d %d %d", c.score,
                                                              max(c.games, 0), max(c.earned, 0)));
    }
    HXUIOBJ tile = Child(base, L"gamerTileImage");
    WCHAR pic[128];
    pic[0] = 0;
    if (xuid && OwnPicture(xuid, pic, ARRAYSIZE(pic)))
    {
        // The user's own photo (Change Gamer Picture's own rows); its bytes
        // kept for SlopDash's card.
        BYTE* data = NULL;
        UINT size = 0;
        if (SUCCEEDED(((LoadAllFn)p_ResourceLoadAll)(pic, &data, &size)) && data)
        {
            CacheWrite(xuid, "png", data, size);
            ((FreeFn)p_Free)(data);
        }
    }
    else if (xuid && c.key[0])
    {
        // 1888's picture for the key, from our package; its bytes kept for
        // SlopDash's card.
        _snwprintf_s(pic, ARRAYSIZE(pic), _TRUNCATE, L"file://SlopGuide:/blades1888.xzp#64_%s.png", c.key);
        BYTE* data = NULL;
        UINT size = 0;
        if (SUCCEEDED(((LoadAllFn)p_ResourceLoadAll)(pic, &data, &size)) && data)
        {
            CacheWrite(xuid, "png", data, size);
            ((FreeFn)p_Free)(data);
        }
        else
            pic[0] = 0;                 // not one of 1888's: the cached picture, if any
    }
    if (!pic[0] && xuid)
    {
        char path[96];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X.png", (unsigned __int64)xuid);
        if (GetFileAttributesA(path) != (DWORD)-1)
            _snwprintf_s(pic, ARRAYSIZE(pic), _TRUNCATE, L"file://SlopDashCache:/profiles/%016I64X.png",
                         (unsigned __int64)xuid);
    }
    if (tile && pic[0])
        ((SetImagePathFn)p_SetImagePath)(tile, pic);
    SetChildText(base, L"credText", score);
    SetChildText(base, L"zoneText", zone);
    CardLook(card, style, tag, games, zone, pic);
    Log("1888 Guide: gamer card %S, gamerscore %S, zone %S, picture %S, look %d", tag, score, zone, pic, style);
}

// The profile's zone on the cards that show a profile being made or edited
// (Save Gamer Profile, and the offline gamer card on Edit Gamer Profile and
// its screens): in place of Gamerscore and Achievements, which say the same
// thing there. Its row's
// value is "zoneValue": in the card's offlineBaseScene, or the screen itself.
void CardZoneInto(HXUIOBJ scene)
{
    int user = GuideUser();
    if (!scene || user < 0)
        return;
    LPCWSTR zone = L"None";
    CardInfo c = { -1, -1, -1, -1 };
    XUID xuid = 0;
    if (ReadCard(user, c))
        zone = ZoneName(c.zone);
    XUserGetXUID((DWORD)user, &xuid);
    if (xuid && c.zone >= 0)
    {
        // Kept for SlopDash's card too: after Done the Guide closes, and its
        // main menu (FillCard, which kept it before) may never show.
        char text[16];
        CacheWrite(xuid, "zone", text, (DWORD)_snprintf_s(text, sizeof(text), _TRUNCATE, "%d", c.zone));
    }
    static WCHAR custom[32];
    if (xuid && CustomZoneName(xuid, custom, ARRAYSIZE(custom)))
        zone = custom;
    HXUIOBJ base = Child(scene, L"offlineBaseScene");
    if (base)
        g_zoneCard = scene;
    SetChildText(base ? base : scene, L"zoneValue", zone);
    WCHAR own[128];
    HXUIOBJ tile = Child(base ? base : scene, L"gamerTileImage");
    if (tile && xuid && OwnPicture(xuid, own, ARRAYSIZE(own)))
        ((SetImagePathFn)p_SetImagePath)(tile, own);        // the user's own photo
    Log("1888 Guide: card zone %S", zone);
}

// The main menu's paired parts, by whether the user is on Xbox Live.
extern HXUIOBJ g_recScreen;         // (Screen Recorder's state line: RecorderScreen, below)
extern DWORD g_recScreenTick;

// --- Screen Recorder on the Guide ------------------------------
// The main menus' recorder panel (Record, Stop and its state) in the XBOX 360
// banner's place, or the banner again with it hidden: Personal Settings >
// Screen Recorder > Show on Guide, kept in Hdd:\SlopDash\recorder.txt
// ("show=1" or "show=0"; shown when there's none). On the hard drive, not
// the stick: SlopGuide's file use on the stick has hung before (kPicDirs).
int g_recShow = -1;                 // -1: not read yet
int g_recFocus = -1;                // the panel's highlighted button: 0 Record, 1 Stop, 2 Stream

bool RecorderShown()
{
    if (g_recShow < 0)
    {
        g_recShow = 1;
        HANDLE h = CreateFileA("SlopDashPics:\\recorder.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE)
        {
            char text[32] = { 0 };
            DWORD got = 0;
            ReadFile(h, text, sizeof(text) - 1, &got, NULL);
            CloseHandle(h);
            if (strstr(text, "show=0"))
                g_recShow = 0;
        }
    }
    return g_recShow != 0;
}

void SetRecorderShown(bool show)
{
    g_recShow = show ? 1 : 0;
    HANDLE h = CreateFileA("SlopDashPics:\\recorder.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD put = 0;
        WriteFile(h, show ? "show=1\r\n" : "show=0\r\n", 8, &put, NULL);
        CloseHandle(h);
    }
}

// --- Trainer -------------------------------------------------
// Start + Back in a game with a trainer running (Trainers.inc): the Guide opens
// on Trainer (Trainer.xur, tools/guide1888.py trainer_screen: Notifications'
// own parts), the trainer's name and a tick box a cheat, ticked as it is;
// A switches one.
const int kCheatRows = 12;
volatile LONG g_openTrainer = 0;        // the main menu opens Trainer as it comes up
void SetCheck(HXUIOBJ box, BOOL on);    // (Personal Settings' tick boxes, below)

// The tick box's number, from its id ("chkCheat3" -> 3; -1 if it isn't one).
int CheatRow(LPCWSTR id)
{
    if (wcsncmp(id, L"chkCheat", 8))
        return -1;
    int n = _wtoi(id + 8);
    return n >= 0 && n < kCheatRows && iswdigit(id[8]) ? n : -1;
}

void TrainerScreen(HXUIOBJ scene)
{
    BYTE* list = trainer::List();
    int count = trainer::Count(list);
    int shown = count < kCheatRows ? count : kCheatRows;
    SetChildText(scene, L"txtTrainerName", !list ? L"No trainer is running." :
                                           count ? L"Tick a cheat to switch it on." : L"This trainer has no cheats.");
    LPCWSTR above = NULL;
    static WCHAR ids[kCheatRows][16];
    for (int i = 0; i < kCheatRows; i++)
    {
        _snwprintf_s(ids[i], ARRAYSIZE(ids[i]), _TRUNCATE, L"chkCheat%d", i);
        HXUIOBJ box = Child(scene, ids[i]);
        if (!box)
            continue;
        XuiElementSetShow(box, i < shown);
        if (i >= shown)
            continue;
        trainer::Cheat* c = trainer::At(list, i);
        WCHAR name[64] = L"?";
        if (c->name && MmIsAddressValid((PVOID)c->name))
            _snwprintf_s(name, ARRAYSIZE(name), _TRUNCATE, L"%S", c->name);
        XuiControlSetText(box, name);
        SetCheck(box, c->on);
        SetNav(box, L"NavUp", above ? above : L"");
        if (above)
            SetNav(Child(scene, above), L"NavDown", ids[i]);
        above = ids[i];
    }
    if (above)
        SetNav(Child(scene, above), L"NavDown", L"");
}

// --- Achievements -------------------------------------------
// The console's own achievements code fills 1888's scenes (tools/guide1888.py
// ACHIEVEMENTS). 1888's details screen showed the game's title in its heading,
// which 1888's code filled and 17559's doesn't (it stayed "<game title>"): the
// title of the game whose list was last focused, put there as it's made.
WCHAR g_achGame[64] = L"";

void AchievementsSeen(HXUIOBJ scene)
{
    HXUIOBJ t = Child(scene, L"labGameTitle");
    LPCWSTR s = t ? XuiControlGetText(t) : NULL;
    if (s && *s)
        wcsncpy_s(g_achGame, ARRAYSIZE(g_achGame), s, _TRUNCATE);
}

// The unlock date of the focused achievement: 17559's
// code left its labAcquiredDate empty for these offline unlocks (the console,
// 2026-11-05). From SlopDash's copy of the profile's files (current from the
// last time it was signed out: an achievement won this sign-in isn't in it
// and shows none): the game by its title in FFFE07D1.gpd's title records,
// the achievement by its name in that game's GPD; the date as SlopDash's
// Achievements screen shows it (M/D/YYYY, the console's local time).
bool ReadWhole(const char* path, std::vector<unsigned char>& out)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    bool ok = size != INVALID_FILE_SIZE && size < 4 * 1024 * 1024;
    if (ok)
    {
        out.resize(size ? size : 1);
        ok = size && ReadFile(h, &out[0], size, &got, NULL) && got == size;
    }
    CloseHandle(h);
    return ok;
}

void AchDateFill(HXUIOBJ scene)
{
    static WCHAR lastName[96] = L"", lastGame[64] = L"";
    static std::vector<unsigned char> gpd;
    HXUIOBJ nameLab = Child(scene, L"labAchievementName"), gameLab = Child(scene, L"labGameTitle"),
            dateLab = Child(scene, L"labAcquiredDate");
    LPCWSTR name = nameLab ? XuiControlGetText(nameLab) : NULL;
    LPCWSTR game = gameLab ? XuiControlGetText(gameLab) : NULL;
    if (!dateLab || !name || !*name || !game || !*game)
        return;
    if (!wcscmp(name, lastName) && !wcscmp(game, lastGame))
        return;
    wcsncpy_s(lastName, ARRAYSIZE(lastName), name, _TRUNCATE);
    int user = GuideUser();
    XUID xuid = 0;
    if (user < 0 || XUserGetXUID((DWORD)user, &xuid) != ERROR_SUCCESS)
        return;
    char path[96];
    if (wcscmp(game, lastGame))
    {
        // The game: its title record in the dashboard's file, then its own file.
        wcsncpy_s(lastGame, ARRAYSIZE(lastGame), game, _TRUNCATE);
        gpd.clear();
        std::vector<unsigned char> dash;
        std::vector<xdbf::Entry> entries;
        _snprintf_s(path, sizeof(path), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X\\FFFE07D1.gpd",
                    (unsigned __int64)xuid);
        unsigned title = 0;
        if (ReadWhole(path, dash) && xdbf::Entries(&dash[0], dash.size(), entries))
            for (size_t i = 0; i < entries.size() && !title; i++)
            {
                xdbf::Title t;
                if (xdbf::ReadTitle(entries[i], t) && t.name == game)
                    title = t.titleId;
            }
        if (title)
        {
            _snprintf_s(path, sizeof(path), _TRUNCATE, "SlopDashCache:\\profiles\\%016I64X\\%08X.gpd",
                        (unsigned __int64)xuid, title);
            ReadWhole(path, gpd);
        }
        Log("achievements: %S is title %08X (%u-byte file)", game, title, (unsigned)gpd.size());
    }
    WCHAR when[32] = L"";
    std::vector<xdbf::Entry> entries;
    if (!gpd.empty() && xdbf::Entries(&gpd[0], gpd.size(), entries))
        for (size_t i = 0; i < entries.size(); i++)
        {
            xdbf::Achievement a;
            if (!xdbf::ReadAchievement(entries[i], a) || a.name != name)
                continue;
            if (a.Earned() && a.unlocked)
            {
                FILETIME utc, local;
                SYSTEMTIME t;
                utc.dwLowDateTime = (DWORD)a.unlocked;
                utc.dwHighDateTime = (DWORD)(a.unlocked >> 32);
                if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &t))
                    _snwprintf_s(when, ARRAYSIZE(when), _TRUNCATE, L"%u/%u/%u", t.wMonth, t.wDay, t.wYear);
            }
            break;
        }
    XuiControlSetText(dateLab, when);
}

// The key timer on a screen SlopGuide made: a still screen (the achievement
// details) has no frames to take keys on, and the main menu's timer doesn't
// run under the console's own screens (2026-11-05). On a game's achievements
// it also keeps the unlock date current (AchDateFill).
void KeyTimerOn(HXUIOBJ scene)
{
    if (!scene || !p_SetTimer)
        return;
    typedef HRESULT (*SetTimerFn)(HXUIOBJ, DWORD, DWORD);
    ((SetTimerFn)p_SetTimer)(scene, 0x52474B59, 50);   // (kKeyTimer, "RGKY": defined below)
}

void AchDetailsMade(HXUIOBJ scene)
{
    HXUIOBJ head = Child(scene, L"headerText");
    if (head)
        XuiControlSetText(head, g_achGame);
}

// --- Plugins -------------------------------------------------
// Personal Settings > Plugins (Plugins.xur, tools/guide1888.py plugins_screen):
// Performance (every plugin off), Debug (every one on), Debug Tools, then a
// row per plugin (Plugins.inc). Its rows' words and which rows show are set
// whenever one of them is focused.
const int kPluginRows = 6;

void PluginsScreen(HXUIOBJ scene)
{
    WCHAR text[64];
    int shown = plugins::g_count < kPluginRows ? plugins::g_count : kPluginRows;
    LPCWSTR above = L"btnDebugTools";
    for (int i = 0; i < kPluginRows; i++)
    {
        WCHAR id[16];
        _snwprintf_s(id, ARRAYSIZE(id), _TRUNCATE, L"btnPlugin%d", i);
        HXUIOBJ row = Child(scene, id);
        if (!row)
            continue;
        XuiElementSetShow(row, i < shown);
        if (i >= shown)
            continue;
        plugins::Text(i, text, ARRAYSIZE(text));
        XuiControlSetText(row, text);
        static WCHAR ids[kPluginRows][16];
        wcscpy_s(ids[i], ARRAYSIZE(ids[i]), id);
        SetNav(row, L"NavUp", above);
        SetNav(Child(scene, above), L"NavDown", ids[i]);
        above = ids[i];
    }
    SetNav(Child(scene, above), L"NavDown", L"");
    SetChildText(scene, L"btnDebugTools", plugins::g_debugTools ? L"Debug Tools: On" : L"Debug Tools: Off");
}

// The row's number, from its id ("btnPlugin2" -> 2; -1 if it isn't one).
int PluginRow(LPCWSTR id)
{
    return !wcsncmp(id, L"btnPlugin", 9) && id[9] >= L'0' && id[9] < L'0' + kPluginRows && !id[10] ? id[9] - L'0'
                                                                                                    : -1;
}

void RecorderPanel(HXUIOBJ menu)
{
    HXUIOBJ panel = Child(menu, L"scnRecorderPanel");
    if (!panel)
        return;
    bool show = RecorderShown();
    XuiElementSetShow(panel, show);
    ShowChild(menu, L"btnGameBanner", !show);
    ShowChild(menu, L"tempAdImage", !show);         // (the banner's XBOX 360 picture: drawn over the panel, 0.95)
    SetNav(Child(menu, L"scnMusic"), L"NavDown", show ? L"scnRecorderPanel" : L"btnGameBanner");
    if (show)
    {
        g_recScreen = panel;                // (its state line kept current: RecorderScreen)
        g_recScreenTick = 0;
    }
}

// The main menu's battery icon (imgBatteryIcon: frames Little, Low, Medium,
// High at 0-3, each its ico_32x_Battery1-4.png). 1888's hud drove it (913E7878
// and three more): XamInputGetDeviceStats for the Guide's controller; a
// battery pad (its type word 0x10000000) shows the icon at the frame of its
// level (the stats' first word), anything else hides it. 17559's stats are
// 28 bytes, the type word at 12 instead of 8 (its hud, 913EA95C: level at 0,
// type at 12). The controller: the signed-in one, else the first with a
// battery.
HXUIOBJ g_batteryMenu = NULL;
DWORD g_batteryTick = 0;

void BatteryIcon(HXUIOBJ menu)
{
    HXUIOBJ icon = Child(menu, L"imgBatteryIcon");
    if (!icon || !p_DeviceStats)
        return;
    typedef DWORD (*DeviceStatsFn)(DWORD, DWORD*);
    int first = GuideUser();
    int level = -1;
    for (int k = -1; k < 4 && level < 0; k++)
    {
        int user = k < 0 ? first : k;
        if (user < 0 || (k >= 0 && k == first))
            continue;
        DWORD stats[8] = {0};
        if (((DeviceStatsFn)p_DeviceStats)((DWORD)user, stats) == 0 && stats[3] == 0x10000000)
            level = stats[0] > 3 ? 3 : (int)stats[0];
    }
    XuiElementSetShow(icon, level >= 0);
    if (level >= 0)
    {
        static const LPCWSTR kFrames[] = { L"Little", L"Low", L"Medium", L"High" };
        PlayNamed(icon, kFrames[level], kFrames[level]);
    }
    static int s_logged = -2;
    if (level != s_logged)
    {
        Log("1888 Guide: battery %d (of 3; -1: no battery pad)", level);
        s_logged = level;
    }
    g_batteryMenu = menu;
}

// The level kept current while the menu's up (every 2 s or so).
void BatteryTick()
{
    if (!g_batteryMenu || ++g_batteryTick % 60)
        return;
    LPCWSTR raw = NULL;
    if (FAILED(((GetIdFn)p_GetId)(g_batteryMenu, &raw)) || !raw || wcscmp(raw, L"sceneMainMenu"))
    {
        g_batteryMenu = NULL;           // (gone: the Guide moved on)
        return;
    }
    BatteryIcon(g_batteryMenu);
}

// --- Personal Settings: Voice, Vibration, Notifications, Online Status ----------
// 1888's screens (hud OptionsVoice / OptionsController / OptionsNotifications /
// OptionsOnline) did nothing here. As 1888's hud does them:
//   Voice: Mute (XPROFILE_OPTION_VOICE_MUTED), Volume (..._VOICE_VOLUME: the
//   slider's place as 0-100, 913EAB58), Voice Output (..._VOICE_THRU_SPEAKERS:
//   headset 0, TV 1, both 2);
//   Vibration: XPROFILE_OPTION_CONTROLLER_VIBRATION;
//   Notifications: XNotifyUIGetOptions / SetOptions (show, sound);
//   Online Status: the preset presence state (Online 1, Away 0x10001, Busy
//   0x20001, Appear Offline 5; read back as 913EA5C8 does).
// Profile settings are the signed-in profile's, under the dashboard's title.
// Read as a screen comes up; written a frame after a change (the control has
// changed itself by then).
HXUIOBJ g_personalScene = NULL;
int g_personalDirty = 0;

bool IsPersonalScreen(const std::wstring& id)
{
    return id == L"scnOptionsVoice" || id == L"scnOptionsController" || id == L"scnNotificationsOnline" ||
           id == L"scnOptionsOnline";
}

BOOL GetCheck(HXUIOBJ box)
{
    XUIMessage msg;
    XUIMessageGetCheckState d;
    d.bChecked = FALSE;
    XuiMessageGetCheckstate(&msg, &d);
    if (box)
        XuiSendMessage(box, &msg);
    return d.bChecked;
}

void SetCheck(HXUIOBJ box, BOOL on)
{
    XUIMessage msg;
    XUIMessageSetCheckState d;
    XuiMessageSetCheckstate(&msg, &d, on);
    if (box)
        XuiSendMessage(box, &msg);
}

int GetRadio(HXUIOBJ group)
{
    XUIMessage msg;
    XUIMessageGetCurSel d;
    d.iItem = -1;
    d.hItem = NULL;
    XuiMessageGetCurSel(&msg, &d);
    if (group)
        XuiSendMessage(group, &msg);
    return d.iItem;
}

void SetRadio(HXUIOBJ group, int item)
{
    XUIMessage msg;
    XUIMessageSetCurSel d;
    XuiMessageSetCurSel(&msg, &d, item);
    if (group)
        XuiSendMessage(group, &msg);
}

bool ReadProfileInts(int user, const DWORD* ids, DWORD n, int* out)
{
    typedef DWORD (*ReadSettingsFn)(DWORD, DWORD, DWORD, const XUID*, DWORD, const DWORD*, DWORD*,
                                    PXUSER_READ_PROFILE_SETTING_RESULT, PXOVERLAPPED);
    if (user < 0 || !p_ReadSettings)
        return false;
    static BYTE buffer[512];
    DWORD size = sizeof(buffer);
    PXUSER_READ_PROFILE_SETTING_RESULT r = (PXUSER_READ_PROFILE_SETTING_RESULT)buffer;
    DWORD hr = ((ReadSettingsFn)p_ReadSettings)(kDashTitle, (DWORD)user, 0, NULL, n, ids, &size, r, NULL);
    if (hr != ERROR_SUCCESS || !r->pSettings)
    {
        Log("1888 Guide: personal settings read: %08X", (unsigned)hr);
        return false;
    }
    for (DWORD i = 0; i < r->dwSettingsLen; i++)
        for (DWORD k = 0; k < n; k++)
            if (r->pSettings[i].dwSettingId == ids[k] && r->pSettings[i].data.type == XUSER_DATA_TYPE_INT32)
                out[k] = (int)r->pSettings[i].data.nData;
    return true;
}

void WriteProfileInt(int user, DWORD id, int value)
{
    typedef DWORD (*WriteSettingsFn)(DWORD, DWORD, DWORD, const XUSER_PROFILE_SETTING*, PXOVERLAPPED);
    if (user < 0 || !p_WriteSettings)
        return;
    XUSER_PROFILE_SETTING s;
    ZeroMemory(&s, sizeof(s));
    s.source = XSOURCE_TITLE;
    s.user.dwUserIndex = (DWORD)user;
    s.dwSettingId = id;
    s.data.type = XUSER_DATA_TYPE_INT32;
    s.data.nData = value;
    DWORD hr = ((WriteSettingsFn)p_WriteSettings)(kDashTitle, (DWORD)user, 1, &s, NULL);
    Log("1888 Guide: setting %08X = %d: %08X", (unsigned)id, value, (unsigned)hr);
}

// The slider's place as a percentage, and back.
int SliderPercent(HXUIOBJ slider)
{
    typedef HRESULT (APIENTRY *RangeFn)(HXUIOBJ, int*, int*);
    int lo = 0, hi = 100, v = 0;
    if (!slider)
        return 0;
    if (p_SliderGetRange)
        ((RangeFn)p_SliderGetRange)(slider, &lo, &hi);
    ((SliderGetFn)p_SliderGetValue)(slider, &v);
    return hi > lo ? (v - lo) * 100 / (hi - lo) : 0;
}

void SetSliderPercent(HXUIOBJ slider, int percent)
{
    typedef HRESULT (APIENTRY *RangeFn)(HXUIOBJ, int*, int*);
    int lo = 0, hi = 100;
    if (!slider)
        return;
    if (p_SliderGetRange)
        ((RangeFn)p_SliderGetRange)(slider, &lo, &hi);
    ((SliderSetFn)p_SliderSetValue)(slider, lo + (hi - lo) * percent / 100);
}

void PersonalLoad(HXUIOBJ scene)
{
    std::wstring id = Id(scene);
    int user = GuideUser();
    if (id == L"scnOptionsVoice")
    {
        const DWORD ids[] = { XPROFILE_OPTION_VOICE_MUTED, XPROFILE_OPTION_VOICE_VOLUME,
                              XPROFILE_OPTION_VOICE_THRU_SPEAKERS };
        int v[3] = { 0, 100, 1 };
        ReadProfileInts(user, ids, 3, v);
        SetCheck(Child(scene, L"chkMute"), v[0] != 0);
        SetSliderPercent(Child(scene, L"sliderVolume"), v[1]);
        SetRadio(Child(scene, L"radgrpOutputLocation"), v[2] < 0 ? 0 : v[2] > 2 ? 2 : v[2]);
    }
    else if (id == L"scnOptionsController")
    {
        const DWORD ids[] = { XPROFILE_OPTION_CONTROLLER_VIBRATION };
        int v[1] = { 3 };
        ReadProfileInts(user, ids, 1, v);
        SetCheck(Child(scene, L"chkVibration"), v[0] != 0);
    }
    else if (id == L"scnNotificationsOnline" && p_NotifyGetOptions)
    {
        typedef void (*GetFn)(BOOL*, BOOL*, BOOL*, BOOL*);
        BOOL show = TRUE, sound = TRUE, a = 0, b = 0;
        ((GetFn)p_NotifyGetOptions)(&show, &sound, &a, &b);
        SetCheck(Child(scene, L"chkShow"), show);
        SetCheck(Child(scene, L"chkSound"), sound);
    }
    else if (id == L"scnOptionsOnline" && p_GetPresence && user >= 0)
    {
        typedef DWORD (*GetFn)(DWORD);
        DWORD s = ((GetFn)p_GetPresence)((DWORD)user);
        int item = (s & 4) ? 3 : (s & 0xF0000) == 0x10000 ? 1 : (s & 0xF0000) == 0x20000 ? 2 : 0;
        SetRadio(Child(scene, L"radgrpOnlineState"), item);
    }
    Log("1888 Guide: %S read", id.c_str());
}

void PersonalSave(HXUIOBJ scene)
{
    LPCWSTR raw = NULL;
    if (FAILED(((GetIdFn)p_GetId)(scene, &raw)) || !raw || !IsPersonalScreen(raw))
        return;                         // (gone: the Guide moved on)
    std::wstring id = raw;
    int user = GuideUser();
    if (id == L"scnOptionsVoice")
    {
        WriteProfileInt(user, XPROFILE_OPTION_VOICE_MUTED, GetCheck(Child(scene, L"chkMute")) ? 1 : 0);
        WriteProfileInt(user, XPROFILE_OPTION_VOICE_VOLUME, SliderPercent(Child(scene, L"sliderVolume")));
        int out = GetRadio(Child(scene, L"radgrpOutputLocation"));
        WriteProfileInt(user, XPROFILE_OPTION_VOICE_THRU_SPEAKERS, out < 0 ? 0 : out > 2 ? 2 : out);
    }
    else if (id == L"scnOptionsController")
        WriteProfileInt(user, XPROFILE_OPTION_CONTROLLER_VIBRATION, GetCheck(Child(scene, L"chkVibration")) ? 3 : 0);
    else if (id == L"scnNotificationsOnline" && p_NotifySetOptions && p_NotifyGetOptions)
    {
        typedef void (*GetFn)(BOOL*, BOOL*, BOOL*, BOOL*);
        typedef void (*SetFn)(BOOL, BOOL, BOOL, BOOL);
        BOOL show = TRUE, sound = TRUE, a = 0, b = 0;
        ((GetFn)p_NotifyGetOptions)(&show, &sound, &a, &b);         // (the other two kept)
        show = GetCheck(Child(scene, L"chkShow"));
        sound = GetCheck(Child(scene, L"chkSound"));
        ((SetFn)p_NotifySetOptions)(show, sound, a, b);
        Log("1888 Guide: notifications show %d sound %d", (int)show, (int)sound);
    }
    else if (id == L"scnOptionsOnline" && p_SetPresence && user >= 0)
    {
        typedef void (*SetFn)(DWORD, DWORD);
        static const DWORD kStates[] = { 1, 0x10001, 0x20001, 5 };
        int item = GetRadio(Child(scene, L"radgrpOnlineState"));
        if (item >= 0 && item < 4)
        {
            ((SetFn)p_SetPresence)((DWORD)user, kStates[item]);
            Log("1888 Guide: online status %d", item);
        }
    }
}

// --- The main menu's mini player (scnMusic: 1888's MiniMediaPlayer) ----------------
// tools/guide1888.py builds it idle (1888's look with nothing playing: "Select
// Music", the five controls greyed). With the console's player playing (Media
// > Music, or a game's), as 1888's hud did (913E9C28): the song on the bar,
// the controls live - play / pause, previous, next, shuffle. XMP from xav.lib,
// as SlopDash calls it.
HXUIOBJ g_playerMenu = NULL;
DWORD g_playerTick = 0;

void PlayerRefresh(HXUIOBJ menu)
{
    HXUIOBJ player = Child(menu, L"scnMusic");
    if (!player)
        return;
    XMP_STATE state = XMP_STATE_IDLE;
    bool on = XMPGetStatus(&state) == ERROR_SUCCESS && state != XMP_STATE_IDLE;
    WCHAR line[96] = L"Select Music";
    if (on)
    {
        XMP_SONGINFO info;
        ZeroMemory(&info, sizeof(info));
        if (XMPGetCurrentSong(&info, NULL) == ERROR_SUCCESS && info.Details.wszTitle[0])
        {
            if (info.Details.wszArtist[0])
                _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"%s - %s", info.Details.wszTitle,
                             info.Details.wszArtist);
            else
                wcscpy_s(line, ARRAYSIZE(line), info.Details.wszTitle);
        }
    }
    static const LPCWSTR kControls[] = { L"btnPlay", L"btnPrev", L"btnNext", L"btnPlaybackMode" };
    HXUIOBJ row = Child(player, L"sceneControlsContainer");    // (MiniMediaPlayer's: the five in it)
    for (int i = 0; i < 4; i++)
        if (HXUIOBJ c = Child(row, kControls[i]))
            XuiControlSetEnable(c, on);
    if (HXUIOBJ bar = Child(player, L"btnPlaylist"))
        XuiControlSetText(bar, line);
    g_playerMenu = menu;
}

// A press on the player's controls: true if it was one.
bool PlayerPress(HXUIOBJ src)
{
    std::wstring id = Id(src);
    bool inPlayer = false;
    for (HXUIOBJ o = src; o && !inPlayer; o = Parent(o))
        inPlayer = Id(o) == L"scnMusic";
    if (!inPlayer)
        return false;
    XMP_STATE state = XMP_STATE_IDLE;
    XMPGetStatus(&state);
    if (id == L"btnPlay")
    {
        if (state == XMP_STATE_PLAYING)
            XMPPause(NULL);
        else if (state == XMP_STATE_PAUSED)
            XMPContinue(NULL);
    }
    else if (id == L"btnPrev")
        XMPPrevious(NULL);
    else if (id == L"btnNext")
        XMPNext(NULL);
    else if (id == L"btnPlaybackMode")
    {
        XMP_PLAYBACKMODE mode = XMP_PLAYBACKMODE_INORDER;
        XMP_REPEATMODE repeat = XMP_REPEATMODE_PLAYLIST;
        DWORD flags = 0;
        XMPGetPlaybackBehavior(&mode, &repeat, &flags);
        XMPSetPlaybackBehavior(mode == XMP_PLAYBACKMODE_SHUFFLE ? XMP_PLAYBACKMODE_INORDER : XMP_PLAYBACKMODE_SHUFFLE,
                               repeat, flags, NULL);
    }
    else
        return false;
    Log("1888 Guide: player %S", id.c_str());
    if (g_playerMenu)
        PlayerRefresh(g_playerMenu);
    return true;
}

void PlayerTick()
{
    if (!g_playerMenu || ++g_playerTick % 45)
        return;
    LPCWSTR raw = NULL;
    if (FAILED(((GetIdFn)p_GetId)(g_playerMenu, &raw)) || !raw || wcscmp(raw, L"sceneMainMenu"))
    {
        g_playerMenu = NULL;
        return;
    }
    PlayerRefresh(g_playerMenu);
}

// A change in one of them: saved a frame later (the per-frame block).
void PersonalChanged(HXUIOBJ src)
{
    for (HXUIOBJ o = src; o; o = Parent(o))
        if (IsPersonalScreen(Id(o)))
        {
            g_personalScene = o;
            g_personalDirty = 2;
            return;
        }
}

void PersonalTick()
{
    if (g_personalDirty && !--g_personalDirty && g_personalScene)
        PersonalSave(g_personalScene);
}

void MainMenuParts(HXUIOBJ menu)
{
    int user = GuideUser();
    bool live = user >= 0 && XUserGetSigninState((DWORD)user) == eXUserSigninState_SignedInToLive;
    ShowChild(menu, L"scnCommunity", live);
    ShowChild(menu, L"btnQuickChat", live);
    ShowChild(menu, L"txtSignInLive", !live);
    ShowChild(menu, L"btnSignInLive", !live);
    LPCWSTR middle = live ? L"btnQuickChat" : L"btnSignInLive";
    SetNav(Child(menu, L"ctlGamerCard"), L"NavDown", live ? L"scnCommunity" : middle);
    SetNav(Child(menu, middle), L"NavUp", live ? L"scnCommunity" : L"ctlGamerCard");
    SetNav(Child(menu, middle), L"NavDown", L"btnOptions");
    SetNav(Child(menu, L"btnOptions"), L"NavUp", middle);
}

// The signed-out main menu's Sign In card: "Looking for profiles" in the file;
// 1888's MainMenuSignedOut code (913E9xxx, its XamProfileCreateEnumerator)
// wrote how many it found, as hud's strings 38 "%d Profiles Found" / 39 "%d
// Profile Found". The number is SlopDash's (game:\Guide\profiles.txt): the
// console's own list call hung from a title (SlopDash 0.0.70).
// (read from the file when the Guide opens, fresh: reread, every focus change
// read the USB stick, and the Guide lagged)
void ProfileCount(HXUIOBJ menu, bool fresh)
{
    HXUIOBJ label = Child(menu, L"labelProfiles");
    if (!label)
        return;
    static int n = -1;
    if (fresh || n < 0)
    {
        HANDLE h = CreateFileA("SlopGuide:\\profiles.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return;
        char text[16] = { 0 };
        DWORD got = 0;
        ReadFile(h, text, sizeof(text) - 1, &got, NULL);
        CloseHandle(h);
        n = atoi(text);
    }
    WCHAR line[48];
    _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, n == 1 ? L"%d Profile Found" : L"%d Profiles Found", n);
    XuiControlSetText(label, line);
}

// The card scene inside the main menu's ctlGamerCard (made by the console's code).
HXUIOBJ MenuCard(HXUIOBJ menu)
{
    HXUIOBJ control = Child(menu, L"ctlGamerCard");
    if (!control)
        return NULL;
    HXUIOBJ card = Child(control, L"gamerCardScene");
    if (card)
        return card;
    // The control's first child scene, whatever the console named it.
    typedef HRESULT (APIENTRY *FirstChildFn)(HXUIOBJ, HXUIOBJ*);
    HXUIOBJ first = NULL;
    if (p_FirstChild)
        ((FirstChildFn)p_FirstChild)(control, &first);
    return first && Child(first, L"onlineBaseScene") ? first : NULL;
}

PFN_MESSAGE_FILTER g_previous = NULL;

volatile LONG g_sysUiOpen = 0;
volatile LONG g_reFull = 0;         // the frame to go Full again (NotifyThread: a wake)
volatile DWORD g_reFullAt = 0;
const DWORD kKeyTimer = 0x52474B59;     // "RGKY"
#ifdef SLOPDASH_LOCAL
#include "../local/guide_keys.inc"
#endif

// Whether obj is in the Guide's frame.
bool InFrame(HXUIOBJ obj)
{
    for (int i = 0; obj && g_frameRoot && i < 24; i++, obj = Parent(obj))
        if (obj == g_frameRoot)
            return true;
    return false;
}

// Whether obj is in the Guide's main screen (1888's MainMenuSignedIn/Out:
// sceneMainMenu).
bool InMainMenu(HXUIOBJ obj)
{
    for (int i = 0; obj && i < 12; i++, obj = Parent(obj))
        if (Id(obj) == L"sceneMainMenu")
            return true;
    return false;
}

// --- message boxes on 1888's error blade -----------------------------------------
// 2.0.1888 showed a message box (the Guide's "Xbox Dashboard" question among
// them) on a blade of its own: the frame's ErrorHUD, a mirrored Guide blade
// that slides in from the right while the Guide's blade closes (named frames
// HalfToError 299-358, ClosedToError 239-268 and back: ErrorToHalf,
// ErrorToClosed), the box in its ErrorHostElement (516 x 770). The box itself
// is 1888's skin visual XuiMessageBox2/3/4 (515 x 770: Title, MessageText in
// edit_Error, Button0-2, legends).
// 17559's xam makes the box (817CE9F0) as an XuiMessageBox whose Visual is
// "XuiMessageBox%d" (or "Xui%sMessageBox%d", %s "" or "Nui": 81694190,
// 81694244), and adds it to the top of the asker's tree, where 17559's own
// visual sits. Here those names become "B8_MessageBox%d" (the same length),
// 1888's visuals under that name in the package's skin (tools/guide1888.py),
// and the box is moved into ErrorHostElement once xam has finished making it.
const DWORD kBoxFormat = 0x815F4A3C;     // L"XuiMessageBox%d"
const DWORD kBoxFormatNui = 0x815F4A0C;  // L"Xui%sMessageBox%d"
const DWORD kHostTimer = 0x52474D42;     // "RGMB"
HXUIOBJ g_box = NULL;                    // the box being hosted (or waiting to be)
HXUIOBJ g_boxHosted = NULL;              // ... once it's on the blade
bool g_boxFromOpen = false;              // the Guide was open when it came

void PatchWide(DWORD at, LPCWSTR text)
{
    for (size_t i = 0; text[i]; i++)
        ((volatile WCHAR*)at)[i] = text[i];
    __dcbst(0, (void*)at);
    __dcbst(0, (void*)(at + 32));
    __sync();
}

// Only when the package has the visuals (else the box would find none).
void UseBladeBoxes()
{
    if (wcsncmp((LPCWSTR)kBoxFormat, L"XuiMessageBox%d", 15) || wcsncmp((LPCWSTR)kBoxFormatNui, L"Xui%sMessageBox%d", 17))
    {
        Log("1888 Guide: message box names not as expected: left");
        return;
    }
    PatchWide(kBoxFormat, L"B8_MessageBox%d");
    PatchWide(kBoxFormatNui, L"B8_%sMessageBox%d");
    Log("1888 Guide: message boxes are 1888's");
}

typedef HRESULT (APIENTRY *UnlinkFn)(HXUIOBJ);
typedef HRESULT (APIENTRY *AddChildFn)(HXUIOBJ, HXUIOBJ);
typedef HRESULT (APIENTRY *SetBoundsFn)(HXUIOBJ, float, float);
typedef HXUIOBJ (APIENTRY *GetUserFocusFn)(BYTE);
typedef HRESULT (APIENTRY *SetUserFocusFn)(HXUIOBJ, BYTE);
typedef HRESULT (APIENTRY *KillTimerFn)(HXUIOBJ, DWORD);

bool Inside(HXUIOBJ obj, HXUIOBJ top)
{
    for (int i = 0; obj && i < 24; i++, obj = Parent(obj))
        if (obj == top)
            return true;
    return false;
}

void SetPosition(HXUIOBJ obj, float x, float y)
{
    DWORD id = 0;
    if (!obj || FAILED(((GetPropIdFn)p_GetPropId)(obj, L"Position", &id)))
        return;
    RawVector v = { XUI_EPT_VECTOR, x, y, 0.0f, { 0, 0 } };
    ((SetPropFn)p_SetProp)(obj, id, 0, &v);
}

void HostBox(HXUIOBJ box)
{
    HXUIOBJ root = FrameRoot();
    HXUIOBJ host = Child(Child(root, L"ErrorHUD"), L"ErrorHostElement");
    // Only the box itself moves, and never into something inside it.
    if (!host || Id(box).compare(0, 13, L"B8_MessageBox") || Inside(host, box) || Inside(root, box))
    {
        Log("1888 Guide: box %S not moved (host %s)", Id(box).c_str(), host ? "inside it" : "missing");
        return;
    }
    if (Parent(box) == host)
        Log("1888 Guide: the box is already in ErrorHostElement");
    // Whoever had focus in the box keeps it after the move.
    HXUIOBJ focus[4] = { NULL, NULL, NULL, NULL };
    for (BYTE u = 0; u < 4; u++)
    {
        HXUIOBJ f = ((GetUserFocusFn)p_GetUserFocus)(u);
        if (Inside(f, box))
            focus[u] = f;
    }
    HXUIOBJ was = Parent(box);
    HRESULT a = S_OK, b = S_OK;
    if (was != host)
    {
        a = ((UnlinkFn)p_Unlink)(box);
        b = ((AddChildFn)p_AddChild)(host, box);
        if (FAILED(b) && was)
        {
            HRESULT back = ((AddChildFn)p_AddChild)(was, box);      // (where it was)
            Log("1888 Guide: box not moved (%08X): put back %08X", (unsigned)b, (unsigned)back);
            return;
        }
    }
    SetPosition(box, 0.0f, 0.0f);
    ((SetBoundsFn)p_SetBounds)(box, 515.0f, 770.0f);
    for (BYTE u = 0; u < 4; u++)
        if (focus[u])
            ((SetUserFocusFn)p_SetUserFocus)(focus[u], u);
    // (The legends' texts, btnA and btnB, are the box scene class's own:
    // 17559's MessageBox, XM_INIT 816936B0.)
    g_boxHosted = box;
    g_boxFromOpen = g_sysUiOpen != 0;
    if (g_boxFromOpen)
        PlayNamed(root, L"HalfToError", L"EndHalfToError");
    else
        PlayNamed(root, L"ClosedToError", L"EndClosedToError");
    Log("1888 Guide: box on the error blade (%08X %08X, Guide %s)", (unsigned)a, (unsigned)b,
        g_boxFromOpen ? "open" : "closed");
}

// The box going: the blade goes back as 1888's did.
void BoxGone()
{
    HXUIOBJ root = FrameRoot();
    if (g_boxFromOpen && g_sysUiOpen)
    {
        PlayNamed(root, L"ErrorToHalf", L"EndErrorToHalf");
        // Over a Full screen (Edit Gamer Profile's error boxes: the screen
        // stayed Full in a Half blade): Full again
        // when that ends (the frame filter, as after a wake).
        if (g_vkFull)
        {
            g_reFullAt = GetTickCount();
            g_reFull = 1;
        }
    }
    else
        PlayNamed(root, L"ErrorToClosed", L"EndErrorToClosed");
    Log("1888 Guide: box closed");
    g_boxHosted = NULL;
    g_box = NULL;
}

// "Xbox Dashboard" from the Guide. 17559's own button class
// (GuideDashCommandNavButton, which the 1888 main screens' btnY is) does it
// all (hud 17559: press 913E63A8 asks with XamShowMessageBox; the answer,
// XM_MSG_RETURN 913E6228, button 0: launch data of 1020 bytes (word 1 the
// button's DashCommand, word 2 the user, 913E9378) to xam export 2105
// (816A1330), which keeps it and calls XamLoaderLaunchTitleEx(NULL, ...)
// unless the dashboard itself is running; then the Guide closes). SlopGuide
// only watches it: what the answer was, and whether the title changed after.
// If after 12 s the same title still runs, the same launch is made again here.
volatile DWORD g_dashUser = 0;         // the controller of the last key
volatile LONG g_dashWatch = 0;

DWORD WINAPI WatchDashboard(LPVOID)
{
    typedef DWORD (*TitleIdFn)();
    typedef DWORD (*SetLaunchDataFn)(PVOID, DWORD);
    typedef DWORD (*LaunchTitleExFn)(LPCSTR, LPCSTR, LPCSTR, DWORD);
    DWORD title = ((TitleIdFn)p_CurrentTitleId)();
    Log("1888 Guide: Yes to the dashboard in title %08X", (unsigned)title);
    FlushLog();
    static const DWORD kAt[] = { 1000, 3000, 6000, 12000, 20000, 30000, 45000, 60000 };
    DWORD waited = 0;
    for (int i = 0; i < ARRAYSIZE(kAt); i++)
    {
        Sleep(kAt[i] - waited);
        waited = kAt[i];
        DWORD now = ((TitleIdFn)p_CurrentTitleId)();
        Log("1888 Guide: %u s later: title %08X", (unsigned)(waited / 1000), (unsigned)now);
        FlushLog();
        if (now != title)
        {
            g_dashWatch = 0;
            return 0;
        }
    }
    // (No second launch: 0.51's hung behind the first, in SetLaunchData.)
    Log("1888 Guide: still in title %08X after 60 s", (unsigned)title);
    FlushLog();
    g_dashWatch = 0;
    return 0;
}

// The console's XUI calls the filter before every message reaches its element,
// and stops there when the filter marks it handled (17559 xam 817B3854). A
// notification reaches the source's parent first, then bubbles up.
HRESULT APIENTRY FilterBody(HXUIOBJ obj, XUIMessage* msg);

// --- Screen Recorder (Personal Settings > Screen Recorder: tools/guide1888.py
// recorder_screen; the recording itself: Recorder.inc) ------------------------------
// The screen's button says what it will do and its status line what's
// happening, kept current while it's up (the frame filter, every 15 frames).
HXUIOBJ g_recScreen = NULL;
DWORD g_recScreenTick = 0;

void RecorderScreen()
{
    LPCWSTR raw = NULL;
    if (FAILED(((GetIdFn)p_GetId)(g_recScreen, &raw)) || !raw ||
        (wcscmp(raw, L"scnRecorder") && wcscmp(raw, L"scnRecorderPanel")))
    {
        g_recScreen = NULL;             // (gone: the Guide moved on)
        return;
    }
    bool on = recorder::Recording();
    bool panel = !wcscmp(raw, L"scnRecorderPanel");
    if (!panel)
    {
        // The settings page: its switch's words.
        if (HXUIOBJ button = Child(g_recScreen, L"btnRecShow"))
            XuiControlSetText(button, RecorderShown() ? L"Show on Guide: On" : L"Show on Guide: Off");
        return;
    }
    // The panel's line: the highlighted button's name,
    // Record's while recording its time; none highlighted, the panel's name,
    // or its time while recording.
    // Stop only while recording (greyed otherwise), and
    // left and right going round it while it's greyed.
    HXUIOBJ row = Child(g_recScreen, L"sceneRecorderButtons");
    if (HXUIOBJ stop = Child(row, L"btnRecStop"))
        XuiControlSetEnable(stop, on);
    SetNav(Child(row, L"btnRecStart"), L"NavRight", on ? L"btnRecStop" : L"btnRecShot");
    SetNav(Child(row, L"btnRecShot"), L"NavLeft", on ? L"btnRecStop" : L"btnRecStart");
    // The line under the tiles: the highlighted one's name; recording, the
    // time on Record (or none highlighted).
    WCHAR line[64];
    DWORD s = on ? (GetTickCount() - recorder::g_recStarted) / 1000 : 0;
    if (LPCWSTR shot = recorder::ShotLine())
        wcscpy_s(line, ARRAYSIZE(line), shot);         // (just after a screenshot)
    else if (recorder::Failed())
        wcscpy_s(line, ARRAYSIZE(line), L"Recording Failed");
    else if (on && (g_recFocus == 0 || g_recFocus < 0))
        _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"Recording %u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
    else
        wcscpy_s(line, ARRAYSIZE(line), g_recFocus == 0 ? L"Record" : g_recFocus == 1 ? L"Stop" :
                                        g_recFocus == 2 ? L"Screenshot" : g_recFocus == 3 ? L"Stream" :
                                        g_recFocus == 4 ? L"Settings" : L"Screen Recorder");
    SetChildText(g_recScreen, L"txtRecordState", line);
}

// The screen come up (its button focused): kept, for RecorderScreen.
bool RecorderSeen(XUINotify* n)
{
    LPCWSTR raw = NULL;
    if (!n->hObjSource || FAILED(((GetIdFn)p_GetId)(n->hObjSource, &raw)) || !raw)
        return false;
    static const LPCWSTR kButtons[] = { L"btnRecStart", L"btnRecStop", L"btnRecShot", L"btnRecStream",
                                        L"btnRecSettings" };
    int focus = -1;
    for (int i = 0; i < ARRAYSIZE(kButtons); i++)
        if (!wcscmp(raw, kButtons[i]))
            focus = i;
    if (focus >= 0 || g_recFocus >= 0)
    {
        // One of the panel's buttons, or away from them: the bar follows.
        g_recFocus = focus;
        if (focus >= 0)
            g_recScreen = Parent(Parent(n->hObjSource));   // (the tiles: in its row)
        if (g_recScreen)
            RecorderScreen();
        return false;
    }
    if (wcscmp(raw, L"btnRecShow"))
        return false;
    g_recScreen = Parent(n->hObjSource);
    g_recScreenTick = 0;
    return false;                       // (the focus goes on as usual)
}

// --- How long the Guide takes --------------------------------------------------------------------
// While the Guide is open, every 2 s: the messages through SlopGuide's
// filter, the time spent in it and its slowest message; and the Guide's frames
// (message 0 to its frame, once a frame), their count and longest gap.
struct GuideTiming
{
    LONGLONG spent, slowest, lastFrame, longestFrame;
    DWORD messages, slowestMsg, frames, since;
};
GuideTiming g_timing = { 0 };
LARGE_INTEGER g_ticksPerSecond = { 0 };

HRESULT APIENTRY Filter(HXUIOBJ obj, XUIMessage* msg)
{
    if (!g_sysUiOpen || !plugins::g_debugTools)     // (the timing: a Debug Tool)
        return FilterBody(obj, msg);
    DWORD m = msg->dwMessage;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    HRESULT hr = FilterBody(obj, msg);
    QueryPerformanceCounter(&b);
    GuideTiming& t = g_timing;
    LONGLONG d = b.QuadPart - a.QuadPart;
    t.spent += d;
    t.messages++;
    if (d > t.slowest)
    {
        t.slowest = d;
        t.slowestMsg = m;
    }
    if (obj && obj == g_frameRoot && m == 0)
    {
        if (t.lastFrame && a.QuadPart - t.lastFrame > t.longestFrame)
            t.longestFrame = a.QuadPart - t.lastFrame;
        t.lastFrame = a.QuadPart;
        t.frames++;
    }
    DWORD now = GetTickCount();
    if (!t.since)
        t.since = now;
    else if (now - t.since >= 2000)
    {
        if (!g_ticksPerSecond.QuadPart)
            QueryPerformanceFrequency(&g_ticksPerSecond);
        double us = 1000000.0 / (double)g_ticksPerSecond.QuadPart;
        Log("Guide timing: %u frames in %u ms (longest gap %.1f ms); filter: %u messages, %.2f ms, slowest %.0f us (msg %u)",
            (unsigned)t.frames, (unsigned)(now - t.since), (double)t.longestFrame * us / 1000.0, (unsigned)t.messages,
            (double)t.spent * us / 1000.0, (double)t.slowest * us, (unsigned)t.slowestMsg);
        LONGLONG last = t.lastFrame;
        ZeroMemory(&t, sizeof(t));
        t.lastFrame = last;
        t.since = now;
    }
    return hr;
}

HRESULT APIENTRY FilterBody(HXUIOBJ obj, XUIMessage* msg)
{
    DWORD m = msg->dwMessage;
#ifdef SLOPDASH_LOCAL
#include "../local/guide_filter.inc"
#endif
    // A message box made (XM_INIT_RESOURCES, xam broadcasting it to the new
    // box: 817CEBA4): its scene, XuiScene1, under 1888's visual (B8_...).
    // 17559's own frame code already puts it on 1888's error blade
    // (ErrorHostElement, in a scene of its own) and plays the blade's
    // transitions (the test of 2026-10-14: a box SlopGuide hadn't touched sat
    // there, the Guide's blade closed). 0.49-0.51 moved the box there itself:
    // out of xam's scene, so xam's clean-up missed it and it stayed on screen
    // under the next one. Nothing is moved now; the box's X and Y legends,
    // which 1888's own box code hid (empty here), are hidden.
    if ((m == XM_INIT_RESOURCES || m == XM_INIT) && obj)
    {
        LPCWSTR raw = NULL;
        if (SUCCEEDED(((GetIdFn)p_GetId)(obj, &raw)) && raw && !wcscmp(raw, L"XuiScene1"))
        {
            std::wstring visual = Id(Parent(obj));
            if (visual.compare(0, 13, L"B8_MessageBox") == 0)
            {
                static const wchar_t* const kUnused[] = { L"btnX", L"btnY" };
                for (int i = 0; i < 2; i++)
                    if (HXUIOBJ b = Child(obj, kUnused[i]))
                        XuiElementSetShow(b, FALSE);
                if (m == XM_INIT_RESOURCES)
                    Log("1888 Guide: message box %S in %S (in %S)", visual.c_str(), Id(Parent(Parent(obj))).c_str(),
                        Id(Parent(Parent(Parent(obj)))).c_str());
            }
            else if (visual.find(L"MessageBox") != std::wstring::npos)
                Log("1888 Guide: a message box not in 1888's style: %S", visual.c_str());
        }
    }
    // Keys and presses in the Guide, with where they land (the exit box's
    // Yes did nothing: no A reached the box).
    if (m == XM_KEYDOWN)
    {
        DWORD k = ((XUIMessageInput*)msg->pvData)->dwKeyCode;
        g_dashUser = ((XUIMessageInput*)msg->pvData)->UserIndex;
        if (k == VK_PAD_A || k == VK_PAD_B || k == VK_PAD_Y || g_box)
            Log("key %04X at %S (in %S)", (unsigned)k, Id(obj).c_str(), Id(Parent(obj)).c_str());
    }
    else if (m == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS &&
             Parent(((XUINotify*)msg->pvData)->hObjSource) == obj)
    {
        std::wstring src = Id(((XUINotify*)msg->pvData)->hObjSource), here = Id(obj);
        Log("press %S (in %S)", src.c_str(), here.c_str());
    }
    else if (m == XM_MSG_RETURN && obj)
    {
        // The box's answer, to whoever asked: on btnY (the dashboard button),
        // 0 is Yes, and 17559's class goes on to the launch (watched).
        int button = ((XUIMessageMessageBoxReturn*)msg->pvData)->nButton;
        std::wstring here = Id(obj);
        Log("box answer %d at %S", button, here.c_str());
        HANDLE t = NULL;
        if (here == L"btnY" && button == 0 && InterlockedCompareExchange(&g_dashWatch, 1, 0) == 0)
        {
            if (NT_OK(StartThread(&t, WatchDashboard, NULL)) && t)
                CloseHandle(t);
            else
                g_dashWatch = 0;
        }
        FlushLog();
    }
    if (m == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kKeyTimer)
    {
        if (Child(obj, L"labAcquiredDate"))
            AchDateFill(obj);                   // (a game's achievements: the focused one's date)
#ifdef SLOPDASH_LOCAL
#include "../local/guide_timer.inc"
#endif
        msg->bHandled = TRUE;               // SlopGuide's own: the Guide never sees it
        return S_OK;
    }
    // B on the main screen closes the Guide. 17559's own main screen
    // (GuideMain.xur: btnB a XuiBackButton in a GuideMainScene) closed it in
    // that class's code, which 1888's screens don't have; its B, only drawn
    // (tools/guide1888.py own_legend), went nowhere (0.33). The Xbox button,
    // as xam takes it, closes the Guide as the button does.
    if (m == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_B && p_XenonButton &&
        InMainMenu(obj))
    {
        typedef DWORD (*XenonButtonFn)(DWORD);
        DWORD r = ((XenonButtonFn)p_XenonButton)(((XUIMessageInput*)msg->pvData)->UserIndex);
        Log("1888 Guide: B on the main screen: closing (%u)", (unsigned)r);
        msg->bHandled = TRUE;
        return S_OK;
    }
    // After a wake (NotifyThread): Full again when the frame's own opening
    // ends, or 1.5 s on if its end came first.
    if (g_reFull && obj && obj == g_frameRoot && g_sysUiOpen && g_vkFull &&
        (m == XM_TIMELINE_END || GetTickCount() - g_reFullAt > 1500))
    {
        g_reFull = 0;
        PlayNamed(obj, L"RG_EndHalfToFull", L"RG_EndHalfToFull");
        Log("1888 Guide: blade Full again (after a wake)");
    }
    // The zone list's side panel: 1888's text (ZonePanel), once a frame.
    if (obj && obj == g_frameRoot && m == 0)
    {
        if (g_zoneFrames)
            ZonePanel();
        if (g_czPending)
            CustomZoneDone();
        // Done pressed (Save Gamer Profile, Edit Gamer Profile): the Guide
        // closes, as the Xbox button closes it (B on the main menu does the
        // same) - only if it's still open, so it never opens it again.
        if (g_closeFrames && !--g_closeFrames && g_sysUiOpen && p_XenonButton)
        {
            typedef DWORD (*XenonButtonFn)(DWORD);
            DWORD r = ((XenonButtonFn)p_XenonButton)((DWORD)max(GuideUser(), 0));
            Log("1888 Guide: Done: Guide closed (%u)", (unsigned)r);
        }
        if (g_zoneCardFrames && (--g_zoneCardFrames == 60 || g_zoneCardFrames == 0) && g_zoneCard)
            CardZoneInto(g_zoneCard);
        if (g_recScreen && ++g_recScreenTick % 15 == 1)
            RecorderScreen();
        BatteryTick();
        PersonalTick();
        PlayerTick();
    }
    // Y on the sign-in screen: Create New Profile, as its legend says (1888's
    // btnY, only drawn: the key went nowhere).
    if (m == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_Y && Id(obj) == L"Scene_Signin")
    {
        Log("1888 Guide: Y on sign-in: create profile");
        OpenCreateProfile();
        msg->bHandled = TRUE;
        return S_OK;
    }
    // What the console tells the Guide's frame (its states: the Guide that
    // stayed "open", 0.29), kept to the log until it's full.
    // (Not message 0, sent every frame: it filled the log, 0.30.)
    if (obj && obj == g_frameRoot && m != 0 && m != XM_TIMER && m != 2039 && m != 15 && m != 20 && m != 2073 &&
        m != 3002 && InterlockedIncrement(&g_frameMsgs) <= 40)
    {
        Log("frame msg %u (%u ms)", (unsigned)m, (unsigned)GetTickCount());
        if (m == XM_TIMELINE_END || m == XM_NOTIFY)
            FlushLog();
    }
    // A on the sign-in list (the key at the list or one of its rows, or the
    // list's press): the selection read that follows is the console's pick.
    if (m == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_A)
    {
        std::wstring id = Id(obj);
        if (id == L"listUser" || Id(Parent(obj)) == L"listUser")
            g_pickArmed = true;
    }
    else if (m == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS &&
             Id(((XUINotify*)msg->pvData)->hObjSource) == L"listUser")
        g_pickArmed = true;
    else if (m == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_SELCHANGED)
    {
        g_pickArmed = false;
        if (ZoneListNotify((XUINotify*)msg->pvData, msg) || TileListNotify((XUINotify*)msg->pvData, msg))
            return S_OK;
    }
    else if (m == XM_NOTIFY && (((XUINotify*)msg->pvData)->dwNotify == XN_PRESS ||
                                ((XUINotify*)msg->pvData)->dwNotify == XN_SET_FOCUS) &&
             (ZoneListNotify((XUINotify*)msg->pvData, msg) || TileListNotify((XUINotify*)msg->pvData, msg)))
        return S_OK;
    // A tick box of Trainer focused: the screen filled from the trainer.
    if (m == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_SET_FOCUS)
    {
        LPCWSTR raw = NULL;
        HXUIOBJ src = ((XUINotify*)msg->pvData)->hObjSource;
        if (src && SUCCEEDED(((GetIdFn)p_GetId)(src, &raw)) && raw && CheatRow(raw) >= 0)
            TrainerScreen(Parent(src));
    }
    // A row of Plugins focused: the screen's rows brought up to date.
    if (m == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_SET_FOCUS)
    {
        LPCWSTR raw = NULL;
        HXUIOBJ src = ((XUINotify*)msg->pvData)->hObjSource;
        if (src && SUCCEEDED(((GetIdFn)p_GetId)(src, &raw)) && raw &&
            (PluginRow(raw) >= 0 || !wcscmp(raw, L"btnDebugTools") || !wcscmp(raw, L"btnAllOff") ||
             !wcscmp(raw, L"btnAllOn")))
            PluginsScreen(Parent(src));
    }
    // A game's achievements focused: its title kept for the details screen.
    if (m == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_SET_FOCUS && Child(obj, L"labGameTitle"))
        AchievementsSeen(obj);
    // Screen Recorder's button focused: its screen is up (RecorderScreen).
    if (m == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_SET_FOCUS)
        RecorderSeen((XUINotify*)msg->pvData);
    if (m == XM_SET_SOURCE_IMAGE)
        ZoneIconSwap(msg);
    if (m == XM_GET_ITEMCOUNT || m == XM_GET_SOURCE_TEXT || m == XM_GET_SOURCE_IMAGE || m == XM_GET_CURSEL)
    {
        // (every list asks these while it draws: the name compared in place,
        // no string made, unless it's one of SlopGuide's lists)
        LPCWSTR raw = NULL;
        if (obj && SUCCEEDED(((GetIdFn)p_GetId)(obj, &raw)) && raw)
        {
            if (!wcscmp(raw, L"listUser"))
            {
                if (HideBadAvatar(obj, msg))
                    return S_OK;
            }
            else if (!wcscmp(raw, L"zoneList"))
            {
                if (ZoneListMsg(obj, msg))
                    return S_OK;
            }
            else if (!wcscmp(raw, L"gamerTileList"))
            {
                if (TileListMsg(obj, msg))
                    return S_OK;
            }
            else if ((m == XM_GET_ITEMCOUNT || m == XM_GET_SOURCE_TEXT) &&
                     (!wcscmp(raw, L"TileListId") || !wcscmp(raw, L"SkinsListId") || !wcscmp(raw, L"StyleListId")))
                Answer(msg, raw);
        }
    }
    else if (m == XM_NOTIFY)
    {
        XUINotify* n = (XUINotify*)msg->pvData;
        HXUIOBJ src = n->hObjSource;
        // Presses come to the source's parent first; selection and value
        // changes bubble from the container's outer (xuimessage.h), so they're
        // taken wherever they're seen (what they do can be done twice).
        bool first = src && Parent(src) == obj;
        if (n->dwNotify == XN_SET_FOCUS && Id(obj) == L"sceneMainMenu" && g_openTrainer)
        {
            g_openTrainer = 0;                  // (Start + Back in a game: straight to Trainer)
            if (HXUIOBJ any = Child(obj, L"btnOptions"))
                Open(any, L"Trainer.xur");
        }
        if (n->dwNotify == XN_SET_FOCUS && Id(obj) == L"sceneMainMenu")
        {
            ProfileCount(obj, false);
            RecorderPanel(obj);
            BatteryIcon(obj);
            PlayerRefresh(obj);
        }
        if (n->dwNotify == XN_SET_FOCUS && Id(obj) == L"sceneMainMenu" && Child(obj, L"ctlGamerCard"))
        {
            MainMenuParts(obj);
            FillCard(MenuCard(obj));
        }
        bool change = src && (n->dwNotify == XN_SELCHANGED || n->dwNotify == XN_VALUE_CHANGED);
        if (src && (change || n->dwNotify == XN_PRESS))
            PersonalChanged(src);           // (Voice, Vibration, Notifications, Online Status)
        if (src && n->dwNotify == XN_PRESS && Id(src) == L"ctlGamerCard" && Id(obj) == L"sceneMainMenu")
        {
            // The Guide's gamer card: your profile. 17559's own code pressed it to no
            // effect; XShowGamerCardUI opens 17559's profile screen
            // (GamerCardScene: View Games, Edit Profile...), in 1888's scenes
            // (GP_GamerCardScene, GP_820/802/828).
            int user = GuideUser();
            XUID xuid = 0;
            DWORD r = user >= 0 && XUserGetXUID((DWORD)user, &xuid) == ERROR_SUCCESS
                          ? XShowGamerCardUI((DWORD)user, xuid) : ERROR_NOT_LOGGED_ON;
            Log("gamer card: player %d: %u", user, (unsigned)r);
            msg->bHandled = TRUE;
            return S_OK;
        }
        if (src && n->dwNotify == XN_PRESS && PlayerPress(src))
        {
            msg->bHandled = TRUE;
            return S_OK;
        }
        if (first || change)
        {
            std::wstring id = Id(src);
            if (first && n->dwNotify == XN_PRESS && (id == L"btnSignIn" || id == L"btnX") &&
                Id(obj) == L"sceneMainMenu")
            {
                OpenSignIn();
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS &&
                     ((id == L"btnNewProfile" && Id(obj) == L"sceneMainMenu") ||
                      (id == L"btnY" && Id(obj) == L"Scene_Signin")))
            {
                OpenCreateProfile();
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS && id == L"doneButton" && Id(obj) == L"congratulationsScene")
            {
                CreateProfileOver("Save Gamer Profile: Done");
                g_closeFrames = 20;             // the Guide closes once the console's done with it
            }
            else if (first && n->dwNotify == XN_PRESS && id == L"doneButton" &&
                     (Id(obj) == L"offlineButtonGroup" || Id(obj) == L"onlineButtonGroup"))
            {
                CreateProfileOver("Edit Gamer Profile: Done");
                // Edit Gamer Profile's Done (tools/guide1888.py add_done): where
                // B goes, the Guide's main menu with the profile - B to the
                // screen's own back button (its PressKey), as the pad's B.
                // Then the Guide closes.
                HXUIOBJ back = Child(Parent(obj), L"backButton");
                if (back)
                {
                    XUIMessage key;
                    XUIMessageInput in = { 0 };
                    XuiMessageInput(&key, &in, XUI_KEYDOWN, VK_PAD_B, 0, 0, (BYTE)max(GuideUser(), 0));
                    XuiSendMessage(back, &key);
                }
                g_closeFrames = 20;
                Log("1888 Guide: Edit Gamer Profile: Done (%s), closing", back ? "back" : "no back button");
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS && id == L"btnRecorder")
            {
                Open(src, L"Recorder.xur");     // Personal Settings > Screen Recorder
                msg->bHandled = TRUE;
            }
            else if (n->dwNotify == XN_PRESS &&
                     (id == L"btnRecStart" || id == L"btnRecStop" || id == L"btnRecStream" || id == L"btnRecShot"))
            {
                if (id == L"btnRecShot")
                    recorder::TakeScreenshot();         // (the screen as the Guide opened)
                else if (id == L"btnRecStart")
                    recorder::Start();          // (already recording: nothing)
                else if (id == L"btnRecStop")
                    recorder::Stop();
                else
                    Open(Parent(obj), L"Streaming.xur");    // (the services to link: Streaming.xur)
                g_recScreen = Parent(obj);      // (the panel: the buttons' row is in it)
                RecorderScreen();
                g_recScreenTick = 0;
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS && id == L"btnRecSettings")
            {
                Open(Parent(obj), L"Recorder.xur");     // (the panel's cog: its settings, over the main menu)
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS &&
                     (id == L"btnStreamTwitch" || id == L"btnStreamYouTube" || id == L"btnStreamKick"))
            {
                SetChildText(obj, L"txtStreamState", L"Coming Soon");   // (linking: later)
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS && CheatRow(id.c_str()) >= 0)
            {
                SetCheck(src, trainer::Flip(CheatRow(id.c_str())));    // (the trainer's own word on it)
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS && id == L"btnPlugins")
            {
                Open(src, L"Plugins.xur");      // Personal Settings > Plugins
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS && id == L"btnAchievements")
            {
                // Personal Settings > Achievements: the
                // console's own achievements for the Guide's player (its
                // gamerprofile code reads the profile live, a game's new
                // unlocks included), in 1888's scenes (SlopGuide.cpp's gp
                // branch: GP_820/802/804/828, tools/guide1888.py).
                // With no game running (the dashboard: SlopDash's title has no
                // achievements, and the list came up empty, 2026-11-05) your
                // profile instead, whose View Games lists every game.
                int user = GuideUser();
                typedef DWORD (*TitleIdFn)();
                DWORD title = p_CurrentTitleId ? ((TitleIdFn)p_CurrentTitleId)() : 0;
                bool game = title && title != 0xFFFE07D1 && title != 0xFFFF07D1;
                XUID xuid = 0;
                DWORD r = ERROR_NOT_LOGGED_ON;
                if (user >= 0 && game)
                    r = XShowAchievementsUI((DWORD)user);
                else if (user >= 0 && XUserGetXUID((DWORD)user, &xuid) == ERROR_SUCCESS)
                    r = XShowGamerCardUI((DWORD)user, xuid);
                Log("achievements: player %d, title %08X (%s): %u", user, (unsigned)title,
                    game ? "the game's" : "the profile", (unsigned)r);
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS &&
                     (PluginRow(id.c_str()) >= 0 || id == L"btnDebugTools" || id == L"btnAllOff" ||
                      id == L"btnAllOn"))
            {
                if (id == L"btnDebugTools")
                    plugins::SwitchDebugTools();
                else if (id == L"btnAllOff" || id == L"btnAllOn")
                    plugins::SwitchAll(id == L"btnAllOn");
                else
                    plugins::Switch(PluginRow(id.c_str()));
                PluginsScreen(Parent(src));
                msg->bHandled = TRUE;
            }
            else if (first && n->dwNotify == XN_PRESS && id == L"btnRecShow")
            {
                SetRecorderShown(!RecorderShown());     // (Personal Settings > Screen Recorder)
                g_recScreen = obj;
                g_recScreenTick = 0;
                msg->bHandled = TRUE;
            }
            else if (n->dwNotify == XN_PRESS && id == L"CustomizeList")
            {
                // hud 913EC6B0: item 0 Dashboard Trim, 1 Guide Background.
                Open(src, XuiListGetCurSel(src, NULL) == 0 ? L"DashStyleSelect.xur" : L"TileSelect.xur");
                msg->bHandled = TRUE;
            }
            else if (n->dwNotify == XN_PRESS && id == L"SkinsListId")
            {
                int i = XuiListGetCurSel(src, NULL);
                if (i == 0)
                    Open(src, L"OptionsPersonalization.xur");
                else if (i >= 1 && i <= 3)
                    SetTheme((DWORD)(i - 1));
                msg->bHandled = TRUE;
            }
            else if (n->dwNotify == XN_PRESS && id == L"StyleListId")
            {
                int i = XuiListGetCurSel(src, NULL);
                if (i >= 0 && i < ARRAYSIZE(kTrims))
                    SetTheme((DWORD)i, false);
                msg->bHandled = TRUE;
            }
            else if (id == L"TileListId" && n->dwNotify == XN_SELCHANGED)
                Preview(src, XuiListGetCurSel(src, NULL));
            else if (id == L"TileListId" && n->dwNotify == XN_PRESS)
            {
                int i = XuiListGetCurSel(src, NULL);
                if (i == 0)
                    Open(src, L"ColorSelect.xur");
                else if (i == 1)
                    StoreBackground(NULL, NULL);
                else if (i - 2 < kTileCount)
                    StoreBackground(kTiles[i - 2].png, NULL);
                msg->bHandled = TRUE;
            }
            else if (n->dwNotify == XN_VALUE_CHANGED && Id(Parent(src)) == L"scnColorSelect")
                FillColour(Child(Parent(src), L"PreviewId"), ColourFromSliders(Parent(src)));
            else if (first && Id(obj) == L"scnColorSelect" && n->dwNotify == XN_PRESS && id == L"btnA")
            {
                DWORD colour = ColourFromSliders(obj);
                StoreBackground(NULL, &colour);
                msg->bHandled = TRUE;
            }
        }
    }
    else if (m == XM_INIT)
    {
        std::wstring id = Id(obj);
        if (id == L"scnSkinSelect")
        {
            // 1888 showed the list and its line once it had the themes (they're
            // hidden in the file, with "Please wait" for downloaded ones).
            HXUIOBJ list = Child(obj, L"SkinsListId"), line = Child(obj, L"txtSelectSkin");
            if (list)
                XuiElementSetShow(list, TRUE);
            if (line)
                XuiElementSetShow(line, TRUE);
        }
        else if (IsPersonalScreen(id))
            PersonalLoad(obj);
        else if (id == L"scnColorSelect")
        {
            // As 913ED258: the sliders from the stored colour, and the preview.
            DWORD colour = StoredColour();
            ((SliderSetFn)p_SliderSetValue)(Child(obj, L"sliderRed"), (colour >> 16) & 0xFF);
            ((SliderSetFn)p_SliderSetValue)(Child(obj, L"sliderGreen"), (colour >> 8) & 0xFF);
            ((SliderSetFn)p_SliderSetValue)(Child(obj, L"sliderBlue"), colour & 0xFF);
            FillColour(Child(obj, L"PreviewId"), colour);
        }
        else if (id == L"sceneMainMenu" || id == L"Scene_Signin")
        {
            PlaceFrame();
            if (id == L"sceneMainMenu")
            {
                ApplyBackground();      // the Guide opening: whoever is signed in now
                ProfileCount(obj, true);
                if (Child(obj, L"ctlGamerCard"))
                {
                    MainMenuParts(obj);
                    FillCard(MenuCard(obj));
                }
            }
        }
    }
    return g_previous ? g_previous(obj, msg) : S_OK;
}

// The BadAvatar exploit's profile is never to be signed in: a sign-in of it,
// anywhere (the Guide, a game, the console's own
// Sign In), is signed straight back out. Only sign-ins after SlopGuide loads:
// it loads after the exploit has done its work. The profile itself is left alone.
void SignOutBadAvatar()
{
    typedef DWORD (*LogonFn)(const XUID*, DWORD, PXOVERLAPPED);
    if (!p_UserLogon)
        return;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
    {
        char name[XUSER_NAME_SIZE + 1] = { 0 };
        if (XUserGetSigninState(i) == eXUserSigninState_NotSignedIn ||
            XUserGetName(i, name, sizeof(name)) != ERROR_SUCCESS || _stricmp(name, "ABadAvatar"))
            continue;
        XUID keep[XUSER_MAX_COUNT];
        for (DWORD j = 0; j < XUSER_MAX_COUNT; j++)
            if (j == i || XUserGetSigninState(j) == eXUserSigninState_NotSignedIn ||
                XUserGetXUID(j, &keep[j]) != ERROR_SUCCESS)
                keep[j] = INVALID_XUID;
        DWORD r = ((LogonFn)p_UserLogon)(keep, 0x23, NULL);
        Log("ABadAvatar signed in at slot %u: signed out (%u)", (unsigned)i, (unsigned)r);
    }
}

#ifdef SLOPDASH_LOCAL
#include "../local/guide_auto.inc"
#endif

DWORD WINAPI NotifyThread(LPVOID)
{
    typedef HANDLE (*CreateListenerFn)(ULONGLONG);
    typedef BOOL (*GetNextFn)(HANDLE, DWORD, PDWORD, PULONG_PTR);
    HANDLE listener = ((CreateListenerFn)p_NotifyCreateListener)(XNOTIFY_SYSTEM);
    Log("system notifications: %s", listener ? "listening" : "FAILED");
    for (;;)
    {
        Sleep(30);
        // The log to the stick, off the Guide's thread (SlopGuide.cpp FlushLog).
        if (InterlockedExchange(&g_flushWanted, 0))
            WriteLogNow();
        DWORD id = 0;
        ULONG_PTR param = 0;
        while (listener && ((GetNextFn)p_NotifyGetNext)(listener, 0, &id, &param))
            if (id == XN_SYS_SIGNINCHANGED)
                SignOutBadAvatar();
            else if (id == XN_SYS_UI)
            {
                // The user's own gamer pictures, listed here as the Guide
                // opens: listed on the Guide's thread as Change Gamer Picture
                // came up, the hard drive held it up 349 ms (the Guide's
                // timing, 2026-10-17).
                // The screen kept first, before the Guide draws over it
                // (the panel's Screenshot tile saves it); let go as it closes.
                if (param && !g_sysUiOpen)
                {
                    typedef DWORD (*TitleIdFn)();
                    recorder::HoldFrame(((TitleIdFn)p_CurrentTitleId)());
                    LoadPics();
                }
                else if (!param && g_sysUiOpen)
                    recorder::ReleaseFrame();
                // An open while already open and Full: the console woke
                // from sleep and the frame replayed its opening, back to
                // Half, with the content still Full.
                // Full again once that's played (the frame filter).
                if (param && g_sysUiOpen && g_vkFull)
                {
                    g_reFullAt = GetTickCount();
                    g_reFull = 1;
                }
                g_sysUiOpen = param ? 1 : 0;
                if (!param)
                {
                    g_vkFull = 0, g_reFull = 0; // (the frame starts at Half again)
                    CreateProfileOver("the Guide closed");
                }
                Log("the Guide (system UI): %s", param ? "open" : "closed");
            }
        // A trainer running (Trainers.inc): its own menu off, and Start + Back
        // opens the Guide on Trainer (the main menu opens it: g_openTrainer).
        if (HANDLE m = trainer::Module())
        {
            trainer::Quieten(m);
            if (!g_sysUiOpen && trainer::ComboPressed() && p_ShowGuide)
            {
                typedef DWORD (*ShowGuideFn)(DWORD);
                g_openTrainer = 1;
                DWORD r = ((ShowGuideFn)p_ShowGuide)(0);
                Log("trainer: Start + Back: the Guide (%u)", (unsigned)r);
            }
        }
#ifdef SLOPDASH_LOCAL
        AutoCommands();
#endif
    }
}

void Install()
{
    if (!Resolve())
        return;
    HANDLE notifyThread = NULL;
    if (NT_OK(StartThread(&notifyThread, NotifyThread, NULL)) && notifyThread)
        CloseHandle(notifyThread);
    g_previous = ((SetMessageFilterFn)p_SetMessageFilter)(Filter);
    Log("1888 Guide behaviour on (filter chained to %p)", g_previous);
    PlaceFrame();
    ApplyBackground();
    UseBladeBoxes();
}

}  // namespace g1888
