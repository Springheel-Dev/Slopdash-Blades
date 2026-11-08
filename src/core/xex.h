// Reading resources out of a retail XEX2 (e.g. a dashboard's dash.xex).
//
// Portable C++: builds with the XDK compiler (VS2010) and with g++ for tests.
// The image key is AES-decrypted with the retail XEX key, the image with
// AES-CBC; "normal" (LZX) compression is undone; resources are found through
// the 0x2FF optional header (name, virtual address, size).
#pragma once

#include <string>
#include <vector>

namespace xex {

struct Resource
{
    std::string name;       // up to 8 characters, e.g. "dashmain"
    unsigned offset;        // into the decompressed image
    unsigned size;
};

struct Image
{
    std::vector<unsigned char> data;    // the decompressed PE image
    unsigned loadAddress;
    std::vector<Resource> resources;

    const Resource* Find(const char* name) const;
};

// Fills `out` from a whole XEX file in memory. Returns false and sets `error`
// on anything unexpected (wrong magic, unsupported compression, bad LZX...).
bool Load(const unsigned char* file, size_t size, Image& out, std::string& error);

// LZX decompression (exposed for tests). windowBits: 15..21.
bool Lzx(const unsigned char* in, size_t inSize, unsigned windowBits, unsigned char* out, size_t outSize,
         std::string& error);

}  // namespace xex
