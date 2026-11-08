// --- Console Settings' screens (consoles#dashSysCslSet*.xur) -------------------------
// 1888's own classes for them, which its dashboard code filled in: each
// screen's "Current Setting" (labCurrentSettings; Display's labCurrentSetting),
// the lists its code supplied (time zones, locales, remote controls, HDTV
// modes, the clock's spinners), and the changes it made (platform/xconfig.h,
// as 1888's dash.xex writes them). Display is shown, not changed: a display
// change needs 1888's can-you-see-it check (a mode switch with a revert). System Info shows the console's details
// (temperatures live) and Y the power box; its file's A (a factory reset) isn't used.
// Included in blades1888.cpp (namespace launch).

const DWORD kCslTimer = 0x43534C31;             // "CSL1": once the screen's parts exist
const DWORD kCslSelTimer = 0x43534C32;          // "CSL2": the lists' current item, once they're filled
const DWORD kInfoTimer = 0x43534C33;            // "CSL3": System Info refreshed; the power box's answer

std::wstring ClassOf(HXUIOBJ obj)
{
    XUIClass info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    XuiGetClass(XuiGetObjectClass(obj), &info);
    return info.szClassName ? info.szClassName : L"";
}

void SetCurrent(HXUIOBJ scene, const std::wstring& text)
{
    HXUIOBJ label = Child(scene, L"labCurrentSettings");
    if (!label)
        label = Child(scene, L"labCurrentSetting");
    if (label)
        XuiControlSetText(label, text.c_str());
}

void SetSel(HXUIOBJ list, int i)
{
    if (!list || i < 0)
        return;
    HRESULT hr = XuiListSetCurSel(list, i);
    // (the clock's spinners, List_VerticalSpin, show their top item: it too)
    if (ClassOf(list) == L"dashCValueSpin" || ElementId(list) == L"lstAMPM")
        XuiListSetTopItem(list, i);
    int now = XuiListGetCurSel(list, NULL);
    Log("console settings: %S item %d: %08X, now %d of %d", ElementId(list).c_str(), i, (unsigned)hr, now,
        XuiListGetItemCount(list));
}

int ListPick(HXUIOBJ list)
{
    return list ? XuiListGetCurSel(list, NULL) : -1;
}

bool FromWithin(HXUIOBJ source, HXUIOBJ ancestor)
{
    for (HXUIOBJ o = source; o && ancestor; XuiElementGetParent(o, &o))
        if (o == ancestor)
            return true;
    return false;
}

// --- the lists 1888's code supplied --------------------------------------------------
const wchar_t* const kHdModes[] = { L"480p", L"720p", L"1080i" };   // consoles strings 112, 114, 110

int HdModeIndex()
{
    XVIDEO_MODE m;
    ZeroMemory(&m, sizeof(m));
    XGetVideoMode(&m);
    return m.dwDisplayHeight >= 1080 ? 2 : m.dwDisplayHeight >= 720 ? 1 : 0;
}

// The clock's spinners: their values from the list's id.
int SpinCount(const std::wstring& id)
{
    if (id == L"lstDay") return 31;
    if (id == L"lstMonth") return 12;
    if (id == L"lstYear") return 31;                    // 2005 .. 2035
    if (id == L"lstHour") return xconfig::Is24HourClock() ? 24 : 12;
    if (id == L"lstMin") return 60;
    return 0;
}

int SpinValue(const std::wstring& id, int i)
{
    if (id == L"lstDay" || id == L"lstMonth") return i + 1;
    if (id == L"lstYear") return 2005 + i;
    if (id == L"lstHour") return xconfig::Is24HourClock() ? i : i + 1;
    return i;
}

HRESULT CslListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    std::wstring cls = ClassOf(obj);
    std::wstring id = ElementId(obj);
    int count = 0;
    if (cls == L"dashCTimezone") count = xconfig::ZoneCount();
    else if (cls == L"dashCCountry") count = xconfig::LocaleCount() - 1;     // (not "Unknown")
    else if (cls == L"dashRemoteCList") count = 2;
    else if (cls == L"dashCScreenSize") count = 3;
    else if (cls == L"dashCValueSpin") count = SpinCount(id);
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = count;
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData && d->iItem >= 0 && d->iItem < count)
        {
            static wchar_t num[8];
            const wchar_t* t = L"";
            if (d->iData == 0)
            {
                if (cls == L"dashCTimezone") t = xconfig::ZoneName(d->iItem);
                else if (cls == L"dashCCountry") t = xconfig::LocaleName(d->iItem + 1);
                else if (cls == L"dashRemoteCList") t = xconfig::RemoteName(d->iItem);
                else if (cls == L"dashCScreenSize") t = kHdModes[d->iItem];
                else if (cls == L"dashCValueSpin")
                {
                    _snwprintf_s(num, 8, _TRUNCATE, id == L"lstYear" ? L"%d" : L"%02d", SpinValue(id, d->iItem));
                    t = num;
                }
            }
            d->szText = t;
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = NULL;
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// --- the screens -----------------------------------------------------------------------
DWORD AnalogFlags()
{
    return xconfig::ReadU32(xconfig::kUser, xconfig::kAudioFlags) & xconfig::kAudioAnalogMask;
}

std::wstring AnalogText()
{
    DWORD a = AnalogFlags();
    return a == xconfig::kAudioAnalogMono ? L"Mono" : a == xconfig::kAudioDolbyProLogic ? L"Dolby Pro Logic II" : L"Stereo";
}

int AnalogItem()
{
    DWORD a = AnalogFlags();
    return a == xconfig::kAudioAnalogMono ? 0 : a == xconfig::kAudioDolbyProLogic ? 1 : -1;
}

// One line each (the labels hold one): the mode and the shape, the two outputs.
std::wstring DisplayShort()
{
    XVIDEO_MODE m;
    ZeroMemory(&m, sizeof(m));
    XGetVideoMode(&m);
    wchar_t b[48];
    _snwprintf_s(b, 48, _TRUNCATE, L"%u%s, %s", m.dwDisplayHeight, m.fIsInterlaced ? L"i" : L"p",
                 m.fIsWideScreen ? L"Widescreen" : L"Normal");
    return b;
}

std::wstring DigitalText()
{
    static const wchar_t* const kNames[] = { L"Digital Stereo", L"Dolby Digital 5.1", L"Dolby Digital with WMA Pro" };
    return kNames[xconfig::DigitalAudioIndex()];
}

std::wstring PalText()
{
    XVIDEO_MODE m;
    ZeroMemory(&m, sizeof(m));
    XGetVideoMode(&m);
    if (m.RefreshRate < 55.0f)
        return L"PAL-50";
    return L"PAL-60";
}

std::wstring Widen(const std::string& a)
{
    return std::wstring(a.begin(), a.end());
}

// System Info's text: 1888's first lines (consoles strings 311: "Console
// Serial Number: %hs", "Console ID: %hs"; its legal text and dash version
// left out), then the console as SlopDash sees it. Refreshed every 2 s while
// shown (the temperatures).
std::wstring SystemInfo()
{
    std::wstring s = L"Console Serial Number: " + Widen(console::SerialNumber());
    s += L"\r\nConsole ID: " + Widen(console::ConsoleId());
    std::wstring ip = console::IpAddress();
    s += L"\r\nIP Address: " + (ip.empty() ? std::wstring(L"Not Connected") : ip);
    if (console::DebugMonitorOn())
        s += L"\r\nDebug Connection: On (Port 730)";
    console::Temps t;
    wchar_t b[128];
    if (console::Temperatures(t))
    {
        _snwprintf_s(b, 128, _TRUNCATE, L"\r\nTemperature: CPU %.0f\x00B0" L"C, GPU %.0f\x00B0" L"C, Memory %.0f\x00B0" L"C",
                     t.cpu, t.gpu, t.memory);
        s += b;
    }
    // (sizes as Memory shows them: GB of 2^30 bytes)
    s += L"\r\n";
    static const struct { const char* root; const wchar_t* name; } kDrives[] = {
        { "hdd:\\", L"Hard Drive" }, { "usb:\\", L"USB Storage" } };
    for (int i = 0; i < 2; i++)
    {
        ULONGLONG f = 0, total = 0;
        if (!console::Space(kDrives[i].root, f, total))
            continue;
        _snwprintf_s(b, 128, _TRUNCATE, L"\r\n%s: %.1f GB free of %.1f GB%s", kDrives[i].name, f / 1073741824.0,
                     total / 1073741824.0, i == 0 && f < console::kLowSpace ? L" (Almost Full)" : L"");
        s += b;
    }
    s += L"\r\nVideo: " + xconfig::DisplayText();
    s += L"\r\nLanguage: " + xconfig::LanguageText() + L", " + xconfig::LocaleText();
    // (1888 ended with its dash's version, "D:2.0.1888.0")
    s += L"\r\n\r\nDashboard: SlopDash (Blades Homebrew), built " + Widen(__DATE__);
    std::wstring kernel = console::KernelVersion();
    if (!kernel.empty())
        s += L"\r\nSystem: " + kernel;
    return s;
}

// System Info's Y: the console's power, in the console's own box (the
// Guide's, 1888's style with SlopGuide); the answer is looked for on the
// screen's timer.
XOVERLAPPED g_powerOv;
MESSAGEBOX_RESULT g_powerResult;
bool g_powerAsked = false;

void AskPower()
{
    if (g_powerAsked)
        return;
    static LPCWSTR buttons[] = { L"Restart Dashboard", L"Restart Console", L"Turn Off Console" };
    int user = SignedInUser();
    ZeroMemory(&g_powerOv, sizeof(g_powerOv));
    ZeroMemory(&g_powerResult, sizeof(g_powerResult));
    DWORD r = XShowMessageBoxUI(user >= 0 ? user : 0, L"Power", L"Choose what to do.", 3, buttons, 0,
                                XMB_NOICON, &g_powerResult, &g_powerOv);
    g_powerAsked = r == ERROR_IO_PENDING;
    BoxOpened(&g_powerOv, r);
    Log("system info: power box %u", (unsigned)r);
}

void PowerAnswered()
{
    if (!g_powerAsked || !XHasOverlappedIoCompleted(&g_powerOv))
        return;
    g_powerAsked = false;
    DWORD r = XGetOverlappedResult(&g_powerOv, NULL, FALSE);
    Log("system info: power answer %u, button %u", (unsigned)r, (unsigned)g_powerResult.dwButtonPressed);
    if (r != ERROR_SUCCESS)
        return;                         // (B: nothing)
    if (g_powerResult.dwButtonPressed == 0)
        console::RestartDashboard();
    else if (g_powerResult.dwButtonPressed == 1)
        console::Restart();
    else if (g_powerResult.dwButtonPressed == 2)
        console::TurnOff();
}

// The date and time screen: its spinners set to now; A sets the clock.
void FillClock(HXUIOBJ scene)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    bool h24 = xconfig::Is24HourClock();
    struct { const wchar_t* id; int index; } spins[] = {
        { L"lstDay", t.wDay - 1 }, { L"lstMonth", t.wMonth - 1 }, { L"lstYear", t.wYear - 2005 },
        { L"lstHour", h24 ? t.wHour : (t.wHour % 12 == 0 ? 11 : t.wHour % 12 - 1) }, { L"lstMin", t.wMinute },
    };
    for (size_t i = 0; i < sizeof(spins) / sizeof(spins[0]); i++)
        if (HXUIOBJ l = Find(scene, spins[i].id))
            SetSel(l, spins[i].index < 0 ? 0 : spins[i].index);
    if (HXUIOBJ ampm = Find(scene, L"lstAMPM"))
    {
        XuiElementSetShow(ampm, !h24);
        SetSel(ampm, t.wHour >= 12 ? 1 : 0);
    }
}

void ApplyClock(HXUIOBJ scene)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    bool h24 = xconfig::Is24HourClock();
    int day = SpinValue(L"lstDay", ListPick(Find(scene, L"lstDay")));
    int month = SpinValue(L"lstMonth", ListPick(Find(scene, L"lstMonth")));
    int year = SpinValue(L"lstYear", ListPick(Find(scene, L"lstYear")));
    int hour = SpinValue(L"lstHour", ListPick(Find(scene, L"lstHour")));
    int minute = SpinValue(L"lstMin", ListPick(Find(scene, L"lstMin")));
    if (!h24)
    {
        bool pm = ListPick(Find(scene, L"lstAMPM")) == 1;
        hour = hour % 12 + (pm ? 12 : 0);
    }
    static const int kDays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12 || day < 1 || day > kDays[month - 1] ||
        (month == 2 && day == 29 && !((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)))
    {
        Log("clock: %02d/%02d/%04d isn't a date", day, month, year);
        return;
    }
    t.wDay = (WORD)day;
    t.wMonth = (WORD)month;
    t.wYear = (WORD)year;
    t.wHour = (WORD)hour;
    t.wMinute = (WORD)minute;
    t.wSecond = 0;
    t.wMilliseconds = 0;
    xconfig::SetLocalTime(t);
}

HRESULT CslSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    std::wstring cls = ClassOf(obj);
    if (msg->dwMessage == XM_INIT)
    {
        XuiSetTimer(obj, kCslTimer, 30);
        return S_OK;
    }
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kCslTimer)
    {
        XuiKillTimer(obj, kCslTimer);
        HXUIOBJ list = Child(obj, L"listOptions");
        if (cls == L"dashAudioSettings")
            SetCurrent(obj, AnalogText() + L", " + DigitalText());
        else if (cls == L"dashAudioSettings_A")
        {
            SetCurrent(obj, AnalogText());
            SetSel(list, AnalogItem());
        }
        else if (cls == L"dashAudioSettings_D")
        {
            SetCurrent(obj, DigitalText());
            SetSel(list, xconfig::DigitalAudioIndex());
        }
        else if (cls == L"dashAutoOff")
            SetCurrent(obj, xconfig::IsAutoOffOn() ? L"Enabled" : L"Disabled");
        else if (cls == L"dashScreensaver")
            SetCurrent(obj, xconfig::IsScreensaverOn() ? L"Enabled" : L"Disabled");
        else if (cls == L"dashClockSettings_Menu")
        {
            std::wstring c = xconfig::ClockText();
            size_t nl = c.find(L"\r\n");
            SetCurrent(obj, nl == std::wstring::npos ? c : c.substr(0, nl));
        }
        else if (cls == L"dashClockSettings_1224")
        {
            int i = xconfig::Is24HourClock() ? 1 : 0;
            SetCurrent(obj, i ? L"24-Hour Clock" : L"12-Hour Clock");
            SetSel(list, i);
        }
        else if (cls == L"dashClockSettings_DSaving")
        {
            int i = xconfig::IsDaylightSaving() ? 0 : 1;
            SetCurrent(obj, i ? L"Off" : L"On");
            SetSel(list, i);
        }
        else if (cls == L"dashClockSettings_TZone")
        {
            SetCurrent(obj, xconfig::TimeZoneText());
            if (xconfig::CurrentZone() >= 0)
                SetSel(Child(obj, L"lstTimezone"), xconfig::CurrentZone());
        }
        else if (cls == L"dashClockSettings")
            FillClock(obj);
        else if (cls == L"dashCountrySettings")
        {
            SetCurrent(obj, xconfig::LocaleText());
            if (xconfig::CurrentLocale() > 0)
                SetSel(Child(obj, L"lstCountries"), xconfig::CurrentLocale() - 1);
        }
        else if (cls == L"dashRemoteC")
        {
            SetCurrent(obj, xconfig::RemoteName(xconfig::RemoteIndex()));
            SetSel(Child(obj, L"listChannels"), xconfig::RemoteIndex());
        }
        else if (cls == L"dashVideoSettings")
        {
            // 1888's own layout: the screen's shape drawn in scnCurrentFormat
            // (consoles#metaPane_DisplayWidescreen / _DisplayNormal.xur: a TV
            // outline, "Widescreen" or "Normal" beside it) and the mode above
            // that label (labCurrentSetting).
            XVIDEO_MODE m;
            ZeroMemory(&m, sizeof(m));
            XGetVideoMode(&m);
            wchar_t b[16];
            _snwprintf_s(b, 16, _TRUNCATE, L"%u%s", m.dwDisplayHeight, m.fIsInterlaced ? L"i" : L"p");
            HXUIOBJ slot = Child(obj, L"scnCurrentFormat"), pane = NULL;
            if (slot && SUCCEEDED(AddScene(slot, L"consoles", m.fIsWideScreen ? L"metaPane_DisplayWidescreen.xur"
                                                                              : L"metaPane_DisplayNormal.xur", &pane)))
                SetCurrent(obj, b);
            else
                SetCurrent(obj, DisplayShort());
        }
        else if (cls == L"dashVideoSettings_Format")
        {
            SetCurrent(obj, xconfig::IsWidescreen() ? L"Widescreen" : L"Normal");
            if (HXUIOBJ b = Child(obj, xconfig::IsWidescreen() ? L"btnWide" : L"btnNormal"))
                XuiElementSetFocus(b);
        }
        else if (cls == L"dashVideoSettings_HD")
        {
            SetCurrent(obj, DisplayShort());
            SetSel(list, HdModeIndex());
        }
        else if (cls == L"dashVideoSettings_60")
        {
            SetCurrent(obj, PalText());
            SetSel(list, PalText() == L"PAL-60" ? 0 : 1);
        }
        else if (cls == L"dashSystemReset")
        {
            if (HXUIOBJ info = Child(obj, L"edInfo"))
                XuiControlSetText(info, SystemInfo().c_str());
            Show(obj, L"btnA", false);          // no reset from here
            if (HXUIOBJ y = Child(obj, L"btnY"))
            {
                XuiControlSetText(y, L"Power");
                XuiControlSetEnable(y, TRUE);
            }
            XuiSetTimer(obj, kInfoTimer, 2000);
        }
        XuiSetTimer(obj, kCslSelTimer, 300);
        msg->bHandled = TRUE;
        return S_OK;
    }
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kCslSelTimer)
    {
        XuiKillTimer(obj, kCslSelTimer);
        if (cls == L"dashClockSettings_TZone")
            SetSel(Child(obj, L"lstTimezone"), xconfig::CurrentZone());
        else if (cls == L"dashCountrySettings")
            SetSel(Child(obj, L"lstCountries"), xconfig::CurrentLocale() - 1);
        else if (cls == L"dashClockSettings")
            FillClock(obj);
        msg->bHandled = TRUE;
        return S_OK;
    }
    // System Info: A does nothing (its file's A was a factory reset); Y is
    // the power box. Its text is refreshed every 2 s, the edit's scroll kept.
    if (cls == L"dashSystemReset")
    {
        if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kInfoTimer)
        {
            PowerAnswered();
            if (HXUIOBJ info = Child(obj, L"edInfo"))
            {
                std::wstring now = SystemInfo();
                LPCWSTR was = XuiControlGetText(info);
                if (!was || now != was)
                    XuiControlSetText(info, now.c_str());
            }
            msg->bHandled = TRUE;
        }
        else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_A)
            msg->bHandled = TRUE;
        else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
        {
            std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
            if (id == L"btnY")
                AskPower();
            if (id == L"btnA" || id == L"btnY")
                msg->bHandled = TRUE;
        }
        return S_OK;
    }
    // The date and time: A sets the clock to what the spinners show.
    if (cls == L"dashClockSettings" && msg->dwMessage == XM_KEYDOWN &&
        ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_A)
    {
        ApplyClock(obj);
        msg->bHandled = TRUE;
        return S_OK;
    }
    // A on a list item: its press, or the key itself where the list sends no
    // press (the Locale list didn't: the test of 2026-10-14).
    HXUIOBJ source = NULL;
    if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS)
        source = ((XUINotify*)msg->pvData)->hObjSource;
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_A)
    {
        static const wchar_t* const kLists[] = { L"lstCountries", L"lstTimezone", L"listChannels", L"listOptions" };
        HXUIOBJ focus = XuiElementGetFocus();
        for (int i = 0; i < 4 && !source; i++)
            if (HXUIOBJ l = Child(obj, kLists[i]))
                if (FromWithin(focus, l))
                    source = focus;
    }
    if (!source)
        return S_OK;
    std::wstring id = ElementId(source);
    HXUIOBJ list = Child(obj, L"listOptions");
    bool handled = true;
    if (cls == L"dashAudioSettings_A" && FromWithin(source, list))
    {
        xconfig::SetAnalogAudio(ListPick(list));
        SetCurrent(obj, AnalogText());
    }
    else if (cls == L"dashAudioSettings_D" && FromWithin(source, list))
    {
        xconfig::SetDigitalAudio(ListPick(list));
        SetCurrent(obj, DigitalText());
    }
    else if (cls == L"dashAutoOff" && (id == L"btnOn" || id == L"btnOff"))
    {
        xconfig::SetAutoOff(id == L"btnOn");
        SetCurrent(obj, xconfig::IsAutoOffOn() ? L"Enabled" : L"Disabled");
    }
    else if (cls == L"dashScreensaver" && (id == L"btnOn" || id == L"btnOff"))
    {
        xconfig::SetScreensaver(id == L"btnOn");
        SetCurrent(obj, xconfig::IsScreensaverOn() ? L"Enabled" : L"Disabled");
    }
    else if ((cls == L"dashClockSettings_1224" || cls == L"dashClockSettings_DSaving") && FromWithin(source, list))
    {
        bool dst = cls == L"dashClockSettings_DSaving";
        int pick = ListPick(list);
        DWORD bit = dst ? xconfig::kRetailDstOff : xconfig::kRetail24HourClock;
        DWORD f = xconfig::ReadU32(xconfig::kUser, xconfig::kRetailFlags);
        DWORD changed = pick == 1 ? (f | bit) : (f & ~bit);
        if (changed != f)
            xconfig::Write(xconfig::kUser, xconfig::kRetailFlags, &changed, sizeof(changed));
        SetCurrent(obj, dst ? (pick == 1 ? L"Off" : L"On") : (pick == 1 ? L"24-Hour Clock" : L"12-Hour Clock"));
    }
    else if (cls == L"dashClockSettings_TZone" && FromWithin(source, Child(obj, L"lstTimezone")))
    {
        xconfig::SetZone(ListPick(Child(obj, L"lstTimezone")));
        SetCurrent(obj, xconfig::TimeZoneText());
    }
    else if (cls == L"dashCountrySettings" && FromWithin(source, Child(obj, L"lstCountries")))
    {
        // (the list leaves out "Unknown": item i is locale i + 1)
        int locale = ListPick(Child(obj, L"lstCountries")) + 1;
        bool ok = xconfig::SetLocale(locale);
        Log("console settings: locale %d: %s", locale, ok ? "written" : "not written");
        if (ok)
            SetCurrent(obj, xconfig::LocaleName(locale));
    }
    else if (cls == L"dashRemoteC" && FromWithin(source, Child(obj, L"listChannels")))
    {
        xconfig::SetRemote(ListPick(Child(obj, L"listChannels")));
        SetCurrent(obj, xconfig::RemoteName(xconfig::RemoteIndex()));
    }
    else if ((cls == L"dashVideoSettings_HD" || cls == L"dashVideoSettings_60") && FromWithin(source, list))
        SayBox(L"Display Settings", L"Not Available");      // (shown only: see the top)
    else if (cls == L"dashVideoSettings_Format" && (id == L"btnNormal" || id == L"btnWide"))
        SayBox(L"Screen Format", L"Not Available");
    else
        handled = false;
    if (handled)
        msg->bHandled = TRUE;
    return S_OK;
}

const StandInInfo kCslScenes[] = {
    { L"dashAudioSettings", L"DashScene", 0, { 0 } },
    { L"dashAudioSettings_A", L"XuiScene", 0, { 0 } },
    { L"dashAudioSettings_D", L"XuiScene", 0, { 0 } },
    { L"dashAutoOff", L"XuiScene", 0, { 0 } },
    { L"dashScreensaver", L"XuiScene", 0, { 0 } },
    { L"dashClockSettings_Menu", L"DashScene", 0, { 0 } },
    { L"dashClockSettings_1224", L"XuiScene", 0, { 0 } },
    { L"dashClockSettings_DSaving", L"XuiScene", 0, { 0 } },
    { L"dashClockSettings_TZone", L"XuiScene", 0, { 0 } },
    { L"dashClockSettings", L"XuiScene", 0, { 0 } },
    { L"dashCountrySettings", L"XuiScene", 0, { 0 } },
    { L"dashRemoteC", L"XuiScene", 0, { 0 } },
    { L"dashVideoSettings", L"DashScene", 0, { 0 } },
    { L"dashVideoSettings_Format", L"DashScene", 0, { 0 } },
    { L"dashVideoSettings_HD", L"XuiScene", 0, { 0 } },
    { L"dashVideoSettings_60", L"XuiScene", 0, { 0 } },
    { L"dashSystemReset", L"XuiScene", 0, { 0 } },
};
const StandInInfo kCslLists[] = {
    { L"dashCTimezone", L"XuiList", 0, { 0 } },
    { L"dashCCountry", L"XuiList", 0, { 0 } },
    { L"dashRemoteCList", L"XuiList", 0, { 0 } },
    { L"dashCScreenSize", L"XuiList", 0, { 0 } },
    { L"dashCValueSpin", L"XuiList", 0, { 0 } },
};

void RegisterConsoleSettings1888()
{
    int ok = 0, n = 0;
    for (size_t i = 0; i < sizeof(kCslScenes) / sizeof(kCslScenes[0]); i++, n++)
        if (RegisterStandInWithProc(kCslScenes[i], CslSceneProc))
            ok++;
        else
            Log("1888 %S: FAILED", kCslScenes[i].name);
    for (size_t i = 0; i < sizeof(kCslLists) / sizeof(kCslLists[0]); i++, n++)
        if (RegisterStandInWithProc(kCslLists[i], CslListProc))
            ok++;
        else
            Log("1888 %S: FAILED", kCslLists[i].name);
    Log("1888 console settings: %d of %d classes", ok, n);
}
