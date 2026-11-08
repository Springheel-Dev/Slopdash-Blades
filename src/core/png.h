// PNG decoding to 8-bit RGBA. Portable C++ (XDK compiler and g++).
//
// The XDK's D3DX can't read palette-based PNGs, which is what most of the
// Blades dashboard's images are, so SlopDash decodes them itself. Supported:
// every colour type (grey, RGB, palette, grey+alpha, RGBA), bit depths 1-16,
// tRNS transparency, Adam7 interlacing.
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace png {

struct Image
{
    unsigned width, height;
    std::vector<unsigned char> rgba;    // width * height * 4, rows top to bottom
};

bool IsPng(const unsigned char* data, size_t size);
bool Decode(const unsigned char* data, size_t size, Image& out, std::string& error);

// RGBA (width * height * 4) as a PNG: 8-bit RGBA, stored (uncompressed)
// DEFLATE blocks. For images SlopDash makes itself (an XBE's title image).
void Encode(unsigned width, unsigned height, const unsigned char* rgba, std::vector<unsigned char>& out);

// zlib-wrapped DEFLATE (exposed for tests).
bool Inflate(const unsigned char* in, size_t inSize, std::vector<unsigned char>& out, std::string& error);

}  // namespace png
