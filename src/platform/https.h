// HTTP and HTTPS GETs from the console: plain sockets, or TLS with BearSSL
// (the console's own TLS fails with today's servers). Redirects are
// followed. Blocking: call them from a worker thread.
#pragma once

#include <string>
#include <vector>

// GET url (http or https); true with the body of a 2xx response. status is
// the HTTP status (0 when none came back); error says what failed.
bool HttpsGet(const std::string& url, std::vector<unsigned char>& body, int& status, std::string& error,
              size_t maxBytes = 4 * 1024 * 1024);

// POST a form (application/x-www-form-urlencoded) with the given user agent;
// true with the body of a 2xx response. No redirects are followed.
bool HttpsPost(const std::string& url, const std::string& form, const std::string& userAgent,
               std::vector<unsigned char>& body, int& status, std::string& error, size_t maxBytes = 256 * 1024);

// Called as a download arrives (total 0: not known); false cancels.
typedef bool (*DownloadProgress)(void* context, unsigned long long done, unsigned long long total);

// GET url straight into the file at path (any size; nothing kept in memory).
// The file is removed if the download fails.
bool HttpsDownload(const std::string& url, const std::string& path, DownloadProgress progress, void* context,
                   int& status, std::string& error);

