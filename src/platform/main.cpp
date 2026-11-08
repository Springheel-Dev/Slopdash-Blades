// SlopDash: first test of the Blades theme.
//
// 1. mounts the HDD (and flash / system-extension partitions, to find fonts),
// 2. reads the user's own Blades dashboard (hdd:\Apps\6717\dash.xex) and
//    extracts its XUI packages into memory (nothing is written anywhere),
// 3. starts XUI and loads the dashboard's skin and main scene from memory.
// Everything learned goes to game:\SlopDash.log: every resource locator the
// scenes ask for, every class XUI doesn't know (a stand-in is used), and XUI's
// own error messages. Back + Start leaves to the system dashboard.

#include <math.h>
#include <xtl.h>
#include <xui.h>
#include <xuiapp.h>
#include <xaudio2.h>
#include <stdio.h>
#include <stdarg.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "core/png.h"
#include "platform/achievements.h"
#include "platform/console.h"
#include "platform/pictures.h"
#include "platform/retroachievements.h"
#include "platform/trainers.h"
#include "platform/movie.h"
#include "platform/storage.h"
#include "platform/log.h"
#include "platform/blade_colours.h"
#include "platform/resources.h"
#include "platform/fetcher.h"
#include "platform/scanner.h"
#include "platform/shotcache.h"
#include "platform/profiles.h"
#include "platform/https.h"
#include "platform/themes.h"
#include "platform/xconfig.h"
#include "theme/blades.h"
#include "theme/blades1888.h"
#include "platform/stand_ins.h"
#include "theme/blades_classes.inc"
#include "theme/blades1888_classes.inc"

// The theme running: Blades 6717 ("blades") or the launch dashboard, 2.0.1888
// ("blades1888"). They share this app; each has its own scenes and classes.
static bool g_launch = false;

#ifdef SLOPDASH_LOCAL
#include "../../local/main_auto.inc"
#endif

// --- kernel exports the XDK headers don't declare ---------------------------
typedef struct { USHORT Length, MaximumLength; PCHAR Buffer; } KSTRING;
extern "C" LONG ObCreateSymbolicLink(KSTRING* link, KSTRING* device);
extern "C" VOID RtlInitAnsiString(KSTRING* dest, const char* src);

static const char* kDashPath = "hdd:\\Apps\\6717\\dash.xex";
// The dashboard's shared images ("sharedres://"), shipped next to its dash.xex.
static const char* kSharedResPath = "hdd:\\Apps\\6717\\shrdres.xzp";
// Blades lays its scenes out on a 1120x770 canvas (dashmain.xur's root);
// the original dashboard scaled that to the TV.
static const float kDesignWidth = 1120.0f, kDesignHeight = 770.0f;

// Changes made after 0.0.5 (the last build that showed Blades), each switchable
// so they can be brought back one at a time. 0.0.6-0.0.10 had all three on and
// showed a black screen or crashed the console.
#pragma warning(disable : 4127)             // "conditional expression is constant": these switches
static const bool kOwnDevice = false;       // our own D3D device, XUI in "shared" mode
static const bool kScaleToScreen = false;   // XuiRenderSetViewTransform to fill the TV
static const bool kPlayBoot = false;        // play the BootGames animation at start
static IXAudio2* g_audio = NULL;            // the title's XAudio2 (XUI's sounds play through it)
static const bool kSilentSounds = false;    // XuiSoundXAudio replaced by a silent stand-in
// 0.0.12: scale the loaded scene element itself (not the view) to fill the screen.
static const bool kScaleScene = true;

// --- log ---------------------------------------------------------------------
// --- files -------------------------------------------------------------------
static bool Mount(const char* drive, const char* device)
{
    char link[32];
    _snprintf_s(link, sizeof(link), _TRUNCATE, "\\??\\%s", drive);
    KSTRING l, d;
    RtlInitAnsiString(&l, link);
    RtlInitAnsiString(&d, device);
    LONG status = ObCreateSymbolicLink(&l, &d);
    Log("mount %s -> %s: %08X", drive, device, (unsigned)status);
    return status >= 0;
}

// Logs every font file in a folder; returns the first one found ("" if none).
static std::string ListFonts(const char* folder)
{
    std::string first;
    char pattern[MAX_PATH];
    _snprintf_s(pattern, sizeof(pattern), _TRUNCATE, "%s*", folder);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        Log("  %s: can't list (%u)", folder, (unsigned)GetLastError());
        return first;
    }
    int count = 0;
    do
    {
        count++;
        const char* dot = strrchr(fd.cFileName, '.');
        if (dot && (!_stricmp(dot, ".xtt") || !_stricmp(dot, ".ttf")))
        {
            Log("  font: %s%s (%u bytes)", folder, fd.cFileName, (unsigned)fd.nFileSizeLow);
            if (first.empty())
                first = std::string(folder) + fd.cFileName;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    Log("  %s: %d entries", folder, count);
    return first;
}

// --- XUI hooks: learn what the scenes need ------------------------------------
class Hooks : public IXUIRuntimeHooks
{
public:
    // The dashboard's own transports and cross-package references (see resources.h).
    virtual HRESULT STDMETHODCALLTYPE RewriteResourceLocator(LPCWSTR szLocator, LPWSTR* pszNewLocator)
    {
        std::wstring result;
        if (!szLocator || !ResolveLocator(szLocator, result))
            return E_NOTIMPL;
        LPWSTR copy = (LPWSTR)XuiAlloc((result.size() + 1) * sizeof(wchar_t));
        if (!copy)
            return E_OUTOFMEMORY;
        wcscpy_s(copy, result.size() + 1, result.c_str());
        *pszNewLocator = copy;
        return S_OK;
    }

    // Called only for classes XUI doesn't know: the dashboard's own code. Each
    // gets a standard stand-in for now; the log is the list of what to write.
    virtual HRESULT STDMETHODCALLTYPE FindClassByName(LPCWSTR szClassName, HXUICLASS* phClass)
    {
        const wchar_t* base = XUI_CLASS_CONTROL;
        if (wcsstr(szClassName, L"Scene") || wcsstr(szClassName, L"Tab") || wcsstr(szClassName, L"MOTD"))
            base = XUI_CLASS_SCENE;
        else if (wcsstr(szClassName, L"List"))
            base = XUI_CLASS_LIST;
        *phClass = XuiFindClass(base);
        Log("missing class %S: using %S", szClassName, base);
        return *phClass ? S_OK : E_FAIL;
    }

    virtual HRESULT STDMETHODCALLTYPE GetTextPresenterText(HXUIOBJ, LPCWSTR*)
    {
        return E_NOTIMPL;
    }

    virtual HRESULT STDMETHODCALLTYPE LogMessage(HRESULT hr, LPCWSTR pszText)
    {
        Log("xui %08X: %S", (unsigned)hr, pszText ? pszText : L"");
        return S_OK;
    }
};

// --- schema dump: XUI's own class definitions --------------------------------
// The .xur format stores properties by position, so decoding the dashboard's
// scenes on the PC needs each class's exact property list. XUI reports it.
static const wchar_t* kDumpClasses[] = {
    L"XuiElement", L"XuiCanvas", L"XuiControl", L"XuiScene", L"XuiTabScene", L"XuiFigure", L"XuiImage",
    L"XuiGroup", L"XuiText", L"XuiLabel", L"XuiButton", L"XuiNavButton", L"XuiBackButton", L"XuiSound",
    L"XuiSoundXAudio", L"XuiSoundXACT", L"XuiList", L"XuiCommonList", L"XuiListItem", L"XuiImagePresenter",
    L"XuiTextPresenter", L"XuiVisual", L"XuiEdit", L"XuiMultilineEdit", L"XuiProgressBar", L"XuiRadioGroup",
    L"XuiRadioButton", L"XuiSlider", L"XuiScrollEnd", L"XuiHtmlPresenter", L"XuiHtmlElement",
    L"XuiHtmlControl", L"XuiCheckbox", L"XuiCaret", L"XuiScrollBar", L"XuiComboBox", L"XuiVideo",
    L"XuiMessageBoxButton", L"XuiGamerCard", L"XuiPanel", L"XuiListChooser", L"XuiNineGrid",
};

static void DumpDefs(FILE* f, const XUIElementPropDef* defs, DWORD count, int depth)
{
    for (DWORD i = 0; i < count; i++)
    {
        const XUIElementPropDef& d = defs[i];
        fprintf(f, "%*s%u\t%S\ttype=%d\tflags=%#x\textra=%u%s\r\n", depth * 2, "", (unsigned)d.dwId,
                d.szPropName ? d.szPropName : L"?", (int)d.Type, (unsigned)d.dwFlags, (unsigned)d.dwExtra,
                d.pCustomHandler ? "\tcustom" : "");
        if (d.pfnGetPropDef && depth < 4)
        {
            DWORD sub = 0;
            const XUIElementPropDef* subDefs = d.pfnGetPropDef(&sub);
            DumpDefs(f, subDefs, sub, depth + 1);
        }
    }
}

static void DumpSchema()
{
    FILE* f = NULL;
    if (fopen_s(&f, "game:\\xui_schema.txt", "w") != 0 || !f)
    {
        Log("can't write game:\\xui_schema.txt");
        return;
    }
    int found = 0;
    for (size_t c = 0; c < ARRAYSIZE(kDumpClasses); c++)
    {
        HXUICLASS h = XuiFindClass(kDumpClasses[c]);
        if (!h)
        {
            fprintf(f, "class %S: not registered\r\n", kDumpClasses[c]);
            continue;
        }
        found++;
        fprintf(f, "class %S", kDumpClasses[c]);
        for (HXUICLASS b = h;;)          // the base-class chain
        {
            HXUICLASS parent = NULL;
            if (FAILED(XuiGetBaseClass(b, &parent)) || !parent)
                break;
            XUIClass info;
            ZeroMemory(&info, sizeof(info));
            info.cbSize = sizeof(info);
            if (FAILED(XuiGetClass(parent, &info)))
                break;
            fprintf(f, " : %S", info.szClassName ? info.szClassName : L"?");
            b = parent;
        }
        fprintf(f, "\r\n");
        const XUIElementPropDef* defs = NULL;
        DWORD count = 0;
        if (SUCCEEDED(XuiClassGetPropDef(h, &defs, &count)))
            DumpDefs(f, defs, count, 1);
    }
    fclose(f);
    Log("schema: %d classes written to game:\\xui_schema.txt", found);
}

// --- the app -----------------------------------------------------------------
bool g_guideUp = false;             // the Guide (system UI) is open: XN_SYS_UI

class App : public CXuiModule
{
protected:
    virtual HRESULT RegisterXuiClasses()
    {
        if (kSilentSounds)
        {
            // XuiSoundXAudio as a stand-in with the same property (File) on the
            // runtime's XuiSound, which plays nothing.
            static const StandInInfo kSilentSound[] = { { L"XuiSoundXAudio", L"XuiSound", 1, { 5 } } };
            int quiet = RegisterStandIns(kSilentSound, 1);
            Log("sounds: silent stand-in %s", quiet ? "registered" : "FAILED");
        }
        else
            Log("XuiSoundXAudioRegister: %08X", (unsigned)XuiSoundXAudioRegister());
        if (g_launch)
        {
            RegisterBlades1888Classes();    // real implementations first; stand-ins skip them
            int n = RegisterStandIns(kBlades1888Classes, ARRAYSIZE(kBlades1888Classes));
            Log("Blades 1888 classes: %d stand-ins registered", n);
            return S_OK;
        }
        RegisterBladesClasses();            // real implementations first; stand-ins skip them
        int n = RegisterStandIns(kBladesClasses, ARRAYSIZE(kBladesClasses));
        Log("Blades classes: %d stand-ins registered", n);
        return S_OK;
    }

    virtual HRESULT UnregisterXuiClasses()
    {
        UnregisterStandIns();
        if (g_launch)
            UnregisterBlades1888Classes();
        else
            UnregisterBladesClasses();
        if (!kSilentSounds)
            XuiSoundXAudioUnregister();
        return S_OK;
    }

public:
    // Scale the design canvas uniformly so it covers the whole back buffer,
    // centred (any overflow is cropped equally from both sides).
    HRESULT FitToScreen(float designW, float designH, float screenW, float screenH)
    {
        float sx = screenW / designW, sy = screenH / designH;
        float s = sx > sy ? sx : sy;
        D3DXMATRIX scale, move, view;
        D3DXMatrixScaling(&scale, s, s, 1.0f);
        D3DXMatrixTranslation(&move, (screenW - designW * s) / 2, (screenH - designH * s) / 2, 0.0f);
        D3DXMatrixMultiply(&view, &scale, &move);
        HRESULT hr = XuiRenderSetViewTransform(m_hDC, &view);
        Log("view: %.0fx%.0f design -> %.0fx%.0f screen, scale %.3f: %08X", designW, designH, screenW, screenH,
            s, (unsigned)hr);
        return hr;
    }

    // Play a named-frame range (e.g. "BootGames" .. "EndBootGames") on obj.
    static HRESULT PlayRange(HXUIOBJ obj, const wchar_t* from, const wchar_t* to, BOOL recurse = TRUE)
    {
        int start = -1, end = -1;
        HRESULT hr = XuiElementFindNamedFrame(obj, from, &start);
        if (SUCCEEDED(hr))
            hr = XuiElementFindNamedFrame(obj, to, &end);
        if (SUCCEEDED(hr))
            hr = XuiElementPlayTimeline(obj, start, start, end, FALSE, recurse);
        Log("play %S (%d) .. %S (%d): %08X", from, start, to, end, (unsigned)hr);
        return hr;
    }

    float m_designW, m_designH, m_screenW, m_screenH;
    bool m_scaled;
    WORD m_prevButtons;

    // Blades (launch): the canvas placed on the screen as 1888's dash.xex itself
    // did (its view transform, dash.xex 920A41E0): x scaled by width / 1120, y by
    // height / 660, moved up 64 pixels per 720 lines - so the canvas's rows from
    // about 59 to 719 fill the screen. A 960-wide target takes the whole canvas
    // (960/1120 by 720/770).
    HRESULT FitLaunchDash(HXUIOBJ layer)
    {
        UINT w = 0, h = 0;
        HRESULT hr = XuiRenderGetBackBufferSize(m_hDC, &w, &h);
        if (FAILED(hr) || !w || !h)
            return hr;
        D3DXVECTOR3 pivot(0, 0, 0), scale, pos(0, 0, 0);
        if (w == 960)
            scale = D3DXVECTOR3(0.857143f, 0.935065f, 1);
        else
        {
            scale = D3DXVECTOR3(w / 1120.0f, h / 660.0f, 1);
            pos.y = -64.0f * h / 720.0f;
        }
        XuiElementSetPivot(layer, &pivot);
        XuiElementSetScale(layer, &scale);
        hr = XuiElementSetPosition(layer, &pos);
        Log("1888 dash fit: %ux%u, scale %.4f x %.4f, y %.1f: %08X", w, h, scale.x, scale.y, pos.y, (unsigned)hr);
        return hr;
    }

    // Scale the scene element uniformly (pivot at its top-left) so the design
    // canvas covers the back buffer, centred; overflow is cropped equally.
    HRESULT ScaleScene(HXUIOBJ scene, float designW, float designH)
    {
        UINT w = 0, h = 0;
        HRESULT hr = XuiRenderGetBackBufferSize(m_hDC, &w, &h);
        if (FAILED(hr) || !w || !h)
        {
            Log("back buffer size: %08X", (unsigned)hr);
            return hr;
        }
        float sx = w / designW, sy = h / designH;
        float s = sx > sy ? sx : sy;
        D3DXVECTOR3 pivot(0, 0, 0), scale(s, s, 1), pos((w - designW * s) / 2, (h - designH * s) / 2, 0);
        XuiElementSetPivot(scene, &pivot);
        XuiElementSetScale(scene, &scale);
        hr = XuiElementSetPosition(scene, &pos);
        XUIClass info;
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        XuiGetClass(XuiGetObjectClass(scene), &info);
        Log("scale %S: %ux%u back buffer, scale %.3f at (%.0f, %.0f): %08X", info.szClassName ? info.szClassName : L"?",
            w, h, s, pos.x, pos.y, (unsigned)hr);
        return hr;
    }

    // Blades' full-screen scenes (187 of them: Games Library, the settings
    // screens...) are laid out on the same 1120 x 770 canvas as dashmain.xur.
    // XUI's navigation adds them next to the scene that opened them, i.e. inside
    // a blade's slot, so they come out shifted by the slot's offset. Any such
    // scene above the focused element gets moved back to the main scene's origin.
    void PlaceFullScreenScenes(HXUIOBJ focus)
    {
        if (!m_root)
            return;
        bool fullScreen = false;
        m_currentFull = NULL;
        int blade = 0;                  // the blade (Tab1..Tab5) the focus is under
        for (HXUIOBJ o = focus; o && o != m_root; )
        {
            {
                HXUIOBJ up = NULL;
                XuiElementGetParent(o, &up);
                LPCWSTR id = NULL;
                if (up == m_root && SUCCEEDED(XuiElementGetId(o, &id)) && id && !wcsncmp(id, L"Tab", 3))
                    blade = _wtoi(id + 3);
            }
            HXUIOBJ parent = NULL;
            XuiElementGetParent(o, &parent);
            float w = 0, h = 0;
            XuiElementGetBounds(o, &w, &h);
            if (w == kDesignWidth && h == kDesignHeight && parent && !m_original.count(o))
            {
                if (!fullScreen)
                    m_currentFull = o;      // the innermost opened screen
                fullScreen = true;
                // The parents' combined offset, up to the main scene.
                D3DXVECTOR3 offset(0, 0, 0);
                bool inside = false;
                for (HXUIOBJ a = parent; a; )
                {
                    if (a == m_root)
                    {
                        inside = true;
                        break;
                    }
                    D3DXVECTOR3 pos;
                    XuiElementGetPosition(a, &pos);
                    offset += pos;
                    HXUIOBJ up = NULL;
                    XuiElementGetParent(a, &up);
                    a = up;
                }
                // Keep the scene's own position from its file (e.g. -1, 0).
                D3DXVECTOR3 now;
                XuiElementGetPosition(o, &now);
                if (!m_ownPos.count(o))
                    m_ownPos[o] = now;
                D3DXVECTOR3 want(m_ownPos[o].x - offset.x, m_ownPos[o].y - offset.y, 0);
                if (inside && (now.x != want.x || now.y != want.y))
                {
                    LPCWSTR id = NULL;
                    XuiElementGetId(o, &id);
                    Log("full-screen scene %S moved from (%.0f, %.0f) to (%.0f, %.0f)", id ? id : L"-", now.x, now.y,
                        want.x, want.y);
                    XuiElementSetPosition(o, &want);
                }
            }
            o = parent;
        }
        if (fullScreen && blade && !m_openBlade)
            OpenBlade(blade);
        else if (!fullScreen && m_openBlade)
            CloseBlade();
        TidyOpenedScenes(focus, false);     // showing only; hiding waits for the focus to settle
    }

    // Opened full-screen screens sit beside the main scene (under the canvas) or
    // inside a blade's tab. One the focus isn't in has been left behind (the
    // Games Library sometimes stayed over the blades after B): hidden. One the
    // focus is in is shown (in case this hid it earlier and XUI came back to it).
    bool Contains(HXUIOBJ scene, HXUIOBJ focus)
    {
        for (HXUIOBJ o = focus; o; XuiElementGetParent(o, &o))
            if (o == scene)
                return true;
        return false;
    }

    // Searched `depth` levels down: the Games Library sits deep inside the Games
    // blade (canvas > main scene > tab > blade > slot > content > it), and 0.0.58
    // only looked two levels down, so it was never found.
    void TidyChildren(HXUIOBJ parent, HXUIOBJ focus, int depth, bool hide)
    {
        for (HXUIOBJ c = NULL; parent && SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(parent, &c)) && c;)
        {
            if (depth > 0)
                TidyChildren(c, focus, depth - 1, hide);
            if (m_original.count(c))
                continue;
            float w = 0, h = 0;
            XuiElementGetBounds(c, &w, &h);
            if (w != kDesignWidth || h != kDesignHeight)
                continue;
            bool in = Contains(c, focus), shown = XuiElementIsShown(c) != FALSE;
            if (in)
                m_hadFocus.insert(c);
            // Only a screen that has had the focus: a new one hasn't been given it yet.
            if (in != shown && m_hadFocus.count(c) && (in || hide))
            {
                LPCWSTR id = NULL;
                XuiElementGetId(c, &id);
                Log("opened screen %S %s", id ? id : L"-", in ? "shown again" : "left behind: hidden");
                XuiElementSetShow(c, in);
            }
        }
    }

    // Hiding only once the focus has settled on the dashboard itself: while XUI
    // goes from one screen back to another, the focus passes outside both for a
    // moment, and hiding then (0.0.62) took the screen being gone back to, so the
    // focus fell out to the blades.
    bool InOpenedScreen(HXUIOBJ focus)
    {
        for (HXUIOBJ o = focus; o; XuiElementGetParent(o, &o))
        {
            float w = 0, h = 0;
            XuiElementGetBounds(o, &w, &h);
            if (!m_original.count(o) && w == kDesignWidth && h == kDesignHeight)
                return true;
        }
        return false;
    }

    void TidyOpenedScenes(HXUIOBJ focus, bool hide)
    {
        if (!focus || (hide && InOpenedScreen(focus)))
            return;
        HXUIOBJ canvas = NULL;
        XuiElementGetParent(m_root, &canvas);
        TidyChildren(canvas, focus, 7, hide);
    }

    // The blade expands around a full-screen scene: dashmain.xur's "<n>Open"
    // animation slides the other blades' tabs away and widens the panel (and
    // plays dash_2ndLevelOpen). A code-filled blade's own header sits outside
    // the content that navigation hides, so it's hidden here.
    void OpenBlade(int blade)
    {
        wchar_t from[16], to[16];
        _snwprintf_s(from, 16, _TRUNCATE, L"%dOpen", blade);
        _snwprintf_s(to, 16, _TRUNCATE, L"%dOpenEnd", blade);
        PlayRange(m_root, from, to, FALSE);    // the tab scene's own timelines only
        m_openBlade = blade;
        // The blade's own header and button legends (A/B/X/Y) sit outside the
        // content that navigation hides; the opened screen has its own.
        m_hidden.clear();
        HideBladeChrome(BladeTab(blade), 2);
        Log("blade %d opened: %u header/legend elements hidden", blade, (unsigned)m_hidden.size());
    }

    // The header and A/B/X/Y legends anywhere in the top two levels of the
    // tab (the tab's first child can be its skin visual, not the blade scene).
    // Opened screens (not in m_original) are left alone.
    void HideBladeChrome(HXUIOBJ parent, int depth)
    {
        for (HXUIOBJ c = NULL; parent && SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(parent, &c)) && c;)
        {
            if (!m_original.count(c))
                continue;
            LPCWSTR id = NULL;
            if (SUCCEEDED(XuiElementGetId(c, &id)) && id &&
                (!wcscmp(id, L"txt_Header") || !wcscmp(id, L"labHeader") || !wcsncmp(id, L"legend_", 7)))
            {
                XuiElementSetShow(c, FALSE);
                m_hidden.push_back(c);
            }
            else if (depth > 1)
                HideBladeChrome(c, depth - 1);
        }
    }

    void CloseBlade()
    {
        wchar_t from[16], to[16];
        _snwprintf_s(from, 16, _TRUNCATE, L"%dClose", m_openBlade);
        _snwprintf_s(to, 16, _TRUNCATE, L"%dCloseEnd", m_openBlade);
        PlayRange(m_root, from, to, FALSE);
        for (size_t i = 0; i < m_hidden.size(); i++)
            XuiElementSetShow(m_hidden[i], TRUE);
        m_hidden.clear();
        m_openBlade = 0;
    }

    // Every element dashmain.xur itself created (its blades are 1120 x 770 too,
    // and must not be taken for opened full-screen scenes).
    void RecordOriginal(HXUIOBJ obj)
    {
        m_original.insert(obj);
        for (HXUIOBJ c = NULL; SUCCEEDED(c ? XuiElementGetNext(c, &c) : XuiElementGetFirstChild(obj, &c)) && c; )
            RecordOriginal(c);
    }

    HXUIOBJ BladeTab(int blade)
    {
        wchar_t tabId[8];
        _snwprintf_s(tabId, 8, _TRUNCATE, L"Tab%d", blade);
        HXUIOBJ tab = NULL;
        XuiElementGetChildById(m_root, tabId, &tab);
        return tab;
    }

    // Frame timing: any frame over 25 ms is logged, split into this step
    // (input, the theme's per-frame work, animation), drawing, and the rest
    // (timers: the themes' own, and the wait for the TV's refresh).
    static double Ms(const LARGE_INTEGER& a, const LARGE_INTEGER& b)
    {
        static LARGE_INTEGER f = {0};
        if (!f.QuadPart)
            QueryPerformanceFrequency(&f);
        return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
    }

    // While the Guide is open (the console draws it in this program's frames,
    // so their length is the Guide's too), every 2 s: the frames, their average
    // and
    // longest, and how much of them was this program's drawing and its step.
    void GuideFrameTiming(const LARGE_INTEGER& now)
    {
        static LARGE_INTEGER last = {0}, since = {0};
        static unsigned frames = 0;
        static double sum = 0, longest = 0, draw = 0, step = 0;
        if (!g_guideUp)
        {
            last.QuadPart = since.QuadPart = 0;
            return;
        }
        if (last.QuadPart)
        {
            double ms = Ms(last, now);
            frames++;
            sum += ms;
            draw += m_drawMs;
            step += m_stepMs;
            if (ms > longest)
                longest = ms;
        }
        else
            since = now;
        last = now;
        if (frames && Ms(since, now) >= 2000.0)
        {
            Log("Guide up: %u frames, %.1f ms average (longest %.1f): drawing %.1f, step %.1f, rest %.1f",
                frames, sum / frames, longest, draw / frames, step / frames, (sum - draw - step) / frames);
            frames = 0;
            sum = longest = draw = step = 0;
            since = now;
        }
    }

    virtual void RunFrame()
    {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        if (m_tFrame.QuadPart)
        {
            double all = Ms(m_tFrame, t0);
            if (all > 25.0 && m_frame > 60)
                Log("slow frame %u: %.1f ms (step %.1f, draw %.1f, rest %.1f)", m_frame, all, m_stepMs, m_drawMs,
                    all - m_stepMs - m_drawMs);
        }
        m_tFrame = t0;
        GuideFrameTiming(t0);
        RunFrameInner();
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        m_stepMs = Ms(t0, t1);
    }

    void RunFrameInner()
    {
        XINPUT_STATE pad;
        ZeroMemory(&pad, sizeof(pad));
        WORD buttons = 0;
        if (XInputGetState(0, &pad) == ERROR_SUCCESS)
            buttons = pad.Gamepad.wButtons;
        WORD pressed = buttons & ~m_prevButtons;
        m_prevButtons = buttons;
#ifdef SLOPDASH_LOCAL
#include "../../local/main_frame.inc"
#endif
        // Exit: click both thumbsticks (Back + Start is the console's screenshot).
        if ((buttons & XINPUT_GAMEPAD_LEFT_THUMB) && (buttons & XINPUT_GAMEPAD_RIGHT_THUMB))
        {
            Log("leaving to the dashboard");
            LogFlush();
            XLaunchNewImage(XLAUNCH_KEYWORD_DASH, 0);
        }
        // B in an opened screen whose B handling was the dashboard's code (e.g.
        // Network Settings): if focus is still in that screen two frames
        // later, nothing went back, so navigate back to its back scene here.
        if ((pressed & XINPUT_GAMEPAD_A) && !g_launch)
            PressSignInCard(XuiElementGetFocus());
        if ((pressed & XINPUT_GAMEPAD_B) && m_currentFull && !g_launch)
        {
            m_pendingBack = m_currentFull;
            m_backFrame = m_frame + 2;
        }
        // Blades (launch): the same idea, its own way (theme/blades1888.cpp).
        if ((pressed & XINPUT_GAMEPAD_B) && g_launch)
        {
            m_launchBackFocus = XuiElementGetFocus();
            m_launchBackFrame = m_frame + 2;
        }
        if (g_launch && m_launchBackFrame && m_frame >= m_launchBackFrame)
        {
            m_launchBackFrame = 0;
            if (XuiElementGetFocus() == m_launchBackFocus)
                Blades1888Back();
        }
        if (m_pendingBack && m_frame >= m_backFrame)
        {
            HXUIOBJ scene = m_pendingBack;
            m_pendingBack = NULL;
            if (scene == m_currentFull && XuiElementGetFocus() == m_lastFocus)
            {
                // The back link is on the scene that was navigated to, which may
                // be an outer scene of the one holding the focus (Network
                // Settings: scene_main inside it).
                HRESULT hr = E_FAIL;
                LPCWSTR id = NULL;
                for (HXUIOBJ o = scene; o && o != m_root; )
                {
                    HXUIOBJ back = XuiSceneGetBackScene(o);
                    if (back)
                    {
                        hr = XuiSceneNavigateBack(o, back, XUSER_INDEX_FOCUS);
                        XuiElementGetId(o, &id);
                        break;
                    }
                    HXUIOBJ up = NULL;
                    XuiElementGetParent(o, &up);
                    o = up;
                }
                Log("B not handled: navigated back from %S: %08X", id ? id : L"(no scene with a back link)", (unsigned)hr);
            }
        }

        // The system's notifications: sign-ins (the "signed in" toast) and
        // storage changes (a disc in or out: the tray looks again).
        static HANDLE notify = NULL;
        static bool trimRestart = false;
        if (!notify)
        {
            notify = XNotifyCreateListener(XNOTIFY_SYSTEM);
            SigninChanged(false);           // who's signed in now, without a toast
        }
        DWORD id = 0;
        ULONG_PTR param = 0;
        while (notify && XNotifyGetNext(notify, 0, &id, &param))
        {
            Log("notification %08X (%08X)", (unsigned)id, (unsigned)param);
            if (id == XN_SYS_UI)
                g_guideUp = param != 0;     // (RunFrame times the frames while it's up)
            if (id == XN_SYS_SIGNINCHANGED)
                SigninChanged(true);
            else if (id == XN_SYS_UI && !param && g_launch)
                RefreshGamerCards1888();    // (the Guide may have changed the profile's card)
            else if (id == XN_SYS_STORAGEDEVICESCHANGED)
            {
                if (!g_launch)
                    RequestDiscCheck();
                m_storageAt = GetTickCount() | 1;
            }
            // A trim chosen in the Guide (1888 hud's broadcast, 0x8000000F) or
            // a profile signed in (and, in case a title doesn't get that
            // broadcast, the Guide closing): a different trim shows after a
            // restart, once the Guide is closed.
            if (g_launch && (id == 0x8000000F || id == XN_SYS_SIGNINCHANGED || (id == XN_SYS_UI && !param)) &&
                TrimChanged())
                trimRestart = true;
        }

        if (trimRestart && !g_guideUp)
        {
            Log("trim changed: restarting to show it");
            RestartForBladeColours();
            trimRestart = false;
        }

        // A drive plugged in or out: once the notifications have stopped for
        // 3 s, and only if the USB drive (the one the library scans besides the
        // hard drive) came or went - the tray sends the same notification -
        // the library is scanned again in the background; then a toast says
        // what's new.
        static bool usbWas = GetFileAttributesA("usb:\\") != (DWORD)-1;
        if (m_storageAt && GetTickCount() - m_storageAt > 3000)
        {
            m_storageAt = 0;
            bool usb = GetFileAttributesA("usb:\\") != (DWORD)-1;
            Log("storage changed: USB %s", usb == usbWas ? "as it was" : usb ? "plugged in" : "removed");
            if (usb != usbWas && StartBackgroundScan())
                usbWas = usb;
        }
        int added = 0, removed = 0;
        if (TakeBackgroundScan(added, removed) && added > 0)
        {
            wchar_t b[64];
            _snwprintf_s(b, 64, _TRUNCATE, added == 1 ? L"%d New Game Found" : L"%d New Games Found", added);
            console::Notify(b, false);
        }
        // The fetcher found official names: the library shows them now (its
        // cache read again, the list sorted by them).
        if (!BackgroundScanBusy() && TakeLearnedNames())
        {
            StopFetcher();
            StartFetcher();
            MarkLibraryReplaced();
        }

        // The hard drive's space: 10 s in, then every 10 minutes (a toast once
        // it is almost full).
        if (m_frame % 36000 == 600)
            console::CheckSpace();

        // Focus changes: log them, and place any full-screen scene the focus is in.
        HXUIOBJ focus = XuiElementGetFocus();
        if (focus != m_lastFocus)
        {
            m_lastFocus = focus;
            m_focusFrame = m_frame;
            if (!g_launch)                  // 1888: its own DashMainScene (theme/blades1888.cpp)
                PlaceFullScreenScenes(focus);
            LPCWSTR id = NULL;
            XUIClass info;
            ZeroMemory(&info, sizeof(info));
            info.cbSize = sizeof(info);
            if (focus)
            {
                XuiElementGetId(focus, &id);
                XuiGetClass(XuiGetObjectClass(focus), &info);
            }
            Log("focus: %S (%S) at %u", id ? id : L"-", info.szClassName ? info.szClassName : L"-",
                (unsigned)GetTickCount());
        }

        // The focus has stayed put for about 0.75 s: any screen left behind goes.
        if (m_frame == m_focusFrame + 45 && !g_launch)
            TidyOpenedScenes(focus, true);

        // Heartbeat: every step of the first frames, then a line a second, so the
        // log shows how far a run got if the console crashes.
        m_frame++;
        bool detail = m_frame <= 3;
        if (detail)
            Log("frame %u: input/animation", m_frame);
        CXuiModule::RunFrame();
        DWORD now = GetTickCount();
        // A signed-out profile changed on disk: read again, so its saved copy
        // (shown while it's signed in) is current. Every 5 s.
        static DWORD profileCheck = 0;
        if (now - profileCheck >= 5000)
        {
            profileCheck = now;
            if (ProfilesChangedOnDisk())
                ScanProfiles();
        }
        if (detail || now - m_lastBeat >= 1000)
        {
            Log("frame %u: alive, %u s", m_frame, (unsigned)((now - m_started) / 1000));
            if (now - m_started >= 30000 && now - m_started < 32000)
                SoundsSettled();             // (the start went well: resources.cpp)
            // Sounds: are XUI's voices made and playing? (changes only)
            if (g_audio)
            {
                XAUDIO2_PERFORMANCE_DATA perf;
                ZeroMemory(&perf, sizeof(perf));
                g_audio->GetPerformanceData(&perf);
                static UINT32 lastTotal = (UINT32)-1, lastGlitch = (UINT32)-1;
                if (perf.TotalSourceVoiceCount != lastTotal || perf.GlitchesSinceEngineStarted != lastGlitch ||
                    perf.ActiveSourceVoiceCount)
                    Log("audio: %u voices (%u playing, %u XMA), %u glitches, latency %u", perf.TotalSourceVoiceCount,
                        perf.ActiveSourceVoiceCount, perf.ActiveXmaSourceVoices, perf.GlitchesSinceEngineStarted,
                        perf.CurrentLatencyInSamples);
                lastTotal = perf.TotalSourceVoiceCount;
                lastGlitch = perf.GlitchesSinceEngineStarted;
            }
            m_lastBeat = now;
        }
    }

    virtual HRESULT Render()
    {
        bool detail = m_frame <= 3;
        if (detail)
            Log("frame %u: render", m_frame);
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        HRESULT hr = S_OK;
        if (movie::Active())
        {
            // A video (Media > Videos): XUI's frame (its black screen), the
            // video's frame over it, then the present (CXuiModule::Render's
            // own steps, with the video between). Before XuiRenderEnd: drawn
            // after it, the frame never reached the screen (it's resolved
            // out of the GPU's tile memory there).
            hr = XuiRenderBegin(m_hDC, D3DCOLOR_ARGB(255, 0, 0, 0));
            if (SUCCEEDED(hr))
            {
                XUIMessage msg;
                XUIMessageRender msgRender;
                XuiMessageRender(&msg, &msgRender, m_hDC, 0xffffffff, XUI_BLEND_NORMAL);
                XuiSendMessage(m_hObjRoot, &msg);
                movie::Draw();
                XuiRenderEnd(m_hDC);
                XuiRenderPresent(m_hDC, NULL, NULL, NULL);
            }
        }
        else
            hr = CXuiModule::Render();
        QueryPerformanceCounter(&t1);
        m_drawMs = Ms(t0, t1);
        if (detail)
            Log("frame %u: rendered %08X", m_frame, (unsigned)hr);
        return hr;
    }

    DWORD m_frame, m_started, m_lastBeat;
    LARGE_INTEGER m_tFrame;
    double m_stepMs, m_drawMs;
    DWORD m_focusFrame;                 // when the focus last moved
    HXUIOBJ m_lastFocus;
    HXUIOBJ m_root;                 // dashmain.xur's tab scene (DashMainScene)
    std::set<HXUIOBJ> m_original;
    std::set<HXUIOBJ> m_hadFocus;               // opened screens the focus has been in
    std::map<HXUIOBJ, D3DXVECTOR3> m_ownPos;   // opened scenes' positions as loaded
    int m_openBlade;
    HXUIOBJ m_currentFull, m_pendingBack;
    DWORD m_backFrame;                // the blade expanded around a full-screen scene, or 0
    DWORD m_launchBackFrame;          // Blades (launch): B to check, or 0
    DWORD m_storageAt;                // the last drive notification (its tick, or 0)
    HXUIOBJ m_launchBackFocus;
    std::vector<HXUIOBJ> m_hidden;  // blade elements hidden while a screen is open
};

static Hooks g_hooks;
static App g_app;

// Some image requests reach the texture loader with the dashboard's own
// "sharedres://" / "common://" locators, without passing through the hook,
// so redirect them here too.
// Images: the dashboard's PNGs are mostly palette-based, which the XDK's D3DX
// can't read (D3DXERR_INVALIDDATA), so they're decoded here (core/png) into a
// linear ARGB texture. Anything else goes to D3DX.
static unsigned g_imagesDecoded = 0, g_imagesFailed = 0;

static HRESULT PngTexture(IXuiDevice* device, const unsigned char* data, unsigned size, XUIImageInfo* info,
                          IDirect3DTexture9** tex)
{
    png::Image img;
    std::string err;
    if (!png::Decode(data, size, img, err))
        return E_FAIL;
    IDirect3DTexture9* t = NULL;
    HRESULT hr = device->CreateTexture(img.width, img.height, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL);
    if (FAILED(hr))
        return hr;
    D3DLOCKED_RECT lr;
    hr = device->LockTextureRect(t, 0, &lr, NULL, 0);
    if (FAILED(hr))
    {
        t->Release();
        return hr;
    }
    for (unsigned y = 0; y < img.height; y++)
    {
        const unsigned char* src = &img.rgba[(size_t)y * img.width * 4];
        unsigned char* dst = (unsigned char*)lr.pBits + (size_t)y * lr.Pitch;
        for (unsigned x = 0; x < img.width; x++, src += 4, dst += 4)
        {
            dst[0] = src[3];        // A8R8G8B8 is stored A, R, G, B on the big-endian console
            dst[1] = src[0];
            dst[2] = src[1];
            dst[3] = src[2];
        }
    }
    device->UnlockTextureRect(t, 0);
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->Width = img.width;
        info->Height = img.height;
        info->Depth = 1;
        info->MipLevels = 1;
        info->ImageFileFormat = D3DXIFF_PNG;
        info->Format = D3DFMT_LIN_A8R8G8B8;
        info->ResourceType = D3DRTYPE_TEXTURE;
    }
    *tex = t;
    return S_OK;
}

// A gamer picture (64x64, 32-bit ARGB: profiles.h) as a texture.
static HRESULT GamerPictureTexture(IXuiDevice* device, const wchar_t* name, XUIImageInfo* info, IDirect3DTexture9** tex)
{
    std::vector<BYTE> pixels;
    if (!GamerPicPixels(name, pixels))
        return E_FAIL;
    const UINT kSize = kGamerPicSize;
    IDirect3DTexture9* t = NULL;
    HRESULT hr = device->CreateTexture(kSize, kSize, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL);
    if (FAILED(hr))
        return hr;
    D3DLOCKED_RECT lr;
    hr = device->LockTextureRect(t, 0, &lr, NULL, 0);
    if (FAILED(hr))
    {
        t->Release();
        return hr;
    }
    for (UINT y = 0; y < kSize; y++)
        memcpy((BYTE*)lr.pBits + y * lr.Pitch, &pixels[y * kSize * 4], kSize * 4);
    device->UnlockTextureRect(t, 0);
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->Width = info->Height = kSize;
        info->Depth = info->MipLevels = 1;
        info->Format = D3DFMT_LIN_A8R8G8B8;
        info->ResourceType = D3DRTYPE_TEXTURE;
    }
    *tex = t;
    return S_OK;
}

// "slopdash://solid/AARRGGBB": one colour, 4 x 4, for flat boxes and edges drawn
// as stretched images (a XuiFigure made in code crashed in XuiFigureSetFill:
// its fill isn't there until a scene file gives it one, 2026-11-02).
// "slopdash://hfade/AARRGGBB": the same, fading in from the left end and out to
// the right (a quarter each), 64 x 4: Launch's row tags.
static HRESULT SolidTexture(IXuiDevice* device, const wchar_t* argb, XUIImageInfo* info, IDirect3DTexture9** tex,
                            bool fade = false)
{
    DWORD c = wcstoul(argb, NULL, 16);
    const UINT kSize = fade ? 64 : 4, kHigh = 4, kRamp = 16;
    IDirect3DTexture9* t = NULL;
    HRESULT hr = device->CreateTexture(kSize, kHigh, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL);
    if (FAILED(hr))
        return hr;
    D3DLOCKED_RECT lr;
    hr = device->LockTextureRect(t, 0, &lr, NULL, 0);
    if (FAILED(hr))
    {
        t->Release();
        return hr;
    }
    for (UINT y = 0; y < kHigh; y++)
        for (UINT x = 0; x < kSize; x++)
        {
            DWORD a = c >> 24;
            if (fade)
            {
                UINT edge = x < kSize - 1 - x ? x : kSize - 1 - x;
                if (edge < kRamp)
                    a = a * edge / kRamp;
            }
            *(DWORD*)((BYTE*)lr.pBits + y * lr.Pitch + x * 4) = (a << 24) | (c & 0xFFFFFF);  // (A, R, G, B: big-endian)
        }
    device->UnlockTextureRect(t, 0);
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->Width = kSize;
        info->Height = kHigh;
        info->Depth = info->MipLevels = 1;
        info->Format = D3DFMT_LIN_A8R8G8B8;
        info->ResourceType = D3DRTYPE_TEXTURE;
    }
    *tex = t;
    return S_OK;
}

// "slopdash://crop/X,Y,W,H,WHITE/<image>": part of a PNG (a dashboard file's,
// through its locator), and with WHITE 1 its colours made white (its alpha
// kept). Launch's header logo and row tags are dashcomm xboxLogo.png's orb and
// wordmark, side by side and alone (1888 has no flat logo), 2026-11-02.
static HRESULT CropTexture(IXuiDevice* device, const wchar_t* spec, XUIImageInfo* info, IDirect3DTexture9** tex)
{
    unsigned x = 0, y = 0, w = 0, h = 0, white = 0;
    const wchar_t* inner = wcschr(spec, L'/');
    if (!inner || swscanf_s(spec, L"%u,%u,%u,%u,%u", &x, &y, &w, &h, &white) != 5 || !w || !h)
        return E_INVALIDARG;
    inner++;
    std::wstring resolved;
    const wchar_t* path = ResolveLocator(inner, resolved) ? resolved.c_str() : inner;
    const unsigned char* data = NULL;
    unsigned size = 0;
    png::Image img;
    std::string err;
    if (!LocatorBytes(path, &data, &size) || !png::Decode(data, size, img, err) || x + w > img.width ||
        y + h > img.height)
        return E_FAIL;
    IDirect3DTexture9* t = NULL;
    HRESULT hr = device->CreateTexture(w, h, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL);
    if (FAILED(hr))
        return hr;
    D3DLOCKED_RECT lr;
    hr = device->LockTextureRect(t, 0, &lr, NULL, 0);
    if (FAILED(hr))
    {
        t->Release();
        return hr;
    }
    for (unsigned row = 0; row < h; row++)
    {
        const unsigned char* src = &img.rgba[((size_t)(y + row) * img.width + x) * 4];
        unsigned char* dst = (unsigned char*)lr.pBits + (size_t)row * lr.Pitch;
        for (unsigned col = 0; col < w; col++, src += 4, dst += 4)
        {
            dst[0] = src[3];        // (A, R, G, B: PngTexture's order)
            dst[1] = white ? 0xFF : src[0];
            dst[2] = white ? 0xFF : src[1];
            dst[3] = white ? 0xFF : src[2];
        }
    }
    device->UnlockTextureRect(t, 0);
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->Width = w;
        info->Height = h;
        info->Depth = info->MipLevels = 1;
        info->Format = D3DFMT_LIN_A8R8G8B8;
        info->ResourceType = D3DRTYPE_TEXTURE;
    }
    *tex = t;
    return S_OK;
}

// "slopdash://shape/K,AARRGGBB,H,V": one colour in a shape of alpha, flipped
// left-right with H 1 and top-bottom with V 1. K: 'v' a line fading from full at
// its top to nothing at its foot (4 x 64); 's' a shadow, that fade times one
// from nothing at its left to full at its right (16 x 64); 'c' a corner, a
// quarter of a ring 0.42 of its radius thick, turning from the top edge down
// into the right one (16 x 16); 't' a triangle pointing down (16 x 10): the
// list's more-below arrow (flipped, more above). Launch's list body: the tab's corners curving
// into its side edges, which fade away down the list, a shadow left of it.
static HRESULT ShapeTexture(IXuiDevice* device, const wchar_t* spec, XUIImageInfo* info, IDirect3DTexture9** tex)
{
    wchar_t kind = 0;
    unsigned argb = 0, hflip = 0, vflip = 0;
    if (swscanf_s(spec, L"%c,%x,%u,%u", &kind, 1, &argb, &hflip, &vflip) != 4)
        return E_INVALIDARG;
    const UINT w = kind == L'v' ? 4 : 16, h = kind == L'c' ? 16 : kind == L't' ? 10 : 64;
    IDirect3DTexture9* t = NULL;
    HRESULT hr = device->CreateTexture(w, h, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_MANAGED, &t, NULL);
    if (FAILED(hr))
        return hr;
    D3DLOCKED_RECT lr;
    hr = device->LockTextureRect(t, 0, &lr, NULL, 0);
    if (FAILED(hr))
    {
        t->Release();
        return hr;
    }
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++)
        {
            float sx = (float)(hflip ? w - 1 - x : x), sy = (float)(vflip ? h - 1 - y : y), a = 0;
            if (kind == L'v')
                a = 1.0f - sy / (h - 1);
            else if (kind == L's')
                a = (sx / (w - 1)) * (sx / (w - 1)) * (1.0f - sy / (h - 1));
            else if (kind == L'c')
            {
                // the ring round the square's bottom left corner, radius w
                float dx = sx + 0.5f, dy = h - (sy + 0.5f), d = sqrtf(dx * dx + dy * dy);
                float outer = (float)w - d + 0.5f, inner = d - (float)w * 0.58f + 0.5f;
                outer = outer < 0 ? 0 : outer > 1 ? 1 : outer;
                inner = inner < 0 ? 0 : inner > 1 ? 1 : inner;
                a = outer * inner;
            }
            else if (kind == L't')
            {
                float half = (w / 2.0f) * (1.0f - (sy + 0.5f) / h), off = sx + 0.5f - w / 2.0f;
                a = half - (off < 0 ? -off : off) + 0.5f;
                a = a < 0 ? 0 : a > 1 ? 1 : a;
            }
            DWORD alpha = (DWORD)((argb >> 24) * a + 0.5f);
            *(DWORD*)((BYTE*)lr.pBits + y * lr.Pitch + x * 4) = (alpha << 24) | (argb & 0xFFFFFF);
        }
    device->UnlockTextureRect(t, 0);
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->Width = w;
        info->Height = h;
        info->Depth = info->MipLevels = 1;
        info->Format = D3DFMT_LIN_A8R8G8B8;
        info->ResourceType = D3DRTYPE_TEXTURE;
    }
    *tex = t;
    return S_OK;
}

static HRESULT APIENTRY TextureLoader(IXuiDevice* device, LPCWSTR name, XUIImageInfo* info, IDirect3DTexture9** tex)
{
    if (!name)
        return E_INVALIDARG;
    // A screenshot Launch's prefetch has made already (shotcache.h): at once.
    PrefetchSetDevice(device->GetD3DDevice());
    UINT pw = 0, ph = 0;
    bool prefetched = PrefetchedTexture(name, tex, &pw, &ph);
    static int shotsLogged = 0;
    if (wcsstr(name, L"shot.jpg") && ++shotsLogged <= 6)
        Log("prefetch: %S %s", name, prefetched ? "made already" : "not made: loading here");
    if (prefetched)
    {
        if (info)
        {
            ZeroMemory(info, sizeof(*info));
            info->Width = pw;
            info->Height = ph;
            info->Depth = info->MipLevels = 1;
            info->Format = D3DFMT_DXT1;
            info->ResourceType = D3DRTYPE_TEXTURE;
        }
        return S_OK;
    }
    static const wchar_t kGamerpic[] = L"slopdash://gamerpic/", kIcon[] = L"slopdash://icon/";
    static const wchar_t kSolid[] = L"slopdash://solid/";
    if (!wcsncmp(name, kGamerpic, ARRAYSIZE(kGamerpic) - 1))
        return GamerPictureTexture(device, name, info, tex);
    if (!wcsncmp(name, kSolid, ARRAYSIZE(kSolid) - 1))
        return SolidTexture(device, name + ARRAYSIZE(kSolid) - 1, info, tex);
    static const wchar_t kFade[] = L"slopdash://hfade/";
    if (!wcsncmp(name, kFade, ARRAYSIZE(kFade) - 1))
        return SolidTexture(device, name + ARRAYSIZE(kFade) - 1, info, tex, true);
    static const wchar_t kShape[] = L"slopdash://shape/";
    if (!wcsncmp(name, kShape, ARRAYSIZE(kShape) - 1))
        return ShapeTexture(device, name + ARRAYSIZE(kShape) - 1, info, tex);
    static const wchar_t kCrop[] = L"slopdash://crop/";
    if (!wcsncmp(name, kCrop, ARRAYSIZE(kCrop) - 1))
        return CropTexture(device, name + ARRAYSIZE(kCrop) - 1, info, tex);
    if (pictures::IsLocator(name))
        return pictures::Texture(device, name, info, tex);     // (no bigger than shown)
    if (IsWrapCoverLocator(name))
        return WrapCoverTexture(device, name, info, tex);       // (a case wrap's front, cropped once)
    std::wstring resolved;
    const wchar_t* path = ResolveLocator(name, resolved) ? resolved.c_str() : name;
    const unsigned char* data = NULL;
    unsigned size = 0;
    bool isIcon = !wcsncmp(name, kIcon, ARRAYSIZE(kIcon) - 1);
    if ((LocatorBytes(path, &data, &size) || IconPng(name, &data, &size) || ProfilePng(name, &data, &size) ||
         storage::ContentPng(name, &data, &size) ||
         AchievementPng(name, &data, &size)) && png::IsPng(data, size))
    {
        HRESULT hr = PngTexture(device, data, size, info, tex);
        if (SUCCEEDED(hr))
        {
            if (++g_imagesDecoded <= 3 || g_imagesDecoded % 50 == 0)
                Log("images: %u decoded (latest %S)", g_imagesDecoded, name);
            return hr;
        }
        if (!isIcon)
            Log("image %S: PNG decode failed %08X", name, (unsigned)hr);
    }
    if (isIcon)
    {
        // The game's own icon is missing or damaged: the one fetched for it, if any.
        std::vector<unsigned char> fetched;
        // (the number right after the prefix: name + 17 skipped its first
        // digit, so games without an icon of their own showed another's)
        size_t game = (size_t)_wtoi(name + ARRAYSIZE(kIcon) - 1);
        if (FetchedIcon(game, fetched) && !fetched.empty())
            return PngTexture(device, &fetched[0], (unsigned)fetched.size(), info, tex);
        std::wstring jpg = FetchedIconFile(game);       // (XboxUnity's, for homebrew)
        if (!jpg.empty())
            return XuiD3DXTextureLoader(device, jpg.c_str(), info, tex);
        const std::vector<library::Game>& games = Games();
        if (game < games.size() && !games[game].art.empty())
        {
            // A picture found beside it (scanner.cpp PictureBeside), a JPEG.
            std::string p = games[game].art;
            std::replace(p.begin(), p.end(), '\\', '/');
            HRESULT hr = XuiD3DXTextureLoader(device, (L"file://" + std::wstring(p.begin(), p.end())).c_str(), info, tex);
            if (SUCCEEDED(hr))
                return hr;
        }
        // Nothing at all: 1888's own games icon (the folder and controller,
        // dashcomm's ico_64x_games.png; an arcade game, the arcade blade's
        // joystick one, GameImg.png).
        bool arcade = game < games.size() && games[game].kind == library::kArcade;
        std::wstring base;
        if (PackageLocator(arcade ? L"arcade" : L"dashcomm", base) &&
            LocatorBytes((base + (arcade ? L"GameImg.png" : L"ico_64x_games.png")).c_str(), &data, &size) &&
            png::IsPng(data, size))
            return PngTexture(device, data, size, info, tex);
        return E_FAIL;
    }
    HRESULT hr = XuiD3DXTextureLoader(device, path, info, tex);
    if (FAILED(hr) && ++g_imagesFailed <= 20)
        Log("image %S: %08X", name, (unsigned)hr);
    return hr;
}

// Logs a crash (its code, address and thread) before the title goes down; the
// build's SlopDash.map turns the address into a function.
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* e)
{
    // The first few only: a fault in the filter's caller repeats it many times.
    static LONG count = 0;
    if (InterlockedIncrement(&count) > 3)
        return EXCEPTION_CONTINUE_SEARCH;
    if (e && e->ExceptionRecord)
        Log("CRASH: exception %08X at %08X (thread %u, data %08X %08X)", (unsigned)e->ExceptionRecord->ExceptionCode,
            (unsigned)(ULONG_PTR)e->ExceptionRecord->ExceptionAddress, (unsigned)GetCurrentThreadId(),
            e->ExceptionRecord->NumberParameters > 0 ? (unsigned)e->ExceptionRecord->ExceptionInformation[0] : 0,
            e->ExceptionRecord->NumberParameters > 1 ? (unsigned)e->ExceptionRecord->ExceptionInformation[1] : 0);
    if (e && e->ContextRecord)
    {
        // Who called it: the link register, then each frame's saved one (the
        // back chain at 0(r1), the caller's return address 8 below the next
        // frame), while the chain stays on the stack (0x7xxxxxxx).
        char chain[160];
        int n = _snprintf_s(chain, sizeof(chain), _TRUNCATE, "CRASH: lr %08X calls", (unsigned)e->ContextRecord->Lr);
        DWORD sp = (DWORD)e->ContextRecord->Gpr1;
        for (int i = 0; i < 8 && n > 0 && n < (int)sizeof(chain) - 10; i++)
        {
            if (sp < 0x70000000 || sp >= 0x80000000 || (sp & 3))
                break;
            sp = *(const DWORD*)sp;
            if (sp < 0x70000000 || sp >= 0x80000000 || (sp & 3))
                break;
            n += _snprintf_s(chain + n, sizeof(chain) - n, _TRUNCATE, " %08X", *(const DWORD*)(sp - 8));
        }
        Log("%s", chain);
    }
    LogFlush();
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool FileThere(const char* path)
{
    DWORD a = GetFileAttributesA(path);
    return a != (DWORD)-1 && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// The theme's files aren't there (a stick the SlopDash Importer hasn't filled,
// or a file missing): the console's own message box says so over a plain
// screen, then the Xbox dashboard.
static int MissingTheme(const char* file)
{
    Log("theme files missing: %s", file);
    LogFlush();
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    (void)d3d;
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    IDirect3DDevice9* device = NULL;
    if (SUCCEEDED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device)))
    {
        static WCHAR text[256];
        _snwprintf_s(text, ARRAYSIZE(text), _TRUNCATE,
                     L"SlopDash's Blades theme needs your own dashboard files (%S is missing).\n\n"
                     L"Run SlopDash Importer on a PC to put them on this USB stick.", file);
        static LPCWSTR buttons[] = { L"Xbox Dashboard" };
        MESSAGEBOX_RESULT result;
        XOVERLAPPED ov;
        ZeroMemory(&result, sizeof(result));
        ZeroMemory(&ov, sizeof(ov));
        bool shown = false;
        for (DWORD start = GetTickCount();;)
        {
            device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
            device->Present(NULL, NULL, NULL, NULL);
            if (!shown)
            {
                DWORD r = XShowMessageBoxUI(XUSER_INDEX_ANY, L"Theme Files Missing", text, 1, buttons, 0,
                                            XMB_ALERTICON, &result, &ov);
                shown = r == ERROR_IO_PENDING || r == ERROR_SUCCESS;
                if (!shown && GetTickCount() - start > 10000)
                    break;
            }
            else if (XHasOverlappedIoCompleted(&ov))
                break;
        }
    }
    XLaunchNewImage(XLAUNCH_KEYWORD_DASH, 0);      // (doesn't return)
}

int __cdecl main()
{
    SetUnhandledExceptionFilter(CrashFilter);
    Log("=== SlopDash 0.0.111 (Blades test) ===");
    Mount("hdd:", "\\Device\\Harddisk0\\Partition1");
    Mount("flash:", "\\Device\\Flash");
    Mount("sysext:", "\\Device\\Harddisk0\\SystemExtPartition");
    Mount("usb:", "\\Device\\Mass0");
    Mount("dvd:", "\\Device\\Cdrom0");          // the disc tray
    // SlopDash's and SlopGuide's own folder on the hard drive (settings they
    // make, screenshots, recordings, Plugins.ini): a fresh console has none.
    CreateDirectoryA("hdd:\\SlopDash", NULL);

    g_launch = StartupTheme() != "blades";
    if (g_launch)
    {
        // Blades needs the user's 1888 and 6717 files: in one pack,
        // <Themes>\blades1888.pak (the SlopDash Importer writes it), or as
        // they are in <Themes>\blades1888, 6717's dash.xex in its 6717 folder
        // (earlier importers; before the importer, 6717's sat in hdd:\Apps\6717).
        std::string pack = ThemePack("blades1888");
        std::string folder = ThemeFolder("blades1888");
        static std::string dash6717 = folder + "\\6717\\dash.xex";
        if (pack.empty())
        {
            if (!FileThere(dash6717.c_str()))
                dash6717 = kDashPath;
            const char* const kNeeded[] = { "dash.xex", "xam.xex", "hud.xex", "huduiskin.xex" };
            for (size_t i = 0; i < ARRAYSIZE(kNeeded); i++)
                if (!FileThere((folder + "\\" + kNeeded[i]).c_str()))
                    return MissingTheme(kNeeded[i]);
            if (!FileThere(dash6717.c_str()))
                return MissingTheme("6717 dash.xex");
        }
        std::vector<xur::CustomClass> custom;
        for (size_t i = 0; i < ARRAYSIZE(kBlades1888Classes); i++)
        {
            const StandInInfo& s = kBlades1888Classes[i];
            xur::CustomClass c;
            for (const wchar_t* p = s.name; *p; p++) c.name += (char)*p;
            for (const wchar_t* p = s.base; *p; p++) c.base += (char)*p;
            c.types.assign(s.types, s.types + s.count);
            custom.push_back(c);
        }
        SoundTestArm();
        if (!pack.empty())
        {
            Log("Blades from its theme pack, %s", pack.c_str());
            if (!LoadDashboard1888Pack(pack.c_str(), custom))
                return MissingTheme("blades1888.pak");
        }
        else if (!LoadDashboard1888(folder, custom, dash6717.c_str()))
            return MissingTheme("dash.xex");
    }
    else if (!LoadDashboard(kDashPath, kSharedResPath))
        return 1;
    ScanLibrary();
    StartFetcher();

    // XAudio2 must exist before XUI's sound class plays anything.
    IXAudio2* audio = NULL;
    IXAudio2MasteringVoice* master = NULL;
    HRESULT hr = XAudio2Create(&audio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (SUCCEEDED(hr))
        hr = audio->CreateMasteringVoice(&master);
    Log("XAudio2: %08X", (unsigned)hr);
    g_audio = audio;
    if (master)
    {
        XAUDIO2_VOICE_DETAILS md;
        master->GetVoiceDetails(&md);
        float vol = 0;
        master->GetVolume(&vol);
        Log("XAudio2 master: %u channels, %u Hz, volume %.2f", md.InputChannels, md.InputSampleRate, vol);
    }

    XUIInitParams params;
    XUI_INIT_PARAMS(params);
    params.pHooks = &g_hooks;
    // Our own device, sized for the TV mode: 1280x720 for widescreen (the
    // console's scaler takes it to 1080p or whatever the output is), 640x480 for 4:3.
    XVIDEO_MODE mode;
    ZeroMemory(&mode, sizeof(mode));
    XGetVideoMode(&mode);
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = mode.fIsWideScreen ? 1280 : 640;
    pp.BackBufferHeight = mode.fIsWideScreen ? 720 : 480;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.FrontBufferFormat = D3DFMT_LE_X8R8G8B8;
    pp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    Log("video mode %ux%u%s%s, back buffer %ux%u", (unsigned)mode.dwDisplayWidth, (unsigned)mode.dwDisplayHeight,
        mode.fIsWideScreen ? " widescreen" : "", mode.fIsHiDef ? " HD" : "", pp.BackBufferWidth,
        pp.BackBufferHeight);
    if (kOwnDevice)
    {
        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        (void)d3d;      // the XDK's IDirect3D9 methods are static, so the compiler sees d3d as unused
        IDirect3DDevice9* device = NULL;
        hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
        if (FAILED(hr))
        {
            Log("CreateDevice: %08X", (unsigned)hr);
            return 1;
        }
        hr = g_app.InitShared(device, &pp, TextureLoader, &params);
    }
    else
        hr = g_app.Init(TextureLoader, &params);    // XUI creates the device, as in 0.0.5
    Log("XUI init: %08X", (unsigned)hr);
    if (FAILED(hr))
        return 1;
    // XUI's own device presents at once (about 113 frames a second, unsynced):
    // the GPU drew twice what the TV shows and the blades tore mid-move. One
    // frame per refresh, as the dashboard draws.
    IDirect3DDevice9* xuiDevice = NULL;
    if (SUCCEEDED(XuiRenderGetDevice(g_app.GetDC(), &xuiDevice)) && xuiDevice)
    {
        DWORD was = 0;
        xuiDevice->GetRenderState(D3DRS_PRESENTINTERVAL, &was);
        xuiDevice->SetRenderState(D3DRS_PRESENTINTERVAL, D3DPRESENT_INTERVAL_ONE);
        Log("present interval %08X -> one", (unsigned)was);
        movie::Attach(xuiDevice, audio);    // (Media > Videos' player draws on it)
    }       // (no reference added: the XDK's samples don't release it)

    // Fonts: the dashboard's text uses "ConvectionUI", which is XenonCLatin.xtt
    // on the console's flash; any other font found is only a fallback.
    Log("looking for fonts:");
    // The theme pack's copy first, straight from memory (the pack stays loaded).
    bool packFont = false;
    const unsigned char* fontData = NULL;
    unsigned fontSize = 0;
    if (g_launch && ThemePackFile("xenonclatin.xtt", &fontData, &fontSize))
    {
        wchar_t url[64];
        _snwprintf_s(url, 64, _TRUNCATE, L"memory://%X,%X", (unsigned)(UINT_PTR)fontData, fontSize);
        hr = g_app.RegisterDefaultTypeface(L"ConvectionUI", url);
        Log("typeface ConvectionUI <- the theme pack's xenonclatin.xtt (%S): %08X", url, (unsigned)hr);
        if (SUCCEEDED(hr))
        {
            g_app.RegisterTypeface(L"ConvectionUIFallback", url);
            packFont = true;
        }
    }
    std::string font;
    std::string ownFont = ThemeFolder(CurrentTheme()) + "\\xenonclatin.xtt";    // the theme's era's own copy
    if (!packFont)
    {
        if (g_launch && GetFileAttributesA(ownFont.c_str()) != (DWORD)-1)
            font = ownFont;
        else if (GetFileAttributesA("flash:\\xenonclatin.xtt") != (DWORD)-1)
            font = "flash:\\xenonclatin.xtt";
        else
        {
            font = ListFonts("flash:\\");
            if (font.empty()) font = ListFonts("sysext:\\20449700\\");
        }
    }
    if (!font.empty())
    {
        std::string url = "file://" + font;
        for (size_t i = 0; i < url.size(); i++)
            if (url[i] == '\\') url[i] = '/';
        std::wstring wurl(url.begin(), url.end());
        hr = g_app.RegisterDefaultTypeface(L"ConvectionUI", wurl.c_str());
        Log("typeface ConvectionUI <- %s: %08X", url.c_str(), (unsigned)hr);
        g_app.RegisterTypeface(L"ConvectionUIFallback", wurl.c_str());
    }
    else if (!packFont)
        Log("no font found: text won't show");

    std::wstring base;
    // The Blades theme's colours, the user's (System > Themes > X): into the
    // skins before they load.
    if (g_launch)
        ApplyBladeColours();
    if (PackageLocator(L"dashuisk", base))
    {
        std::wstring skin = base + L"skin.xur";
        hr = g_app.LoadSkin(skin.c_str());
        Log("skin %S: %08X", skin.c_str(), (unsigned)hr);
    }
    // The profile's Dashboard Trim (the Guide's Customize): its blade visuals over the skin's.
    if (g_launch)
        LoadTrim();
    // 6717's skin, under its own prefix: the Marketplace blade's visuals (blades1888.cpp).
    if (g_launch && PackageLocator(L"dashuisk6717", base))
    {
        std::wstring skin = base + L"skin.xur";
        hr = g_app.LoadSkin(skin.c_str(), L"M6");
        Log("6717 skin %S (prefix M6): %08X", skin.c_str(), (unsigned)hr);
    }
    // 1888's Guide and xam's screens draw with the Guide's own skin, under a prefix.
    if (g_launch && PackageLocator(L"huduiskin", base))
    {
        std::wstring skin = base + L"skin.xur";
        hr = g_app.LoadSkin(skin.c_str(), L"Hud");
        Log("guide skin %S (prefix Hud): %08X", skin.c_str(), (unsigned)hr);
    }
    HXUIOBJ scene = NULL;
    if (PackageLocator(L"dashmain", base))
    {
        const std::wstring& pkg = base;
        hr = g_app.LoadFirstScene(pkg.c_str(), L"dashmain.xur", NULL, &scene);
        Log("scene %Sdashmain.xur: %08X", pkg.c_str(), (unsigned)hr);
    }
    g_app.m_designW = kDesignWidth;
    g_app.m_designH = kDesignHeight;
    g_app.m_screenW = (float)pp.BackBufferWidth;
    g_app.m_screenH = (float)pp.BackBufferHeight;
    g_app.m_prevButtons = 0;
    g_app.m_frame = 0;
    g_app.m_focusFrame = 0;
    g_app.m_lastFocus = NULL;
    g_app.m_root = scene;
    if (scene)
    {
        g_app.RecordOriginal(scene);
        Log("main scene: %u elements", (unsigned)g_app.m_original.size());
        if (!g_launch)
            ApplyBladesOptions(g_app.m_root);     // e.g. the Homebrew blade
    }
    g_app.m_openBlade = 0;
    g_app.m_currentFull = g_app.m_pendingBack = NULL;
    g_app.m_backFrame = 0;
    g_app.m_storageAt = 0;
    g_app.m_launchBackFrame = 0;
    g_app.m_launchBackFocus = NULL;
    g_app.m_started = g_app.m_lastBeat = GetTickCount();
    if (scene && kScaleScene && g_launch)
    {
        // The 1888 theme scales the layer the main scene is in, so the screens
        // opened beside it and the Guide share the fit.
        HXUIOBJ layer = NULL;
        XuiElementGetParent(scene, &layer);
        Log("1888 layer: %p (main scene %p)", layer, scene);
        SetBlades1888Layer(layer ? layer : scene);
        g_app.FitLaunchDash(layer ? layer : scene);
    }
    else if (scene && kScaleScene)
        g_app.ScaleScene(scene, kDesignWidth, kDesignHeight);
    g_app.m_scaled = false;
    if (kScaleToScreen)
    {
        g_app.m_scaled = true;
        g_app.FitToScreen(kDesignWidth, kDesignHeight, (float)pp.BackBufferWidth, (float)pp.BackBufferHeight);
    }

    // Blades starts on a black "Default" frame; its own code played a boot
    // animation that fades the blades in. Play the Games one.
    if (scene && kPlayBoot)
    {
        HXUIOBJ root = scene;
        int frame = 0;
        if (FAILED(XuiElementFindNamedFrame(root, L"BootGames", &frame)))
        {
            HXUIOBJ child = NULL;
            if (SUCCEEDED(XuiElementGetChildById(scene, L"RootScene", &child)) && child)
                root = child;
        }
        App::PlayRange(root, L"BootGames", L"EndBootGames");
    }
    Log("switches: own device %d, view scale %d, scene scale %d, boot animation %d, silent sounds %d", kOwnDevice,
        kScaleToScreen, kScaleScene, kPlayBoot, kSilentSounds);
    Log("running");
    g_app.Run();
    g_app.Uninit();
    return 0;
}
