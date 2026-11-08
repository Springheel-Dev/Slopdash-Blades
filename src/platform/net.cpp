#include "platform/net.h"

#include <xtl.h>
#include <winsockx.h>
#include <stdio.h>

#include "core/http.h"
#include "platform/log.h"

namespace {

bool g_started = false, g_ok = false;
volatile LONG g_lastResult = 0;

bool Resolve(const std::string& host, IN_ADDR& out)
{
    XNDNS* dns = NULL;
    WSAEVENT ev = WSACreateEvent();
    int r = XNetDnsLookup(host.c_str(), ev, &dns);
    if (r != 0 || !dns)
    {
        WSACloseEvent(ev);
        return false;
    }
    WaitForSingleObject(ev, 10000);
    bool ok = dns->iStatus == 0 && dns->cina > 0;
    if (ok)
        out = dns->aina[0];
    XNetDnsRelease(dns);
    WSACloseEvent(ev);
    return ok;
}

}  // namespace

bool NetStartup()
{
    if (g_started)
        return g_ok;
    g_started = true;
    // Ordinary internet servers: the Xbox stack's own security must be bypassed.
    XNetStartupParams xnsp;
    memset(&xnsp, 0, sizeof(xnsp));
    xnsp.cfgSizeOfStruct = sizeof(xnsp);
    xnsp.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
    int r = XNetStartup(&xnsp);
    WSADATA wsa;
    int w = WSAStartup(MAKEWORD(2, 2), &wsa);
    g_ok = r == 0 && w == 0;
    Log("network: XNetStartup %d, WSAStartup %d", r, w);
    return g_ok;
}

bool NetConnect(const std::string& host, int port, unsigned& socketOut, std::string& error)
{
    if (!NetStartup())
    {
        error = "no network";
        return false;
    }
    IN_ADDR addr;
    if (!Resolve(host, addr))
    {
        error = "DNS lookup failed for " + host;
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        error = "socket failed";
        return false;
    }
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, 0x5801, (const char*)&on, sizeof(on));   // insecure (plain internet) socket
    DWORD timeout = 15000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)port);
    sa.sin_addr = addr;
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) != 0)
    {
        closesocket(s);
        error = "connect failed";
        return false;
    }
    socketOut = (unsigned)s;
    return true;
}

void NetClose(unsigned s)
{
    closesocket((SOCKET)s);
}

int LastRequestResult()
{
    return (int)g_lastResult;
}

// The response arrived (any status counts: the server was reached).
static void Reached(bool ok)
{
    InterlockedExchange(&g_lastResult, ok ? 1 : -1);
}

bool HttpGet(const std::string& url, std::vector<unsigned char>& body, std::string& error, size_t maxBytes)
{
    body.clear();
    http::Url u;
    if (!http::ParseUrl(url, u))
    {
        error = "bad url";
        return false;
    }
    if (!NetStartup())
    {
        error = "no network";
        return false;
    }
    IN_ADDR addr;
    if (!Resolve(u.host, addr))
    {
        error = "DNS lookup failed for " + u.host;
        Reached(false);
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
    {
        error = "socket failed";
        return false;
    }
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, 0x5801, (const char*)&on, sizeof(on));   // insecure (plain internet) socket
    DWORD timeout = 15000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u_short)u.port);
    sa.sin_addr = addr;
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) != 0)
    {
        closesocket(s);
        error = "connect failed";
        Reached(false);
        return false;
    }
    std::string req = http::Request(u);
    if (send(s, req.c_str(), (int)req.size(), 0) != (int)req.size())
    {
        closesocket(s);
        error = "send failed";
        return false;
    }
    std::vector<unsigned char> raw;
    char buf[8192];
    for (;;)
    {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        raw.insert(raw.end(), buf, buf + n);
        if (raw.size() > maxBytes + 64 * 1024)
        {
            closesocket(s);
            error = "response too large";
            return false;
        }
    }
    closesocket(s);
    int status = 0;
    if (!http::ParseResponse(raw, status, body, error))
    {
        Reached(false);
        return false;
    }
    Reached(true);
    if (status != 200)
    {
        char b[48];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "HTTP %d", status);
        error = b;
        body.clear();
        return false;
    }
    return true;
}
