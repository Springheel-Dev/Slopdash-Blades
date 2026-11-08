// Minimal HTTP/1.1. Portable: building requests and parsing responses; the
// console does the socket I/O (plain, or TLS: platform/https.cpp).
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace http {

struct Url
{
    std::string host;
    int port;
    std::string path;       // including the query
    bool secure;            // https
};

bool ParseUrl(const std::string& url, Url& out);

// "GET <path> HTTP/1.1" with Host, a user agent and Connection: close.
std::string Request(const Url& url);

// A POST of a form (application/x-www-form-urlencoded), with its own user
// agent (a service may ask for one in its own form: RetroAchievements).
std::string PostRequest(const Url& url, const std::string& form, const std::string& userAgent);

// Text made safe for a form or a query (RFC 3986's unreserved kept, the rest %XX).
std::string Escape(const std::string& text);

// Splits a complete response (read until the server closed the connection)
// into status and body, undoing chunked transfer encoding.
bool ParseResponse(const std::vector<unsigned char>& raw, int& status, std::vector<unsigned char>& body,
                   std::string& error);

// A response header's value (the first; name in any case), or "".
std::string Header(const std::vector<unsigned char>& raw, const std::string& name);

}  // namespace http
