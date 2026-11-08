// SlopDash's own settings: game:\SlopDash.ini, one "key=value" per line.
// Lists are numbered keys: "<prefix>.1", "<prefix>.2"...
#pragma once

#include <string>
#include <vector>

std::string GetSetting(const char* key);            // "" if absent
bool SetSetting(const char* key, const std::string& value);   // "" removes it

// A list; `exists` says whether the file has one at all (an empty list counts).
std::vector<std::string> GetSettingList(const char* prefix, bool* exists = 0);
bool SetSettingList(const char* prefix, const std::vector<std::string>& values);
