#include "core/http.h"

#include <stdlib.h>
#include <string.h>

namespace http {

namespace {

std::string Lower(const std::string& s)
{
    std::string out(s);
    for (size_t i = 0; i < out.size(); i++)
        if (out[i] >= 'A' && out[i] <= 'Z')
            out[i] = (char)(out[i] + 32);
    return out;
}

}  // namespace

bool ParseUrl(const std::string& url, Url& out)
{
    out.secure = url.compare(0, 8, "https://") == 0;
    if (!out.secure && url.compare(0, 7, "http://") != 0)
        return false;
    size_t start = out.secure ? 8 : 7;
    size_t slash = url.find('/', start);
    std::string hostport = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    out.path = slash == std::string::npos ? "/" : url.substr(slash);
    size_t colon = hostport.find(':');
    out.host = hostport.substr(0, colon);
    out.port = colon == std::string::npos ? (out.secure ? 443 : 80) : atoi(hostport.c_str() + colon + 1);
    return !out.host.empty() && out.port > 0;
}

// The user agent: ConsoleMods' Cloudflare lets through clients naming Aurora
// (its Homebrew Store) and challenges the rest; SlopDash reads the same
// repos, and says so.
std::string Request(const Url& url)
{
    return "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host +
           "\r\nUser-Agent: SlopDash (Aurora compatible)\r\nAccept: */*\r\nConnection: close\r\n\r\n";
}

std::string PostRequest(const Url& url, const std::string& form, const std::string& userAgent)
{
    char length[24];
    _snprintf(length, sizeof(length), "%u", (unsigned)form.size());
    return "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + "\r\nUser-Agent: " + userAgent +
           "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " + length +
           "\r\nAccept: */*\r\nConnection: close\r\n\r\n" + form;
}

std::string Escape(const std::string& text)
{
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < text.size(); i++)
    {
        unsigned char c = (unsigned char)text[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
            out += (char)c;
        else
        {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        }
    }
    return out;
}

bool ParseResponse(const std::vector<unsigned char>& raw, int& status, std::vector<unsigned char>& body,
                   std::string& error)
{
    body.clear();
    status = 0;
    const char* d = raw.empty() ? "" : (const char*)&raw[0];
    const size_t n = raw.size();
    size_t headerEnd = std::string::npos;
    for (size_t i = 0; i + 3 < n; i++)
        if (d[i] == '\r' && d[i + 1] == '\n' && d[i + 2] == '\r' && d[i + 3] == '\n')
        {
            headerEnd = i;
            break;
        }
    if (headerEnd == std::string::npos)
    {
        error = "no end of headers";
        return false;
    }
    std::string headers(d, headerEnd);
    if (headers.compare(0, 5, "HTTP/") != 0 || headers.size() < 12)
    {
        error = "not an HTTP response";
        return false;
    }
    status = atoi(headers.c_str() + 9);
    std::string lower = Lower(headers);
    bool chunked = lower.find("\r\ntransfer-encoding: chunked") != std::string::npos;
    size_t p = headerEnd + 4;
    if (!chunked)
    {
        body.assign(raw.begin() + p, raw.end());
        size_t cl = lower.find("\r\ncontent-length:");
        if (cl != std::string::npos)
        {
            size_t len = (size_t)atol(lower.c_str() + cl + 17);
            if (body.size() > len)
                body.resize(len);
            else if (body.size() < len)
            {
                error = "body shorter than Content-Length";
                return false;
            }
        }
        return true;
    }
    for (;;)
    {
        size_t eol = p;
        while (eol + 1 < n && !(d[eol] == '\r' && d[eol + 1] == '\n'))
            eol++;
        if (eol + 1 >= n)
        {
            error = "truncated chunk header";
            return false;
        }
        size_t size = (size_t)strtoul(std::string(d + p, eol - p).c_str(), NULL, 16);
        p = eol + 2;
        if (size == 0)
            return true;
        if (p + size > n)
        {
            error = "truncated chunk";
            return false;
        }
        body.insert(body.end(), raw.begin() + p, raw.begin() + p + size);
        p += size + 2;                      // the chunk's trailing CRLF
    }
}

std::string Header(const std::vector<unsigned char>& raw, const std::string& name)
{
    std::string text(raw.begin(), raw.end());
    size_t end = text.find("\r\n\r\n");
    text = text.substr(0, end);
    std::string lower = Lower(text);
    std::string want = "\r\n" + Lower(name) + ":";
    size_t at = lower.find(want);
    if (at == std::string::npos)
        return "";
    at += want.size();
    size_t eol = lower.find("\r\n", at);
    // (the value from the original text, not lowered)
    std::string value = text.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
    size_t a = value.find_first_not_of(" \t");
    size_t b = value.find_last_not_of(" \t");
    return a == std::string::npos ? "" : value.substr(a, b - a + 1);
}

}  // namespace http

