#include "platform/https.h"

#include <xtl.h>
#include <winsockx.h>
#include <stdio.h>
#include <string.h>

#pragma warning(push)
#pragma warning(disable : 4244)     // (its headers' inline setters narrow on purpose)
extern "C" {
#include "bearssl.h"
}
#pragma warning(pop)

#include "core/http.h"
#include "platform/log.h"
#include "platform/net.h"

// HTTPS with BearSSL (third_party/bearssl): the console's own TLS (xhttp.lib,
// XHTTP_FLAG_SECURE) fails the handshake with today's servers (80090304
// from consolemods.org, 800704CD from GitHub; 2026-10-12). Certificates are
// checked against Mozilla's root store (trust_anchors.inc).

namespace {

struct RootKey
{
    const unsigned char* dn;
    size_t dnLen;
    int curve;                      // 0: RSA
    const unsigned char* key;       // RSA modulus or EC point
    size_t keyLen;
    const unsigned char* e;         // RSA exponent
    size_t eLen;
};

#include "platform/trust_anchors.inc"

const std::vector<br_x509_trust_anchor>& Anchors()
{
    static std::vector<br_x509_trust_anchor> tas;
    if (tas.empty())
    {
        const size_t n = sizeof(kRoots) / sizeof(kRoots[0]);
        tas.resize(n);
        for (size_t i = 0; i < n; i++)
        {
            br_x509_trust_anchor& ta = tas[i];
            memset(&ta, 0, sizeof(ta));
            ta.dn.data = (unsigned char*)kRoots[i].dn;
            ta.dn.len = kRoots[i].dnLen;
            ta.flags = BR_X509_TA_CA;
            if (kRoots[i].curve == 0)
            {
                ta.pkey.key_type = BR_KEYTYPE_RSA;
                ta.pkey.key.rsa.n = (unsigned char*)kRoots[i].key;
                ta.pkey.key.rsa.nlen = kRoots[i].keyLen;
                ta.pkey.key.rsa.e = (unsigned char*)kRoots[i].e;
                ta.pkey.key.rsa.elen = kRoots[i].eLen;
            }
            else
            {
                ta.pkey.key_type = BR_KEYTYPE_EC;
                ta.pkey.key.ec.curve = kRoots[i].curve;
                ta.pkey.key.ec.q = (unsigned char*)kRoots[i].key;
                ta.pkey.key.ec.qlen = kRoots[i].keyLen;
            }
        }
    }
    return tas;
}

// Days since 1 January of year 0 (proleptic Gregorian), as BearSSL counts.
unsigned DaysSinceYear0(int y, unsigned m, unsigned d)
{
    static const unsigned kBefore[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    unsigned days = (unsigned)y * 365 + (unsigned)((y + 3) / 4 - (y + 99) / 100 + (y + 399) / 400);   // years 0..y-1
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    days += kBefore[m - 1] + (leap && m > 2 ? 1 : 0) + (d - 1);
    return days;
}

int g_lastSockError = 0;    // (the last recv's: 0 the server closed it, else WSAGetLastError's)

int SockRead(void* ctx, unsigned char* buf, size_t len)
{
    int n = recv((SOCKET) * (unsigned*)ctx, (char*)buf, (int)len, 0);
    if (n <= 0)
        g_lastSockError = n == 0 ? 0 : WSAGetLastError();
    return n <= 0 ? -1 : n;
}

int SockWrite(void* ctx, const unsigned char* buf, size_t len)
{
    int n = send((SOCKET) * (unsigned*)ctx, (const char*)buf, (int)len, 0);
    return n <= 0 ? -1 : n;
}

// A connection: plain, or TLS over it.
class Conn
{
public:
    Conn() : m_sock(0), m_open(false), m_tls(NULL) {}
    ~Conn() { Close(); }

    bool Open(const http::Url& u, std::string& error)
    {
        if (!NetConnect(u.host, u.port, m_sock, error))
            return false;
        m_open = true;
        if (!u.secure)
            return true;
        m_tls = new Tls;    // (on the heap: the client context and its buffer are some 40 KB)
        const std::vector<br_x509_trust_anchor>& tas = Anchors();
        br_ssl_client_init_full(&m_tls->cc, &m_tls->xc, &tas[0], tas.size());
        SYSTEMTIME now;
        GetSystemTime(&now);
        br_x509_minimal_set_time(&m_tls->xc, DaysSinceYear0(now.wYear, now.wMonth, now.wDay),
                                 now.wHour * 3600u + now.wMinute * 60u + now.wSecond);
        unsigned char seed[32];
        XNetRandom(seed, sizeof(seed));
        br_ssl_engine_inject_entropy(&m_tls->cc.eng, seed, sizeof(seed));
        br_ssl_engine_set_buffer(&m_tls->cc.eng, m_tls->iobuf, sizeof(m_tls->iobuf), 1);
        br_ssl_client_reset(&m_tls->cc, u.host.c_str(), 0);
        br_sslio_init(&m_tls->io, &m_tls->cc.eng, SockRead, &m_sock, SockWrite, &m_sock);
        return true;
    }

    bool WriteAll(const std::string& s, std::string& error)
    {
        if (m_tls)
        {
            if (br_sslio_write_all(&m_tls->io, s.c_str(), s.size()) == 0 && br_sslio_flush(&m_tls->io) == 0)
                return true;
            error = TlsError();
            return false;
        }
        if (send((SOCKET)m_sock, s.c_str(), (int)s.size(), 0) == (int)s.size())
            return true;
        error = "send failed";
        return false;
    }

    // Bytes read; 0 at the end of the response; -1 on an error.
    int Read(unsigned char* buf, size_t len)
    {
        if (m_tls)
        {
            int n = br_sslio_read(&m_tls->io, buf, len);
            if (n > 0)
                return n;
            int err = br_ssl_engine_last_error(&m_tls->cc.eng);
            // (a server that just closes the socket ends the response too)
            return err == BR_ERR_OK || err == BR_ERR_IO ? 0 : -1;
        }
        int n = recv((SOCKET)m_sock, (char*)buf, (int)len, 0);
        return n < 0 ? -1 : n;
    }

    std::string TlsError()
    {
        char b[48];
        _snprintf_s(b, sizeof(b), _TRUNCATE, "TLS error %d", m_tls ? br_ssl_engine_last_error(&m_tls->cc.eng) : 0);
        return b;
    }

    void Close()
    {
        if (m_tls)
        {
            delete m_tls;
            m_tls = NULL;
        }
        if (m_open)
            NetClose(m_sock);
        m_open = false;
    }

private:
    struct Tls
    {
        br_ssl_client_context cc;
        br_x509_minimal_context xc;
        unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
        br_sslio_context io;
    };
    unsigned m_sock;
    bool m_open;
    Tls* m_tls;
};

// Where a response's body goes.
class BodySink
{
public:
    virtual ~BodySink() {}
    virtual bool Put(const unsigned char* data, size_t size, std::string& error) = 0;
    virtual void Reset() = 0;           // a redirect: what was put is dropped
};

class MemorySink : public BodySink
{
public:
    MemorySink(std::vector<unsigned char>& body, size_t max) : m_body(&body), m_max(max) {}
    bool Put(const unsigned char* data, size_t size, std::string& error)
    {
        if (m_body->size() + size > m_max)
        {
            error = "too large";
            return false;
        }
        m_body->insert(m_body->end(), data, data + size);
        return true;
    }
    void Reset() { m_body->clear(); }

private:
    std::vector<unsigned char>* m_body;
    size_t m_max;
};

class FileSink : public BodySink
{
public:
    FileSink(const std::string& path, DownloadProgress progress, void* context)
        : m_path(path), m_h(INVALID_HANDLE_VALUE), m_done(0), m_total(0), m_progress(progress), m_context(context),
          m_offset(0)
    {
    }
    ~FileSink() { Close(); }
    // (the total of the whole file: a resumed part's length plus what's there)
    void SetTotal(unsigned long long total) { m_total = total + m_offset; }
    unsigned long long Done() const { return m_done; }
    // Carry on after what's written: the next Put appends (a Range request's 206).
    void Resume()
    {
        Close();
        m_offset = m_done;
    }
    bool Put(const unsigned char* data, size_t size, std::string& error)
    {
        if (m_h == INVALID_HANDLE_VALUE)
        {
            m_h = CreateFileA(m_path.c_str(), GENERIC_WRITE, 0, NULL, m_offset ? OPEN_ALWAYS : CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
            if (m_h == INVALID_HANDLE_VALUE)
            {
                error = "couldn't create " + m_path;
                return false;
            }
            if (m_offset)
            {
                LARGE_INTEGER at;
                at.QuadPart = (LONGLONG)m_offset;
                SetFilePointerEx(m_h, at, NULL, FILE_BEGIN);
                SetEndOfFile(m_h);
            }
        }
        DWORD put = 0;
        if (!WriteFile(m_h, data, (DWORD)size, &put, NULL) || put != size)
        {
            error = "couldn't write the download (the drive may be full)";
            return false;
        }
        m_done += size;
        if (m_progress && !m_progress(m_context, m_done, m_total))
        {
            error = "cancelled";
            return false;
        }
        return true;
    }
    void Reset()
    {
        Close();
        m_done = 0;
        m_offset = 0;
    }
    void Close()
    {
        if (m_h != INVALID_HANDLE_VALUE)
            CloseHandle(m_h);
        m_h = INVALID_HANDLE_VALUE;
    }

private:
    std::string m_path;
    HANDLE m_h;
    unsigned long long m_done, m_total;
    DownloadProgress m_progress;
    void* m_context;
    unsigned long long m_offset;   // (where a resumed part starts)
};

std::string Lower(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = (char)(s[i] + 32);
    return s;
}

// One request (a GET unless another is given): the status, the headers, and
// the body into sink (only for a 2xx response).
bool Exchange(const http::Url& u, int& status, std::string& headers, BodySink& sink, FileSink* file,
              std::string& error, const std::string* request = NULL)
{
    Conn c;
    if (!c.Open(u, error) || !c.WriteAll(request ? *request : http::Request(u), error))
        return false;
    std::vector<unsigned char> buf(16384);
    std::string head;
    // Headers.
    size_t bodyStart = std::string::npos;
    std::vector<unsigned char> pending;
    for (;;)
    {
        int n = c.Read(&buf[0], buf.size());
        if (n < 0)
        {
            error = u.secure ? c.TlsError() : "receive failed";
            return false;
        }
        if (n == 0)
        {
            error = "no response";
            return false;
        }
        head.append((const char*)&buf[0], n);
        bodyStart = head.find("\r\n\r\n");
        if (bodyStart != std::string::npos)
            break;
        if (head.size() > 64 * 1024)
        {
            error = "headers too long";
            return false;
        }
    }
    pending.assign(head.begin() + bodyStart + 4, head.end());
    head.resize(bodyStart);
    status = 0;
    size_t sp = head.find(' ');
    if (sp != std::string::npos)
        status = atoi(head.c_str() + sp + 1);
    headers = head + "\r\n";
    if (status < 200 || status >= 300)
        return true;                    // (the caller decides: a redirect, or an error)
    std::string lower = Lower(headers);
    bool chunked = lower.find("\r\ntransfer-encoding: chunked") != std::string::npos;
    size_t cl = lower.find("\r\ncontent-length:");
    long long length = cl == std::string::npos ? -1 : _atoi64(lower.c_str() + cl + 17);
    if (file && length >= 0)
        file->SetTotal((unsigned long long)length);

    // Body: identity (to the length, or until the server closes) or chunked.
    long long got = 0;
    std::string chunkLine;
    unsigned long long chunkLeft = 0;
    enum { kSize, kData, kDataEnd, kDone } state = kSize;
    for (;;)
    {
        const unsigned char* d = pending.empty() ? NULL : &pending[0];
        size_t n = pending.size();
        size_t i = 0;
        if (!chunked)
        {
            size_t take = n;
            if (length >= 0 && got + (long long)take > length)
                take = (size_t)(length - got);
            if (take && !sink.Put(d, take, error))
                return false;
            got += take;
            if (length >= 0 && got >= length)
                return true;
        }
        else
            while (i < n && state != kDone)
            {
                if (state == kSize || state == kDataEnd)
                {
                    char ch = (char)d[i++];
                    if (ch != '\n')
                    {
                        if (ch != '\r')
                            chunkLine += ch;
                        continue;
                    }
                    if (state == kDataEnd)
                    {
                        chunkLine.clear();
                        state = kSize;
                        continue;
                    }
                    chunkLeft = strtoul(chunkLine.c_str(), NULL, 16);
                    chunkLine.clear();
                    state = chunkLeft ? kData : kDone;
                }
                else
                {
                    size_t take = (size_t)(n - i < chunkLeft ? n - i : chunkLeft);
                    if (!sink.Put(d + i, take, error))
                        return false;
                    i += take;
                    chunkLeft -= take;
                    if (chunkLeft == 0)
                        state = kDataEnd;
                }
            }
        if (chunked && state == kDone)
            return true;
        int r = c.Read(&buf[0], buf.size());
        if (r < 0)
        {
            error = u.secure ? c.TlsError() : "receive failed";
            return false;
        }
        if (r == 0)
        {
            if (chunked || (length >= 0 && got < length))
            {
                char b[96];
                _snprintf_s(b, sizeof(b), _TRUNCATE, "the connection dropped at %lld of %lld bytes (socket error %d)",
                            got, length, g_lastSockError);
                error = b;
                return false;
            }
            return true;
        }
        pending.assign(&buf[0], &buf[0] + r);
    }
}

// GET with redirects (up to 5).
bool Fetch(const std::string& url, BodySink& sink, FileSink* file, int& status, std::string& error)
{
    std::string at = url;
    status = 0;
    for (int hop = 0; hop < 5; hop++)
    {
        http::Url u;
        if (!http::ParseUrl(at, u))
        {
            error = "bad url: " + at;
            return false;
        }
        std::string headers;
        error.clear();
        if (!Exchange(u, status, headers, sink, file, error))
            return false;
        if (status >= 300 && status < 400)
        {
            // (found in a lowered copy, read from the original: paths are case-sensitive)
            size_t l = Lower(headers).find("\r\nlocation:");
            if (l == std::string::npos)
                break;
            size_t end = headers.find("\r\n", l + 2);
            std::string next = headers.substr(l + 11, end - (l + 11));
            size_t a = next.find_first_not_of(' ');
            next = a == std::string::npos ? "" : next.substr(a);
            if (next.empty())
                break;
            if (next[0] == '/')
                next = std::string(u.secure ? "https://" : "http://") + u.host + next;
            Log("https: %d -> %s", status, next.c_str());
            sink.Reset();
            at = next;
            continue;
        }
        break;
    }
    if (status >= 200 && status < 300)
        return true;
    char b[32];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "HTTP %d", status);
    error = b;
    return false;
}

}  // namespace

bool HttpsGet(const std::string& url, std::vector<unsigned char>& body, int& status, std::string& error,
              size_t maxBytes)
{
    body.clear();
    MemorySink sink(body, maxBytes);
    return Fetch(url, sink, NULL, status, error);
}

bool HttpsPost(const std::string& url, const std::string& form, const std::string& userAgent,
               std::vector<unsigned char>& body, int& status, std::string& error, size_t maxBytes)
{
    body.clear();
    status = 0;
    http::Url u;
    if (!http::ParseUrl(url, u))
    {
        error = "bad url: " + url;
        return false;
    }
    MemorySink sink(body, maxBytes);
    std::string headers, request = http::PostRequest(u, form, userAgent);
    if (!Exchange(u, status, headers, sink, NULL, error, &request))
        return false;
    if (status >= 200 && status < 300)
        return true;
    char b[32];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "HTTP %d", status);
    error = b;
    return false;
}

// The user's VBA360 download from ConsoleMods failed twice, 2026-11-02, "FAILED
// (200) the download was cut short": the connection dropped part way (the file
// itself came whole on the PC). Now a dropped download carries on from where it
// stopped (Range: bytes=<done>-, the server answering 206), up to 8 times.
bool HttpsDownload(const std::string& url, const std::string& path, DownloadProgress progress, void* context,
                   int& status, std::string& error)
{
    FileSink sink(path, progress, context);
    bool ok = Fetch(url, sink, &sink, status, error);
    for (int tries = 0; !ok && status >= 200 && status < 300 && sink.Done() > 0 && tries < 8 &&
                        error.find("connection dropped") != std::string::npos;
         tries++)
    {
        Log("https: %s: %s; resuming", url.c_str(), error.c_str());
        http::Url u;
        if (!http::ParseUrl(url, u))
            break;
        char range[48];
        _snprintf_s(range, sizeof(range), _TRUNCATE, "Range: bytes=%I64u-\r\n", sink.Done());
        std::string request = http::Request(u);
        request.insert(request.size() - 2, range);      // (before the blank line ending the headers)
        sink.Resume();
        std::string headers;
        error.clear();
        int rangeStatus = 0;
        ok = Exchange(u, rangeStatus, headers, sink, &sink, error, &request);
        if (ok && rangeStatus != 206)
        {
            // (no part given: whatever came isn't a continuation)
            error = rangeStatus == 200 ? "the server won't resume a download" : "the server refused to resume";
            ok = false;
            break;
        }
    }
    sink.Close();
    if (!ok)
        DeleteFileA(path.c_str());
    return ok;
}

#ifdef SLOPDASH_LOCAL
#include "../../local/https_probes.inc"
#endif
