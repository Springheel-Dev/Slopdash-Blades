// The System blade's settings screens (consoles#dashSys*.xur). The dashboard's
// code filled them in and saved the changes; here they show the console's real
// settings (platform/xconfig.h) and, where SlopDash can do it safely, change
// them. Family Settings are left alone. Included by blades.cpp, inside its
// anonymous namespace (it uses the helpers there).
//
// Lists are filled the way the Games Library's is: the list's own class answers
// XM_GET_ITEMCOUNT / XM_GET_SOURCE_TEXT. (0.0.52 and 0.0.53 set an XuiCommonList's
// ItemsText / ItemsNavPath at runtime instead, and crashed as the list drew.)
//
// System > Themes: the IPTV entry (unused: no IPTV service here) becomes the
// theme portal, a list screen (below) on the Language screen's layout. Games
// Library > Y (Library Settings) is built the same way.

const DWORD kSettingsTimer = 0x53455454;    // "SETT"
bool g_overBlades = false;      // the screen being made opens over the blades (their panel behind it)

void SetLabel(HXUIOBJ scene, const wchar_t* id, const std::wstring& text)
{
    HXUIOBJ h = Descendant(scene, id);
    if (h)
        XuiControlSetText(h, text.c_str());
}

int ListIndex(HXUIOBJ list)
{
    return list ? XuiListGetCurSel(list, NULL) : -1;
}

bool IsFrom(HXUIOBJ source, HXUIOBJ ancestor)
{
    for (HXUIOBJ p = source; p; XuiElementGetParent(p, &p))
        if (p == ancestor)
            return true;
    return false;
}

// The full-screen scene an element is in: the dashboard's root scene for the
// blades, or a settings screen itself (they sit beside it, under the canvas).
HXUIOBJ TopScene(HXUIOBJ obj)
{
    HXUIOBJ p = obj;
    for (;;)
    {
        HXUIOBJ parent = NULL, grand = NULL;
        if (FAILED(XuiElementGetParent(p, &parent)) || !parent)
            return p;
        if (FAILED(XuiElementGetParent(parent, &grand)) || !grand)
            return p;               // parent is the canvas
        p = parent;
    }
}

// Another theme sharing these screens (Blades (launch)) places a screen opened
// from its blades itself; NULL for this one.
ScreenOpener g_screenOpener = NULL;

// Opens a settings screen over the one `from` is in, placed the same way.
HRESULT OpenSettingsScreen(HXUIOBJ from, const wchar_t* xur)
{
    // (asked first, with no scene: a screen made only to be thrown away, when
    // it isn't from the blades, built its list twice - a game's Saved Games
    // scanned every profile's content twice, 380 ms: the crawl of 2026-10-14)
    if (g_screenOpener && g_screenOpener(from, NULL) != E_NOTIMPL)
    {
        std::wstring base;
        PackageLocator(L"consoles", base);
        HXUIOBJ scene = NULL;
        g_overBlades = true;
        HRESULT hr = XuiSceneCreate(base.c_str(), xur, NULL, &scene);
        g_overBlades = false;
        if (SUCCEEDED(hr))
        {
            hr = g_screenOpener(from, scene);
            if (hr != E_NOTIMPL)
            {
                Log("settings: opened %S over the blades: %08X", xur, (unsigned)hr);
                return hr;
            }
            XuiDestroyObject(scene);    // not from the blades: opened below, as here
        }
    }
    // From the nearest full-screen (1120 x 770) scene: the System blade's own
    // scene (as its Console Settings entry does: the blade opens around it), the
    // Games Library, or another settings screen. Not the main scene: hiding it
    // hid the whole dashboard.
    HXUIOBJ top = NULL;
    for (HXUIOBJ o = from; o && !top; XuiElementGetParent(o, &o))
    {
        float w = 0, h = 0;
        XuiElementGetBounds(o, &w, &h);
        if (w == 1120 && h == 770)
            top = o;
    }
    if (!top)
        top = TopScene(from);
    std::wstring base;
    PackageLocator(L"consoles", base);      // the resolver finds the file in whichever package has it
    HXUIOBJ scene = NULL;
    HRESULT hr = XuiSceneCreate(base.c_str(), xur, NULL, &scene);
    if (SUCCEEDED(hr))
        hr = XuiSceneNavigateForward(top, FALSE, scene, XUSER_INDEX_FOCUS);
    // Placed like the scene it came from only if it sits on the canvas itself.
    // Inside a scaled scene (0.0.57, from the Games Library) the copy scaled
    // and moved it twice; there the file's own placement stands, and main.cpp
    // moves full-screen scenes inside the main scene to its origin.
    HXUIOBJ parent = NULL, grand = NULL;
    if (SUCCEEDED(hr))
    {
        XuiElementGetParent(scene, &parent);
        if (parent)
            XuiElementGetParent(parent, &grand);
        if (parent && !grand)
        {
            D3DXVECTOR3 scale, pos, pivot;
            XuiElementGetScale(top, &scale);
            XuiElementGetPosition(top, &pos);
            XuiElementGetPivot(top, &pivot);
            XuiElementSetPivot(scene, &pivot);
            XuiElementSetScale(scene, &scale);
            XuiElementSetPosition(scene, &pos);
        }
    }
    Log("settings: opened %S: %08X (parent %S, %s)", xur, (unsigned)hr, parent ? ElementId(parent).c_str() : L"-",
        parent && !grand ? "placed like its opener" : "file's placement");
    return hr;
}

// A list's answers to XUI: item count and each item's text.
bool AnswerList(XUIMessage* msg, int count, const wchar_t* (*text)(int))
{
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
    if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData && d->iItem >= 0 && d->iItem < count)
        {
            d->szText = d->iData == 0 ? text(d->iItem) : L"";
            msg->bHandled = TRUE;
        }
        return true;
    }
    if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = NULL;
            msg->bHandled = TRUE;
        }
        return true;
    }
    return false;
}

// --- Console Settings (dashSysCslSet.xur: dashSettingsRoot, list DashSettingsList) ---
// The right-hand panel shows the highlighted setting's current value and what it does.
struct SettingsEntry
{
    const wchar_t* label;
    const wchar_t* scene;
    const wchar_t* about;           // SlopDash's own wording
    std::wstring (*current)();
};

std::wstring CurrentAutoOff() { return xconfig::IsAutoOffOn() ? L"Enabled" : L"Disabled"; }
std::wstring CurrentScreensaver() { return xconfig::IsScreensaverOn() ? L"Enabled" : L"Disabled"; }
std::wstring CurrentRemote() { return L"Xbox 360 remote"; }

const SettingsEntry kConsoleSettings[] = {
    { L"Display", L"dashSysCslSetDisplay.xur", L"Screen resolution, widescreen and HDTV output.", xconfig::DisplayText },
    { L"Audio", L"dashSysCslSetAudio.xur", L"Analog and digital sound output.", xconfig::AudioText },
    { L"Language", L"dashSysCslSetLanguage.xur", L"The language the console uses.", xconfig::LanguageText },
    { L"Clock", L"dashSysCslSetClock.xur", L"Date, time, time format, time zone and daylight saving.",
      xconfig::ClockText },
    { L"Locale", L"dashSysCslSetCountry.xur", L"The country or region you live in.", xconfig::LocaleText },
    { L"Auto-Off", L"dashSysCslSetAutoOff.xur", L"Turns the console off after 6 hours without use.", CurrentAutoOff },
    { L"Screen Saver", L"dashSysCslSetScreensaver.xur", L"Dims the picture after 10 minutes without use.",
      CurrentScreensaver },
    { L"Remote Control", L"dashSysCslSetRemoteC.xur", L"Which remote controls work with this console.", CurrentRemote },
    { L"System Info", L"dashSysCslSetPolicyInfo_System.xur", L"About this console.", xconfig::SystemInfoText },
};
const int kConsoleSettingsCount = sizeof(kConsoleSettings) / sizeof(kConsoleSettings[0]);

const wchar_t* SettingLabel(int i)
{
    return kConsoleSettings[i].label;
}

void ShowSettingInfo(HXUIOBJ scene, int i)
{
    if (i < 0 || i >= kConsoleSettingsCount)
        return;
    // Under the panel's "Current Setting" heading: the value, then what it is.
    HXUIOBJ info = Ensure(scene, L"rdSettingInfo", L"XuiLabel", L"Label_Body", 592, 205, 380, 360);
    std::wstring text = kConsoleSettings[i].current() + L"\r\n\r\n" + kConsoleSettings[i].about;
    if (info)
        XuiControlSetText(info, text.c_str());
}

const StandInInfo kSettingsList = { L"DashSettingsList", L"XuiCommonList", 0, { 0 } };

HRESULT SettingsListProc(HXUIOBJ, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    AnswerList(msg, kConsoleSettingsCount, SettingLabel);
    return S_OK;
}

const StandInInfo kSettingsRoot = { L"dashSettingsRoot", L"DashScene", 0, { 0 } };

HRESULT SettingsRootProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ list = Child(obj, L"lstSettings");
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kSettingsTimer, 50);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kSettingsTimer)
    {
        XuiKillTimer(obj, kSettingsTimer);
        ShowSettingInfo(obj, ListIndex(list));
    }
    else if (msg->dwMessage == XM_NOTIFY)
    {
        XUINotify* n = (XUINotify*)msg->pvData;
        if (n->dwNotify == XN_SELCHANGED)
            ShowSettingInfo(obj, ListIndex(list));
        else if (n->dwNotify == XN_PRESS && IsFrom(n->hObjSource, list))
        {
            int i = ListIndex(list);
            if (i >= 0 && i < kConsoleSettingsCount)
                OpenSettingsScreen(obj, kConsoleSettings[i].scene);
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// --- Two-choice lists: Time Format, Daylight Saving --------------------------
// listOptions: an XuiCommonList with its two items in the file; A picks one.
const StandInInfo kClockFormat = { L"dashClockSettings_1224", L"XuiScene", 0, { 0 } };
const StandInInfo kClockDst = { L"dashClockSettings_DSaving", L"XuiScene", 0, { 0 } };

void SetRetailFlag(DWORD bit, bool on)
{
    DWORD f = xconfig::ReadU32(xconfig::kUser, xconfig::kRetailFlags);
    DWORD changed = on ? (f | bit) : (f & ~bit);
    Log("retail flags %08X -> %08X", (unsigned)f, (unsigned)changed);
    if (changed != f)
        xconfig::Write(xconfig::kUser, xconfig::kRetailFlags, &changed, sizeof(changed));
}

// Time Format: item 1 = 24-hour (bit set). Daylight Saving: item 1 = Off ("DST off" bit set).
HRESULT TwoChoiceProc(HXUIOBJ obj, XUIMessage* msg, bool dst)
{
    HXUIOBJ list = Child(obj, L"listOptions");
    bool bitSet = dst ? !xconfig::IsDaylightSaving() : xconfig::Is24HourClock();
    int current = bitSet ? 1 : 0;
    const wchar_t* names[2] = { dst ? L"On" : L"12-Hour Clock", dst ? L"Off" : L"24-Hour Clock" };
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kSettingsTimer, 50);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kSettingsTimer)
    {
        XuiKillTimer(obj, kSettingsTimer);
        if (list)
            XuiListSetCurSel(list, current);
        SetLabel(obj, L"labCurrentSettings", names[current]);
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS &&
             IsFrom(((XUINotify*)msg->pvData)->hObjSource, list))
    {
        int pick = ListIndex(list);
        if (pick == 0 || pick == 1)
        {
            SetRetailFlag(dst ? xconfig::kRetailDstOff : xconfig::kRetail24HourClock, pick == 1);
            SetLabel(obj, L"labCurrentSettings", names[pick]);
        }
        msg->bHandled = TRUE;
    }
    return S_OK;
}

HRESULT ClockFormatProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return TwoChoiceProc(obj, msg, false);
}

HRESULT ClockDstProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return TwoChoiceProc(obj, msg, true);
}

// --- List screens: the Language screen's layout, reused ----------------------
// consoles#dashSysCslSetLanguage.xur (scene dashLanguageSettings, list
// dashCLanguage) is a header, a list, and a panel with "Current Setting" and a
// description. SlopDash's own screens (Themes, Library Settings, Scan Paths,
// the folder browser, Library View) are that screen with a ListScreen behind
// it; opened plainly, it's the console's Language setting.
class ListScreen
{
public:
    virtual ~ListScreen() {}
    virtual const wchar_t* Header() = 0;
    virtual const wchar_t* CurrentHeading() { return L"Current Setting"; }
    virtual std::wstring Current() = 0;
    virtual void Rebuild() = 0;                     // fills items
    virtual std::wstring About(int i) = 0;
    virtual void Press(HXUIOBJ scene, int i) = 0;
    virtual const wchar_t* XLabel() { return NULL; }
    virtual void X(HXUIOBJ, int) {}
    virtual const wchar_t* YLabel() { return NULL; }
    virtual void Y(HXUIOBJ, int) {}
    virtual int Initial() { return 0; }             // the item highlighted at first
    virtual int SelectAfter(int i) { return i; }    // ... after a change made on item i
    virtual void Preview(HXUIOBJ, int) {}           // a picture for item i (Library View)
    virtual void Tick(HXUIOBJ) {}                   // every half second (e.g. waiting for the keyboard)
    // Tickboxes on the rows: 1888's checked list (its List_Check visual, the
    // one music's Rip CD song list uses). Ticked(i): 1 ticked, 0 not, -1 a row
    // without a tickbox.
    virtual bool Tickboxes() { return false; }
    virtual int Ticked(int) { return -1; }
    std::vector<std::wstring> items;
};

struct OpenScreen
{
    ListScreen* screen;
    int shown;                  // items the list holds now
    unsigned generation;        // of the settings it last showed
};
std::map<HXUIOBJ, OpenScreen> g_screens;
ListScreen* g_opening = NULL;   // the screen the scene being created is for
unsigned g_generation = 1;      // bumped whenever a list screen changes something
DWORD g_lastAction = 0;         // X arrives as a key and as the legend's press: once

HRESULT OpenListScreen(HXUIOBJ from, ListScreen* screen)
{
    g_opening = screen;
    HRESULT hr = OpenSettingsScreen(from, L"dashSysCslSetLanguage.xur");
    g_opening = NULL;
    if (FAILED(hr))
        delete screen;
    else
        Blades1888ScreenOpened();
    return hr;
}

void SetStatus(HXUIOBJ scene, const std::wstring& text)
{
    SetLabel(scene, L"labMetaBody", text);
}

// Shows the screen's items and labels (again, after a change).
void RefreshLabels(HXUIOBJ scene, OpenScreen& s, int item, bool status = true);

void RefreshScreen(HXUIOBJ scene, OpenScreen& s, int select)
{
    s.screen->Rebuild();
    HXUIOBJ list = Child(scene, L"lstLanguages");
    int now = (int)s.screen->items.size();
    if (list)
    {
        // The count XUI is told follows what the list holds at each step.
        if (s.shown > 0)
            XuiListDeleteItems(list, 0, s.shown);
        s.shown = now;
        XuiListInsertItems(list, 0, now);
        if (s.screen->Tickboxes())
            for (int i = 0; i < now; i++)
            {
                int t = s.screen->Ticked(i);
                XuiListSetItemCheck(list, i, t > 0);
                HXUIOBJ row = XuiListGetItemControl(list, i);
                if (row)
                    SetBoolProperty(row, L"Checkable", t >= 0);
            }
        // (scrolled to it: the Grid's sizes opened at 1 x 1 with the chosen 4 x 8
        // selected out of sight)
        if (now > 0)
            XuiListSetCurSelVisible(list, select < 0 ? 0 : select >= now ? now - 1 : select);
    }
    s.shown = now;
    s.generation = g_generation;
    RefreshLabels(scene, s, list ? ListIndex(list) : 0);
}

// The screen's header, current setting, legends and the item's About: also
// written as the scene is made, before it's first drawn (the scene file's own
// "Language Settings" showed while it opened - the RetroAchievements panel's
// glitch, 2026-11-03).
void RefreshLabels(HXUIOBJ scene, OpenScreen& s, int item, bool status)
{
    SetLabel(scene, L"labHeader", s.screen->Header());
    SetLabel(scene, L"labCurrentSettingsText", s.screen->CurrentHeading());
    SetLabel(scene, L"labCurrentSettings", s.screen->Current());
    const wchar_t* legendIds[2] = { L"legend_x", L"legend_y" };
    const wchar_t* legendText[2] = { s.screen->XLabel(), s.screen->YLabel() };
    for (int k = 0; k < 2; k++)
    {
        HXUIOBJ b = Child(scene, legendIds[k]);
        if (!b)
            continue;
        if (legendText[k])
            XuiControlSetText(b, legendText[k]);
        XuiControlSetEnable(b, legendText[k] != NULL);
        XuiElementSetShow(b, legendText[k] != NULL);
    }
    if (!status)
        return;                         // (at creation: the list isn't filled yet)
    SetStatus(scene, s.screen->About(item));
    s.screen->Preview(scene, item);
}

void ScreenAction(HXUIOBJ scene, OpenScreen& s, int button)      // 0 A, 1 X, 2 Y
{
    DWORD now = GetTickCount();
    if (now - g_lastAction < 300)
        return;
    g_lastAction = now;
    int i = ListIndex(Child(scene, L"lstLanguages"));
    if (i < 0 || i >= (int)s.screen->items.size())
        return;
    unsigned before = g_generation;
    if (button == 1)
        s.screen->X(scene, i);
    else if (button == 2)
        s.screen->Y(scene, i);
    else
        s.screen->Press(scene, i);
    // A change shown at once (a sub-screen opened instead shows when we're back).
    std::map<HXUIOBJ, OpenScreen>::iterator it = g_screens.find(scene);
    if (g_generation != before && it != g_screens.end())
        RefreshScreen(scene, it->second, s.screen->SelectAfter(i));
}

const DWORD kScreenPollTimer = 0x504F4C4C;  // "POLL": back from a sub-screen, show its changes

HRESULT ListScreenMessage(HXUIOBJ obj, OpenScreen& s, XUIMessage* msg)
{
    HXUIOBJ list = Child(obj, L"lstLanguages");
    if (msg->dwMessage == XM_TIMER)
    {
        DWORD id = ((XUIMessageTimer*)msg->pvData)->nId;
        if (id == kSettingsTimer)
        {
            XuiKillTimer(obj, kSettingsTimer);
            RefreshScreen(obj, s, s.screen->Initial());
            XuiSetTimer(obj, kScreenPollTimer, 500);
        }
        else if (id == kScreenPollTimer)
        {
            s.screen->Tick(obj);
            if (s.generation != g_generation)
                RefreshScreen(obj, s, ListIndex(list));
        }
    }
    else if (msg->dwMessage == XM_NOTIFY)
    {
        XUINotify* n = (XUINotify*)msg->pvData;
        if (n->dwNotify == XN_SELCHANGED)
        {
            SetStatus(obj, s.screen->About(ListIndex(list)));
            s.screen->Preview(obj, ListIndex(list));
        }
        else if (n->dwNotify == XN_PRESS && IsFrom(n->hObjSource, list))
        {
            ScreenAction(obj, s, 0);
            msg->bHandled = TRUE;
        }
        else if (n->dwNotify == XN_PRESS && ElementId(n->hObjSource) == L"legend_x")
        {
            ScreenAction(obj, s, 1);
            msg->bHandled = TRUE;
        }
        else if (n->dwNotify == XN_PRESS && ElementId(n->hObjSource) == L"legend_y")
        {
            ScreenAction(obj, s, 2);
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_X)
    {
        ScreenAction(obj, s, 1);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_Y)
    {
        ScreenAction(obj, s, 2);
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// --- A theme's own options (System > Themes > Y) ----------------------------------
// Blades: the Homebrew blade, in the Marketplace blade's place.
class BladesOptionsScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Customize Blades"; }
    const wchar_t* CurrentHeading() { return L"Blades"; }
    std::wstring Current() { return BladesHomebrewOn() ? L"Homebrew blade on" : L"As the original"; }
    void Rebuild()
    {
        items.clear();
        items.push_back(std::wstring(L"Homebrew Blade: ") + (BladesHomebrewOn() ? L"On" : L"Off"));
        items.push_back(std::wstring(L"Guide: ") + (!ThemeGuideAvailable("blades") ? L"System (no Blades Guide found)"
                                                    : ThemeGuideOn("blades") ? L"Blades" : L"System"));
    }
    std::wstring About(int i)
    {
        if (i == 1)
            return L"The Xbox Guide's look while Blades is the default theme: the Blades Guide (2006), or the "
                   L"system's own. It changes the next time the console starts.\r\n\r\nNeeds the SlopGuide "
                   L"plugin and the Blades Guide skin in Guide\\blades.\r\n\r\nA: switch.";
        return L"Puts a Homebrew blade where the Marketplace blade is, the furthest left. For now it's the "
               L"Marketplace blade with the Homebrew name.\r\n\r\nA: switch it on or off.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i == 1)
        {
            if (!ThemeGuideAvailable("blades"))
            {
                SetStatus(scene, L"There's no Blades Guide skin in Guide\\blades yet.");
                return;
            }
            bool on = !ThemeGuideOn("blades");
            SetThemeGuide("blades", on);
            g_generation++;
            SetStatus(scene, on ? L"The Guide will look like Blades after the console restarts."
                                : L"The Guide will be the system's own after the console restarts.");
            return;
        }
        bool on = !BladesHomebrewOn();
        SetBladesHomebrew(on, scene);
        g_generation++;
        SetStatus(scene, on ? L"The Homebrew blade is on: the Marketplace blade is now Homebrew."
                            : L"The Homebrew blade is off: Marketplace is back.");
    }
};

// --- Colours (System > Themes > X on Blades Homebrew) -----------------------------
// Each blade's colour, the blades' frames and the background. A part's page
// lists its colours;
// Y on Colours restarts SlopDash to show them.
class BladeColourScreen : public ListScreen
{
public:
    explicit BladeColourScreen(int part) : m_part(part) {}
    const wchar_t* Header() { return m_header.c_str(); }
    const wchar_t* CurrentHeading() { return L"Colour"; }
    std::wstring Current() { return BladeColourName(m_part, BladeColourChoice(m_part)); }
    void Rebuild()
    {
        m_header = std::wstring(BladeColourPartName(m_part)) + L" Colour";
        int now = BladeColourChoice(m_part);
        items.clear();
        for (int i = 0; i < BladeColourCount(m_part); i++)
            items.push_back(std::wstring(BladeColourName(m_part, i)) + (i == now ? L"  (Selected)" : L""));
    }
    int Initial() { return BladeColourChoice(m_part); }
    std::wstring About(int i)
    {
        return i == 0 ? L"The launch dashboard's own colour." : L"Shows when Y on Colours restarts SlopDash.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        SetBladeColourChoice(m_part, i);
        g_generation++;
        SetStatus(scene, std::wstring(BladeColourPartName(m_part)) + L": " + BladeColourName(m_part, i) + L".");
    }

private:
    int m_part;
    std::wstring m_header;
};

class BladeColoursScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Colours"; }
    const wchar_t* CurrentHeading() { return L"Theme"; }
    std::wstring Current() { return L"Blades Homebrew"; }
    void Rebuild()
    {
        items.clear();
        for (int p = 0; p < BladeColourParts(); p++)
            items.push_back(std::wstring(BladeColourPartName(p)) + L": " + BladeColourName(p, BladeColourChoice(p)));
    }
    std::wstring About(int i)
    {
        if (i < 5)
            return L"The blade's background, its jewel and its glow.";
        return i == 5 ? L"The blades' frames and wings." : L"Behind the blades.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        OpenListScreen(scene, new BladeColourScreen(i));
    }
    const wchar_t* YLabel() { return L"Apply"; }
    void Y(HXUIOBJ, int)
    {
        RestartForBladeColours();
    }
    const wchar_t* XLabel() { return L"Reset All"; }
    void X(HXUIOBJ scene, int)
    {
        for (int p = 0; p < BladeColourParts(); p++)
            SetBladeColourChoice(p, 0);
        g_generation++;
        SetStatus(scene, L"All original. Y applies.");
    }
};

// --- Presets (Customize > Presets): a look in one choice, set on Colours ----------
// Dark Mode: each blade its own colour darker, the
// frames darker, the background black; 1888's shading and shines kept
// (platform/blade_colours.cpp, "Dark").
class BladePresetsScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Presets"; }
    const wchar_t* CurrentHeading() { return L"Theme"; }
    std::wstring Current() { return L"Blades Homebrew"; }
    void Rebuild()
    {
        items.clear();
        items.push_back(L"Original");
        items.push_back(L"Dark Mode");
    }
    std::wstring About(int i)
    {
        return i == 0 ? L"The launch dashboard's own colours.\r\n\r\nA: set. Y: apply (SlopDash restarts)."
                      : L"Each blade in its own colour, darker. Darker frames, a black background.\r\n\r\n"
                        L"A: set. Y: apply (SlopDash restarts).";
    }
    void Press(HXUIOBJ scene, int i)
    {
        int dark = BladeColourIndex(L"Dark"), black = BladeColourIndex(L"Black");
        for (int p = 0; p < BladeColourParts(); p++)
            SetBladeColourChoice(p, i == 0 ? 0 : p == BladeColourParts() - 1 ? black : dark);
        g_generation++;
        SetStatus(scene, i == 0 ? L"Original set. Y applies." : L"Dark Mode set. Y applies.");
    }
    const wchar_t* YLabel() { return L"Apply"; }
    void Y(HXUIOBJ, int)
    {
        RestartForBladeColours();
    }
};

// --- Gamer Card (Customize > Gamer Card): the card's look -------------------------
// 1888's own variations: the Xbox Live Gold banner, the
// Silver one, or the offline card. The dashboard's cards change at once; the
// Guide's next time it opens.
class GamerCardStyleScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Gamer Card"; }
    const wchar_t* CurrentHeading() { return L"Gamer Card"; }
    std::wstring Current() { return GamerCardStyleName(GamerCardStyle()); }
    void Rebuild()
    {
        int now = GamerCardStyle();
        items.clear();
        for (int i = 0; i < GamerCardStyles(); i++)
            items.push_back(std::wstring(GamerCardStyleName(i)) + (i == now ? L"  (Selected)" : L""));
    }
    int Initial() { return GamerCardStyle(); }
    std::wstring About(int i)
    {
        if (i == 0)
            return L"The gold Xbox Live banner.";
        return i == 1 ? L"The silver Xbox Live banner." : L"1888's offline card: Gamerscore, Games, Achievements.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        SetGamerCardStyle(i);
        RefreshGamerCards1888();
        g_generation++;
        SetStatus(scene, std::wstring(L"Gamer card: ") + GamerCardStyleName(i) + L".");
    }
};

// --- Customize (System > Themes > X on Blades Homebrew) ---------------------------
class BladesCustomizeScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Customize"; }
    const wchar_t* CurrentHeading() { return L"Theme"; }
    std::wstring Current() { return L"Blades Homebrew"; }
    void Rebuild()
    {
        items.clear();
        items.push_back(L"Colours");
        items.push_back(L"Presets");
        items.push_back(std::wstring(L"Gamer Card: ") + GamerCardStyleName(GamerCardStyle()));
    }
    std::wstring About(int i)
    {
        if (i == 0)
            return L"Each blade's colour, the frames and the background.";
        return i == 1 ? L"A whole look in one choice: Dark Mode." : L"Xbox Live Gold, Silver, or the offline card.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i == 0)
            OpenListScreen(scene, new BladeColoursScreen());
        else if (i == 1)
            OpenListScreen(scene, new BladePresetsScreen());
        else
            OpenListScreen(scene, new GamerCardStyleScreen());
    }
};

// --- Themes (System > Themes) ---------------------------------------------------
class ThemesScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Themes"; }
    const wchar_t* CurrentHeading() { return L"Current Theme"; }
    std::wstring Current()
    {
        const ThemeInfo* t = FindTheme(CurrentTheme());
        return t ? t->name : L"Blades";
    }
    void Rebuild()
    {
        const std::vector<ThemeInfo>& themes = Themes();
        items.clear();
        for (size_t i = 0; i < themes.size(); i++)
        {
            std::wstring s = themes[i].name;
            if (themes[i].id == DefaultTheme())
                s += L" (default)";
            if (!themes[i].available)
                s += L" (coming soon)";
            items.push_back(s);
        }
    }
    int Initial()
    {
        const std::vector<ThemeInfo>& themes = Themes();
        for (size_t i = 0; i < themes.size(); i++)
            if (themes[i].id == CurrentTheme())
                return (int)i;
        return 0;
    }
    std::wstring About(int i)
    {
        const std::vector<ThemeInfo>& themes = Themes();
        if (i < 0 || i >= (int)themes.size())
            return L"";
        return themes[i].description;      // (what A and X do: the legends say)
    }
    void Press(HXUIOBJ scene, int i)
    {
        const ThemeInfo& t = Themes()[i];
        if (!t.available)
            SetStatus(scene, std::wstring(t.name) + L" isn't built yet.");
        else if (t.id == CurrentTheme())
            SetStatus(scene, std::wstring(t.name) + L" is the theme running now.");
        else
            SwitchTheme(t.id);
    }
    // Customize (X): Blades Homebrew's colours.
    const wchar_t* XLabel() { return L"Customize"; }
    void X(HXUIOBJ scene, int i)
    {
        const ThemeInfo& t = Themes()[i];
        if (std::string(t.id) == "blades1888")
            OpenListScreen(scene, new BladesCustomizeScreen());
        else if (std::string(t.id) == "blades")
            OpenListScreen(scene, new BladesOptionsScreen());
        else
            SetStatus(scene, std::wstring(t.name) + L": no options yet.");
    }
    const wchar_t* YLabel() { return L"Set as Default"; }
    void Y(HXUIOBJ scene, int i)
    {
        const ThemeInfo& t = Themes()[i];
        if (SetDefaultTheme(t.id))
        {
            g_generation++;
            SetStatus(scene, std::wstring(t.name) + L" is now the default theme.");
        }
        else
            SetStatus(scene, std::wstring(t.name) + L" isn't built yet.");
    }
};

// --- Library View (Games Library > Y > Library View) ------------------------------
// Each library its own view (library_settings.h): the one this screen was
// opened from. A uses the highlighted view; X opens its options, a page of
// choices to pick from: List's panel, Grid's and
// Tiles' sizes, Index's icons.
const wchar_t* LibraryName()
{
    static const wchar_t* const kNames[] = { L"Played Games", L"Xbox Live Arcade", L"Game Library", L"Applications",
                                             L"Emulators" };     // (LibraryScope order)
    int s = (int)CurrentLibraryScope();
    return kNames[s >= 0 && s < 5 ? s : 0];
}

std::wstring ViewName(const std::string& id)
{
    const std::vector<LibraryView>& v = LibraryViews();
    for (size_t i = 0; i < v.size(); i++)
        if (id == v[i].id)
            return v[i].name;
    return L"List";
}

std::wstring SizeText(int rows, int cols)
{
    wchar_t b[32];
    _snwprintf_s(b, 32, _TRUNCATE, L"%d x %d", rows, cols);
    return b;
}

// A view's option now, in words ("" for a view without options).
std::wstring ViewOption(const std::string& id)
{
    int rows = 0, cols = 0;
    if (id == "list")
        return ListShowsCover() ? L"Cover Art" : L"Description";
    if (id == "grid")
        LibraryGridSize(rows, cols);
    else if (id == "tiles")
        LibraryTilesSize(rows, cols);
    else if (id == "index")
        return IndexIcons() ? L"Small Icons" : L"Names Only";
    else
        return L"";
    return SizeText(rows, cols) + L" (rows x columns)";
}

// A page of choices for one view's option: A picks one (marked Selected).
class ViewOptionScreen : public ListScreen
{
public:
    explicit ViewOptionScreen(const std::string& view) : m_view(view) {}
    const wchar_t* Header() { return m_header.c_str(); }
    const wchar_t* CurrentHeading() { return LibraryName(); }
    std::wstring Current() { return ViewName(m_view) + L": " + ViewOption(m_view); }
    void Rebuild()
    {
        m_header = ViewName(m_view) + L" Options";
        m_choices.clear();
        if (m_view == "list")
        {
            m_choices.push_back(L"Description");
            m_choices.push_back(L"Cover Art");
        }
        else if (m_view == "index")
        {
            m_choices.push_back(L"Names Only");
            m_choices.push_back(L"Small Icons");
        }
        else
            for (int r = 1; r <= kGridMaxRows; r++)
                for (int c = 1; c <= kGridMaxCols; c++)
                    m_choices.push_back(SizeText(r, c));
        int now = Chosen();
        items.clear();
        for (size_t i = 0; i < m_choices.size(); i++)
            items.push_back(m_choices[i] + ((int)i == now ? L"  (Selected)" : L""));
    }
    int Initial() { return Chosen(); }
    std::wstring About(int i)
    {
        if (m_view == "list")
            return i == 0 ? L"Beside the list: the highlighted game's description, with its details."
                          : L"Beside the list: the highlighted game's cover art, filling the panel.";
        if (m_view == "index")
            return i == 0 ? L"Just the names, as small as they go: the most games on one page."
                          : L"Each name with a small picture of its cover.";
        int r = i / kGridMaxCols + 1, c = i % kGridMaxCols + 1;
        wchar_t b[160];
        _snwprintf_s(b, 160, _TRUNCATE, L"%d row%s of %d cover%s: %d games at a glance. The covers grow or shrink to "
                     L"fill the blade.", r, r == 1 ? L"" : L"s", c, c == 1 ? L"" : L"s", r * c);
        return b;
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (m_view == "list")
            SetListShowsCover(i == 1);
        else if (m_view == "index")
            SetIndexIcons(i == 1);
        else if (m_view == "grid")
            SetLibraryGridSize(i / kGridMaxCols + 1, i % kGridMaxCols + 1);
        else
            SetLibraryTilesSize(i / kGridMaxCols + 1, i % kGridMaxCols + 1);
        g_generation++;
        SetStatus(scene, ViewName(m_view) + L" now shows " + m_choices[i] + L".");
    }

private:
    int Chosen()
    {
        int rows = 0, cols = 0;
        if (m_view == "list")
            return ListShowsCover() ? 1 : 0;
        if (m_view == "index")
            return IndexIcons() ? 1 : 0;
        if (m_view == "grid")
            LibraryGridSize(rows, cols);
        else
            LibraryTilesSize(rows, cols);
        return (rows - 1) * kGridMaxCols + (cols - 1);
    }
    std::string m_view;
    std::wstring m_header;
    std::vector<std::wstring> m_choices;
};

class LibraryViewScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Library View"; }
    const wchar_t* CurrentHeading() { return LibraryName(); }
    std::wstring Current()
    {
        std::wstring option = ViewOption(CurrentLibraryView());
        return ViewName(CurrentLibraryView()) + (option.empty() ? L"" : L": " + option);
    }
    void Rebuild()
    {
        const std::vector<LibraryView>& v = LibraryViews();
        items.clear();
        // The view in use is marked (and highlighted when the screen opens).
        for (size_t i = 0; i < v.size(); i++)
            items.push_back(ViewName(v[i].id) + (CurrentLibraryView() == v[i].id ? L"  (Selected)" : L""));
    }
    int Initial()
    {
        const std::vector<LibraryView>& v = LibraryViews();
        for (size_t i = 0; i < v.size(); i++)
            if (CurrentLibraryView() == v[i].id)
                return (int)i;
        return 0;
    }
    std::wstring About(int i)
    {
        const std::vector<LibraryView>& v = LibraryViews();
        if (i < 0 || i >= (int)v.size())
            return L"";
        std::wstring option = ViewOption(v[i].id);
        return std::wstring(v[i].description) + (option.empty() ? L"" : L"\r\n\r\nNow: " + option);
    }
    void Press(HXUIOBJ scene, int i)
    {
        const LibraryView& v = LibraryViews()[i];
        if (SetLibraryView(v.id))
        {
            g_generation++;
            SetStatus(scene, std::wstring(LibraryName()) + L" now uses " + ViewName(v.id) + L".");
        }
    }
    const wchar_t* XLabel() { return L"Customize"; }
    void X(HXUIOBJ scene, int i)
    {
        std::string id = LibraryViews()[i].id;
        if (ViewOption(id).empty())
            SetStatus(scene, ViewName(id) + L" has nothing to customize.");
        else
            OpenListScreen(scene, new ViewOptionScreen(id));
    }
};

// --- Folder browser (Scan Paths > Add a Folder) --------------------------------
class FolderScreen : public ListScreen
{
public:
    FolderScreen() : m_jump(false) {}
    const wchar_t* Header() { return L"Add a Folder"; }
    const wchar_t* CurrentHeading() { return L"Looking In"; }
    std::wstring Current() { return m_dir.empty() ? L"Drives" : Wide(m_dir); }
    void Rebuild()
    {
        items.clear();
        m_paths.clear();
        if (m_dir.empty())
        {
            static const char* const kDrives[] = { "hdd:", "usb:" };
            static const wchar_t* const kNames[] = { L"Hard Drive (hdd:)", L"USB Storage (usb:)" };
            for (int d = 0; d < 2; d++)
            {
                WIN32_FIND_DATAA fd;
                HANDLE h = FindFirstFileA((std::string(kDrives[d]) + "\\*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE)
                    continue;
                FindClose(h);
                items.push_back(kNames[d]);
                m_paths.push_back(kDrives[d]);
            }
            return;
        }
        items.push_back(L".. (up a folder)");
        m_paths.push_back("..");
        std::vector<std::string> dirs;
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((m_dir + "\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.')
                    dirs.push_back(fd.cFileName);
            while (FindNextFileA(h, &fd) && dirs.size() < 500);
            FindClose(h);
        }
        std::sort(dirs.begin(), dirs.end());
        for (size_t i = 0; i < dirs.size(); i++)
        {
            items.push_back(Wide(dirs[i]));
            m_paths.push_back(m_dir + "\\" + dirs[i]);
        }
    }
    std::wstring About(int i)
    {
        if (i < 0 || i >= (int)m_paths.size())
            return L"";
        if (m_paths[i] == "..")
            return L"A: go up a folder.";
        return Wide(m_paths[i]) + L"\r\n\r\nA: open this folder.\r\nX: search this folder for games.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (m_paths[i] == "..")
        {
            size_t slash = m_dir.find_last_of('\\');
            m_dir = slash == std::string::npos ? "" : m_dir.substr(0, slash);
        }
        else
            m_dir = m_paths[i];
        m_jump = true;
        g_generation++;         // the list changes
        (void)scene;
    }
    int SelectAfter(int i)                  // a new folder: from its top
    {
        bool jump = m_jump;
        m_jump = false;
        return jump ? 0 : i;
    }
    const wchar_t* XLabel() { return L"Add Folder"; }
    void X(HXUIOBJ scene, int i)
    {
        if (m_paths[i] == "..")
            return;
        std::vector<std::string> paths = ScanPaths();
        for (size_t k = 0; k < paths.size(); k++)
            if (!_stricmp(paths[k].c_str(), m_paths[i].c_str()))
            {
                SetStatus(scene, Wide(m_paths[i]) + L" is already searched.");
                return;
            }
        paths.push_back(m_paths[i]);
        SetScanPaths(paths);
        g_generation++;
        SetStatus(scene, Wide(m_paths[i]) + L" added. The library will be searched again.");
    }

private:
    static std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
    std::string m_dir;                  // "" = the drives
    std::vector<std::string> m_paths;   // per item
    bool m_jump;
};

// --- Scan Paths (Games Library > Y > Scan Paths) -----------------------------------
class ScanPathsScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Scan Paths"; }
    const wchar_t* CurrentHeading() { return L"Searched Folders"; }
    std::wstring Current()
    {
        wchar_t b[48];
        _snwprintf_s(b, 48, _TRUNCATE, L"%u folders, 4 levels deep", (unsigned)m_paths.size());
        return b;
    }
    void Rebuild()
    {
        m_paths = ScanPaths();
        items.clear();
        for (size_t i = 0; i < m_paths.size(); i++)
            items.push_back(std::wstring(m_paths[i].begin(), m_paths[i].end()));
        items.push_back(L"Add a Folder...");
        items.push_back(L"Restore Defaults");
    }
    std::wstring About(int i)
    {
        if (i >= 0 && i < (int)m_paths.size())
            return L"The library looks for games in this folder and the folders inside it.\r\n\r\nX: stop searching "
                   L"this folder.";
        if (i == (int)m_paths.size())
            return L"Pick another folder for the library to search.";
        return L"Go back to the usual folders: the console's content folders and Games folders, on the hard "
               L"drive and on USB.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i == (int)m_paths.size())
            OpenListScreen(scene, new FolderScreen());
        else if (i == (int)m_paths.size() + 1)
        {
            SetScanPaths(DefaultScanPaths());
            g_generation++;
            SetStatus(scene, L"Back to the usual folders. The library will be searched again.");
        }
        else
            SetStatus(scene, L"X: stop searching this folder.");
    }
    const wchar_t* XLabel() { return L"Remove"; }
    void X(HXUIOBJ scene, int i)
    {
        if (i < 0 || i >= (int)m_paths.size())
            return;
        std::wstring gone(m_paths[i].begin(), m_paths[i].end());
        m_paths.erase(m_paths.begin() + i);
        SetScanPaths(m_paths);
        g_generation++;
        SetStatus(scene, gone + L" removed. The library will be searched again.");
    }

private:
    std::vector<std::string> m_paths;
};

// --- Library Settings (Games Library > Y) ---------------------------------------
class LibrarySettingsScreen : public ListScreen
{
public:
    const wchar_t* Header() { return L"Library Settings"; }
    std::wstring Current()
    {
        std::wstring view = ViewName(CurrentLibraryView());
        wchar_t b[96];
        _snwprintf_s(b, 96, _TRUNCATE, L"%s view, %u folders", view.c_str(), (unsigned)ScanPaths().size());
        return b;
    }
    void Rebuild()
    {
        items.clear();
        items.push_back(L"Scan Paths");
        items.push_back(L"Library View");
    }
    std::wstring About(int i)
    {
        return i == 0 ? L"Choose the folders the library searches for games."
                      : L"Choose how the library shows your games.";
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (i == 0)
            OpenListScreen(scene, new ScanPathsScreen());
        else
            OpenListScreen(scene, new LibraryViewScreen());
    }
};

HRESULT OpenLibrarySettings(HXUIOBJ from)
{
    return OpenListScreen(from, new LibrarySettingsScreen());
}

#include "theme/game_menu.inc"
#include "theme/store_screens.inc"
#include "theme/ra_screen.inc"
#include "theme/services_screen.inc"

// --- The scene and its list -------------------------------------------------------
const wchar_t* const kLanguageNames[] = { L"English", L"Japanese", L"German", L"French", L"Spanish", L"Italian",
                                          L"Korean", L"Chinese (Traditional)", L"Portuguese", L"Polish", L"Russian",
                                          L"Swedish", L"Turkish", L"Norwegian", L"Dutch", L"Chinese (Simplified)" };
const DWORD kLanguageIds[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17 };
const int kLanguageCount = sizeof(kLanguageIds) / sizeof(kLanguageIds[0]);

const wchar_t* LanguageLabel(int i)
{
    return kLanguageNames[i];
}

ListScreen* g_answering = NULL;     // for ScreenLabel (AnswerList takes a plain function)

const wchar_t* ScreenLabel(int i)
{
    return g_answering && i < (int)g_answering->items.size() ? g_answering->items[i].c_str() : L"";
}

const StandInInfo kLanguageList = { L"dashCLanguage", L"XuiList", 0, { 0 } };

HRESULT LanguageListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ scene = NULL;
    XuiElementGetParent(obj, &scene);
    std::map<HXUIOBJ, OpenScreen>::iterator it = g_screens.find(scene);
    ListScreen* screen = it != g_screens.end() ? it->second.screen : g_opening;
    if (screen)
    {
        // Until the scene first shows it, the list is empty (items come with RefreshScreen).
        int count = it != g_screens.end() ? it->second.shown : 0;
        g_answering = screen;
        AnswerList(msg, count, ScreenLabel);
        g_answering = NULL;
    }
    else
        AnswerList(msg, kLanguageCount, LanguageLabel);
    return S_OK;
}

const StandInInfo kLanguageScene = { L"dashLanguageSettings", L"XuiScene", 0, { 0 } };

HRESULT LanguageSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        if (g_opening)
        {
            OpenScreen s = { g_opening, 0, 0 };
            g_opening->Rebuild();
            g_screens[obj] = s;
            // The Language screen expects an opened blade's panel behind it;
            // opened from the Games Library there's none (0.0.56: black). The
            // panel the Games Library itself draws (its figBackground), at the
            // back.
            HXUIOBJ panel = NULL;
            if (!g_overBlades && SUCCEEDED(XuiCreateObject(L"XuiControl", &panel)) && panel)
            {
                SetStringProperty(panel, L"Id", L"rdBackPanel");
                SetStringProperty(panel, L"Visual", L"back panel");
                XuiElementSetBounds(panel, 975, 610);
                D3DXVECTOR3 pos(72.5f, 89.3f, 0);
                XuiElementSetPosition(panel, &pos);
                HXUIOBJ first = NULL;
                XuiElementGetFirstChild(obj, &first);
                XuiElementInsertChild(obj, panel, NULL, first);
            }
            // Tickboxes: the list in 1888's checked style (List_Check: its rows checkable).
            HXUIOBJ list = Child(obj, L"lstLanguages");
            if (list && g_opening->Tickboxes())
                Log("list screen: checked list: %s",
                    SetStringProperty(list, L"Visual", L"List_Check") ? "yes" : "FAILED");
            RefreshLabels(obj, g_screens[obj], 0, false);   // (the list fills at its timer)
            Log("list screen opened: %S at %u", g_opening->Header(), (unsigned)GetTickCount());
        }
        XuiSetTimer(obj, kSettingsTimer, 50);
        return S_OK;
    }
    std::map<HXUIOBJ, OpenScreen>::iterator it = g_screens.find(obj);
    if (msg->dwMessage == XM_DESTROY)
    {
        if (it != g_screens.end())
        {
            // OpenSettingsScreen's first try (over the blades) destroys its scene
            // when it isn't from the blades, then creates it again for the same
            // screen: that one isn't finished with yet (0.0.109: crashed).
            if (it->second.screen != g_opening)
                delete it->second.screen;
            g_screens.erase(it);
        }
        return S_OK;
    }
    if (it != g_screens.end())
        return ListScreenMessage(obj, it->second, msg);
    // The console's Language setting: A on a language makes it the console's.
    if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS &&
        IsFrom(((XUINotify*)msg->pvData)->hObjSource, Child(obj, L"lstLanguages")))
    {
        int i = XuiListGetCurSel(Child(obj, L"lstLanguages"), NULL);
        if (i >= 0 && i < kLanguageCount)
        {
            bool ok = xconfig::SetLanguage(kLanguageIds[i]);
            Log("console settings: language %u: %s", (unsigned)kLanguageIds[i], ok ? "written" : "FAILED");
            if (ok)
                SetLabel(obj, L"labCurrentSettings", kLanguageNames[i]);
        }
        msg->bHandled = TRUE;
        return S_OK;
    }
    // The current one highlighted.
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kSettingsTimer)
    {
        XuiKillTimer(obj, kSettingsTimer);
        DWORD lang = XGetLanguage();
        HXUIOBJ list = Child(obj, L"lstLanguages");
        for (int i = 0; i < kLanguageCount; i++)
            if (kLanguageIds[i] == lang && list)
                XuiListSetCurSel(list, i);
        SetLabel(obj, L"labCurrentSettings", xconfig::LanguageText());
    }
    return S_OK;
}

// --- Clock menu, Auto-Off, Screen Saver: current values (read only for now) ---
const StandInInfo kClockMenu = { L"dashClockSettings_Menu", L"DashScene", 0, { 0 } };
const StandInInfo kAutoOff = { L"dashAutoOff", L"XuiScene", 0, { 0 } };
const StandInInfo kScreensaver = { L"dashScreensaver", L"XuiScene", 0, { 0 } };

HRESULT CurrentValueProc(HXUIOBJ obj, XUIMessage* msg, std::wstring (*current)())
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kSettingsTimer, 50);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kSettingsTimer)
    {
        XuiKillTimer(obj, kSettingsTimer);
        SetLabel(obj, L"labCurrentSettings", current());
    }
    return S_OK;
}

std::wstring ClockFirstLine()
{
    std::wstring s = xconfig::ClockText();
    size_t nl = s.find(L"\r\n");
    return nl == std::wstring::npos ? s : s.substr(0, nl);
}

HRESULT ClockMenuProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return CurrentValueProc(obj, msg, ClockFirstLine);
}

HRESULT AutoOffProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        Log("auto-off value %d, screen saver value %d", xconfig::ReadS16(xconfig::kConsole, xconfig::kAutoShutOff),
            xconfig::ReadS16(xconfig::kConsole, xconfig::kScreenSaver));
    return CurrentValueProc(obj, msg, CurrentAutoOff);
}

HRESULT ScreensaverProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    return CurrentValueProc(obj, msg, CurrentScreensaver);
}

// --- The System blade: IPTV -> Themes ---------------------------------------------
const StandInInfo kSystemScene = { L"DashSystemScene", L"DashScene", 0, { 0 } };

HRESULT SystemSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        HXUIOBJ iptv = Child(obj, L"navIPTVSettings");
        if (iptv)
        {
            XuiControlSetText(iptv, L"Themes");
            SetStringProperty(iptv, L"ImagePath", L"ico_32x_systemSet.png");
            SetStringProperty(iptv, L"PressPath", L"");     // opened here, below
            XuiElementSetShow(iptv, TRUE);
        }
        Log("system blade: themes entry %s", iptv ? "added" : "MISSING");
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS &&
             ElementId(((XUINotify*)msg->pvData)->hObjSource) == L"navIPTVSettings")
    {
        OpenListScreen(obj, new ThemesScreen());
        msg->bHandled = TRUE;
    }
    return S_OK;
}

void RegisterSystemClasses()
{
    struct { const StandInInfo* info; StandInProc proc; } table[] = {
        { &kSystemScene, SystemSceneProc }, { &kSettingsRoot, SettingsRootProc },
        { &kSettingsList, SettingsListProc }, { &kClockFormat, ClockFormatProc },
        { &kClockDst, ClockDstProc },       { &kClockMenu, ClockMenuProc },
        { &kAutoOff, AutoOffProc },         { &kScreensaver, ScreensaverProc },
        { &kLanguageScene, LanguageSceneProc }, { &kLanguageList, LanguageListProc },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        Log("%S: %s", table[i].info->name, RegisterStandInWithProc(*table[i].info, table[i].proc) ? "registered" : "FAILED");
}
