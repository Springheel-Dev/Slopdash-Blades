// Plain-http GETs from the console (XNet sockets).
#pragma once

#include <string>
#include <vector>

// Starts the network stack (once). False if it couldn't.
bool NetStartup();

// A TCP connection to host:port (DNS, the plain-internet socket option,
// 15 s timeouts); the socket, or false (error says why).
bool NetConnect(const std::string& host, int port, unsigned& socketOut, std::string& error);
void NetClose(unsigned socket);

// The outcome of the most recent request: 0 none yet, 1 succeeded, -1 failed.
int LastRequestResult();

// GET url; the body of a 200 response, or false (error says why). Blocking:
// call it from a worker thread. maxBytes caps the response.
bool HttpGet(const std::string& url, std::vector<unsigned char>& body, std::string& error,
             size_t maxBytes = 2 * 1024 * 1024);
