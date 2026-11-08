#include "platform/retroachievements.h"

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/http.h"
#include "core/json.h"
#include "platform/https.h"
#include "platform/log.h"
#include "platform/settings.h"

namespace ra {

namespace {

const char kConnect[] = "https://retroachievements.org/dorequest.php";
// RetroAchievements asks every caller for a user agent, in the form
// "{name}/{x.y.z version} ({platform})" (its Standalones Integration Guide).
// The version is SlopDash.xex.xml's.
const char kUserAgent[] = "SlopDash/0.0.80 (Xbox 360)";

volatile LONG g_busy = 0;
CRITICAL_SECTION g_lock;
struct LockInit
{
    LockInit() { InitializeCriticalSection(&g_lock); }
} g_lockInit;
std::wstring g_outcome;

struct Job
{
    std::string user, secret;
    bool withToken;                 // secret is the token (a refresh), else the password
};

void SetOutcome(const std::wstring& text)
{
    EnterCriticalSection(&g_lock);
    g_outcome = text;
    LeaveCriticalSection(&g_lock);
}

std::string IntText(int v)
{
    char b[16];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%d", v);
    return b;
}

DWORD WINAPI LoginThread(LPVOID p)
{
    Job* job = (Job*)p;
    std::string form = "r=login2&u=" + http::Escape(job->user) + (job->withToken ? "&t=" : "&p=") +
                       http::Escape(job->secret);
    // (the password, or the form holding it, kept no longer than needed)
    SecureZeroMemory(&job->secret[0], job->secret.size());
    std::vector<unsigned char> body;
    int status = 0;
    std::string error;
    bool ok = HttpsPost(kConnect, form, kUserAgent, body, status, error);
    SecureZeroMemory(&form[0], form.size());
    json::Value v;
    std::string jsonError;
    bool parsed = !body.empty() && json::Parse((const char*)&body[0], body.size(), v, jsonError);
    const json::Value* success = parsed ? v.Get("Success") : NULL;
    Log("retroachievements: login2 for %s (%s): HTTP %d %s, %s", job->user.c_str(), job->withToken ? "token" : "password",
        status, ok ? "ok" : error.c_str(), success && success->Truth() ? "success" : "no");
    if (success && success->Truth() && v.Get("Token"))
    {
        std::wstring wuser = v.Get("User") ? v.Get("User")->Text() : std::wstring(job->user.begin(), job->user.end());
        std::wstring wtoken = v.Get("Token")->Text();
        std::string user(wuser.begin(), wuser.end()), token(wtoken.begin(), wtoken.end());
        int points = v.Get("Score") ? (int)v.Get("Score")->n : -1;
        int soft = v.Get("SoftcoreScore") ? (int)v.Get("SoftcoreScore")->n : -1;
        SetSetting("ra.user", user);
        SetSetting("ra.lastuser", user);        // (offered next time; not a secret)
        SetSetting("ra.token", token);
        SetSetting("ra.points", IntText(points));
        SetSetting("ra.softcore", IntText(soft));
        wchar_t b[96];
        _snwprintf_s(b, 96, _TRUNCATE, L"Signed in as %s. %d points.", wuser.c_str(), points);
        SetOutcome(b);
    }
    else if (success)
    {
        // RetroAchievements' own reason ("Invalid user/password combination..."), short
        const json::Value* why = v.Get("Error");
        std::wstring text = why ? why->Text() : L"";
        SetOutcome(text.empty() ? L"Cannot Sign In" : L"Cannot Sign In. " + text);
        if (job->withToken && status == 401)
            SignOut();                  // (the token no longer works)
    }
    else if (status == 401 || status == 403)
    {
        // (a refused sign-in comes back as 401, its reason in a body the
        // HTTPS layer keeps only for 2xx answers)
        SetOutcome(job->withToken ? L"Signed Out. Sign In Again" : L"Cannot Sign In. Wrong Username or Password");
        if (job->withToken)
            SignOut();
    }
    else
        SetOutcome(status ? L"Cannot Sign In" : L"Cannot Connect");
    delete job;
    InterlockedExchange(&g_busy, 0);
    return 0;
}

bool Start(Job* job)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0)
    {
        delete job;
        return false;
    }
    SetOutcome(L"");
    HANDLE t = CreateThread(NULL, 0, LoginThread, job, 0, NULL);
    if (!t)
    {
        delete job;
        InterlockedExchange(&g_busy, 0);
        return false;
    }
    CloseHandle(t);
    return true;
}

}  // namespace

Account Current()
{
    Account a;
    a.user = GetSetting("ra.user");
    a.token = GetSetting("ra.token");
    a.signedIn = !a.user.empty() && !a.token.empty();
    std::string p = GetSetting("ra.points"), s = GetSetting("ra.softcore");
    a.points = p.empty() ? -1 : atoi(p.c_str());
    a.softcorePoints = s.empty() ? -1 : atoi(s.c_str());
    return a;
}

bool StartSignIn(const std::string& user, const std::string& password)
{
    if (user.empty() || password.empty())
        return false;
    Job* job = new Job;
    job->user = user;
    job->secret = password;
    job->withToken = false;
    return Start(job);
}

bool StartRefresh()
{
    Account a = Current();
    if (!a.signedIn)
        return false;
    Job* job = new Job;
    job->user = a.user;
    job->secret = a.token;
    job->withToken = true;
    return Start(job);
}

bool Busy()
{
    return g_busy != 0;
}

std::wstring TakeOutcome()
{
    EnterCriticalSection(&g_lock);
    std::wstring o = g_outcome;
    g_outcome.clear();
    LeaveCriticalSection(&g_lock);
    return o;
}

#ifdef SLOPDASH_LOCAL
#include "../../local/ra_probe.inc"
#endif

void SignOut()
{
    SetSetting("ra.user", "");
    SetSetting("ra.token", "");
    SetSetting("ra.points", "");
    SetSetting("ra.softcore", "");
    Log("retroachievements: signed out");
}

}  // namespace ra
