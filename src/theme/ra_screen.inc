// RetroAchievements (platform/retroachievements.h): the sign-in and the
// account, from the Xbox Live blade (its Connect to Xbox Live / Join Xbox
// Live, the one online service SlopDash signs in to). A list screen on the
// Language screen's layout, as the store's.
//
// Included by system_settings.inc, after the list screens.

class RetroAchievementsScreen : public ListScreen
{
public:
    RetroAchievementsScreen() : m_stage(kIdle), m_confirmAt(0)
    {
        ZeroMemory(&m_ov, sizeof(m_ov));
        m_text[0] = 0;
        if (ra::Current().signedIn)
            ra::StartRefresh();         // (the points as they are now)
    }
    ~RetroAchievementsScreen()
    {
        SecureZeroMemory(m_text, sizeof(m_text));
    }
    const wchar_t* Header() { return L"RetroAchievements"; }
    const wchar_t* CurrentHeading() { return L"Account"; }
    std::wstring Current()
    {
        ra::Account a = ra::Current();
        if (ra::Busy())
            return L"Signing In...";
        return a.signedIn ? Widen(a.user) : L"Not Signed In";
    }
    void Rebuild()
    {
        ra::Account a = ra::Current();
        m_rows.clear();
        items.clear();
        if (a.signedIn)
        {
            wchar_t b[96];
            if (a.points >= 0)
                _snwprintf_s(b, 96, _TRUNCATE, L"%d Points (%d Softcore)", a.points, a.softcorePoints < 0 ? 0 : a.softcorePoints);
            else
                wcscpy_s(b, L"Points");
            Add(kPoints, b);
        }
        else
            Add(kSignIn, L"Sign In");
        Add(kEmulators, L"Emulators");
        if (a.signedIn)
            Add(kSignOut, L"Sign Out");
    }
    std::wstring About(int i)
    {
        switch (Row(i))
        {
        case kSignIn:
            return L"Sign in with your RetroAchievements account (retroachievements.org). SlopDash keeps a sign-in "
                   L"token on this console, never your password.";
        case kPoints:
            return L"Your hardcore and softcore points. A: update them.";
        case kEmulators:
            return L"No Xbox 360 emulator supports RetroAchievements yet. Your sign-in stays ready for one that does.";
        case kSignOut:
            return L"Sign out: the token is removed from this console.";
        default:
            return L"";
        }
    }
    void Press(HXUIOBJ scene, int i)
    {
        if (m_stage != kIdle || ra::Busy())
            return;
        switch (Row(i))
        {
        case kSignIn:
            Type(scene, kTypingUser);
            break;
        case kPoints:
            if (ra::StartRefresh())
                SetStatus(scene, L"Updating...");
            break;
        case kSignOut:
            if (GetTickCount() - m_confirmAt > 5000)
            {
                m_confirmAt = GetTickCount();
                SetStatus(scene, L"Press A again to sign out.");
                return;
            }
            m_confirmAt = 0;
            ra::SignOut();
            g_generation++;
            SetStatus(scene, L"Signed out.");
            break;
        default:
            break;
        }
    }
    void Tick(HXUIOBJ scene)
    {
        std::wstring outcome = ra::TakeOutcome();
        if (!outcome.empty())
        {
            g_generation++;
            SetStatus(scene, outcome);
        }
        if (m_stage == kIdle || !XHasOverlappedIoCompleted(&m_ov))
            return;
        bool typed = XGetOverlappedExtendedError(&m_ov) == ERROR_SUCCESS && m_text[0];
        if (m_stage == kTypingUser)
        {
            m_user.clear();
            for (const wchar_t* c = m_text; *c; c++)
                m_user += *c < 128 ? (char)*c : '?';
            m_stage = kIdle;
            if (typed)
                Type(scene, kTypingPassword);
            return;
        }
        // The password: straight to the sign-in, then wiped.
        std::string password;
        for (const wchar_t* c = m_text; *c; c++)
            password += *c < 128 ? (char)*c : '?';
        SecureZeroMemory(m_text, sizeof(m_text));
        m_stage = kIdle;
        if (typed && ra::StartSignIn(m_user, password))
            SetStatus(scene, L"Signing in...");
        if (!password.empty())
            SecureZeroMemory(&password[0], password.size());
    }

private:
    enum RowKind { kSignIn, kPoints, kEmulators, kSignOut };
    enum Stage { kIdle, kTypingUser, kTypingPassword };

    void Add(RowKind kind, const std::wstring& text)
    {
        m_rows.push_back(kind);
        items.push_back(text);
    }
    int Row(int i) { return i >= 0 && i < (int)m_rows.size() ? m_rows[i] : -1; }

    void Type(HXUIOBJ scene, Stage stage)
    {
        ZeroMemory(&m_ov, sizeof(m_ov));
        SecureZeroMemory(m_text, sizeof(m_text));
        int user = SignedInUser();
        bool name = stage == kTypingUser;
        std::wstring last = Widen(GetSetting("ra.lastuser"));
        if (name)
            wcsncpy_s(m_text, last.c_str(), _TRUNCATE);
        DWORD r = ShowKeyboard(user < 0 ? 0 : user, name ? VKBD_DEFAULT : VKBD_LATIN_PASSWORD,
                                  name ? last.c_str() : L"", L"RetroAchievements",
                                  name ? L"Your RetroAchievements username." : L"Your RetroAchievements password.",
                                  m_text, ARRAYSIZE(m_text), &m_ov);
        if (r == ERROR_IO_PENDING || r == ERROR_SUCCESS)
            m_stage = stage;
        else
            SetStatus(scene, L"The keyboard couldn't be opened.");
    }

    std::vector<int> m_rows;
    Stage m_stage;
    XOVERLAPPED m_ov;
    wchar_t m_text[256];
    std::string m_user;
    DWORD m_confirmAt;
};
