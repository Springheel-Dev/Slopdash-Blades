// Files inside a Games on Demand install (content type 0x7000: a header file
// and its "<name>.data" folder of Data0000, Data0001...): the disc image in an
// SVOD volume, its files in GDF. For the game's own default.xex (its title
// image and name, when the header has none). Portable C++ (XDK compiler and
// g++); the data files are read through the caller's function.
//
// SVOD: each data file is a master hash block (0x1000 bytes), then groups of
// a hash block and 0xCC data blocks, 0xA1C4 data blocks a file; the disc
// image's byte X is data block X / 0x1000 (read so from a real install:
// Assassin's Creed II's). When the volume descriptor isn't there, the image's
// first 0x10000 bytes are taken as left out (the "enhanced" layout: not yet
// seen on a real install, so a guess).
// GDF: the volume descriptor at the image's 0x10000 ("MICROSOFT*XBOX*MEDIA",
// the root directory's sector and size); a directory's entries {u16 left,
// u16 right, u32 sector, u32 size, u8 attributes, u8 name length, name},
// 4-byte aligned, a sector 0x800 bytes, 0xFF padding to a sector's end.
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace god {

// Reads n bytes of data file `file` (Data0000 is 0) at `offset`: false if it can't.
typedef bool (*ReadFn)(void* context, int file, size_t offset, size_t n, unsigned char* out);

// A root directory file of the disc image (case-insensitive), whole, at most
// `limit` bytes; false if it isn't there, is bigger, or the volume isn't read.
bool ReadRootFile(ReadFn read, void* context, const char* name, size_t limit, std::vector<unsigned char>& out);

// The same file's first `n` bytes (all of it if it's smaller): a xex's header.
bool ReadRootFileHead(ReadFn read, void* context, const char* name, size_t n, std::vector<unsigned char>& out);

}  // namespace god
