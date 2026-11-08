// Original Xbox textures ("XPR0" resources: .xbx files, an XBE's $$XTIMAGE
// title image): the top mip level as RGBA. DXT1 / DXT3 / DXT5, and swizzled or
// linear 32-bit A8R8G8B8 / X8R8G8B8, as tools/xpr.py decodes them. Portable
// C++ (XDK compiler and g++): the bytes are read one by one (little endian).
#pragma once

#include <stddef.h>
#include <vector>

namespace xpr {

// False if it isn't an XPR0 or its format isn't one of those.
bool Decode(const unsigned char* data, size_t size, unsigned& width, unsigned& height,
            std::vector<unsigned char>& rgba);

}  // namespace xpr
