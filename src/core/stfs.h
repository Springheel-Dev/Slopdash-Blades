// Files inside STFS packages (CON/LIVE/PIRS: profiles, saves, updates), and the
// gamer card numbers a profile's dashboard settings file (FFFE07D1.gpd) holds.
// Portable C++ (XDK compiler and g++); the layout as tools/stfs.py reads it.
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace stfs {

// The file `name` (case-insensitive, any folder) from the whole package in
// memory; false if it isn't there or the package isn't readable.
bool ReadFile(const unsigned char* data, size_t size, const char* name, std::vector<unsigned char>& out);

// Every file's name, for tests and logs.
bool ListFiles(const unsigned char* data, size_t size, std::vector<std::string>& names);

// From a GPD (XDBF): the gamer card's settings - gamerscore (0x10040006), games
// played (0x10040007) and achievements earned (0x10040008). -1 where absent.
bool GamerCardNumbers(const unsigned char* gpd, size_t size, int& gamerscore, int& played, int& earned);

}  // namespace stfs
