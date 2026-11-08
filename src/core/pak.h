// SlopDash theme packs (.pak, "SDPK"): one file holding everything a theme
// reads from the user's dashboard files, already out of their .xex modules, so
// the console reads one file and decrypts nothing (the SlopDash Importer's
// tools/themepak.py writes them).
//
//   "SDPK", u32 version (1), u32 count, then per entry: u8 name length, the
//   name (ASCII), u32 offset (from the file's start), u32 size; then the data,
//   each entry 16-byte aligned. All big-endian.
//
// Blades' names (themepak.py): "<module>/<resource>" for each 1888 module's
// resource packages ("dash/dashmain", "hud/hud"), "6717/<resource>" for the
// three 6717 packages, and the files "dashmain5.xzp" and "xenonclatin.xtt".
//
// Portable C++ (XDK compiler and g++).
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace pak {

const unsigned kVersion = 1;

struct Entry
{
    std::string name;
    unsigned offset;        // from the start of the pack
    unsigned size;
};

// The pack's entries; false (and `error`) if it isn't a well-formed pack of
// this version.
bool List(const unsigned char* data, size_t size, std::vector<Entry>& out, std::string& error);

// The entry named `name`, or NULL.
const Entry* Find(const std::vector<Entry>& list, const std::string& name);

}  // namespace pak
