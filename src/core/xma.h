// XMA version 1 sounds (WAVE_FORMAT_XMA, 0x165) as XMA2 (0x166), not re-encoded.
// The dashboards' sounds are XMA1; the XDK's XAudio2, which the XDK's XUI sound
// class plays through, takes XMA2 only (xma2defs.h: XMAWAVEFORMAT is legacy),
// so XUI loaded them and played nothing. tools/xma2.py is the same conversion.
// Portable C++ (XDK compiler and g++).
#pragma once

#include <stddef.h>
#include <vector>

namespace xma {

// True if the RIFF file is XMA1.
bool IsXma1(const unsigned char* data, size_t size);

// The XMA2 file for an XMA1 one; false if it isn't one or is malformed.
bool ToXma2(const unsigned char* data, size_t size, std::vector<unsigned char>& out);

}  // namespace xma
