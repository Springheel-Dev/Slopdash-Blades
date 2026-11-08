// 7z archives unpacked (the homebrew store's downloads are .7z, as for
// Aurora's Homebrew Store). Uses Igor Pavlov's LZMA SDK (third_party/lzma,
// public domain) to read the archive, and decodes each solid block straight
// to the files: the SDK's own SzArEx_Extract holds a whole block in memory,
// and the store's archives run to 350 MB on a console with 512 MB.
// Streamed: Copy, LZMA, LZMA2, each alone or after a branch filter (BCJ, PPC,
// IA64, ARM, ARMT, SPARC) or Delta. Anything else (PPMd, BCJ2) is decoded in
// memory when its block is 64 MB or less. Win32 file calls: the console and
// the PC (MinGW, for tests) alike.
#pragma once

#include <string>

namespace sevenzip {

// Called as data is written; false cancels.
typedef bool (*Progress)(void* context, unsigned long long done, unsigned long long total);

// Unpacks archive into destDir (created if needed; "...\\" or not). Every
// file's CRC is checked; a name reaching outside destDir is refused.
bool Extract(const std::string& archive, const std::string& destDir, Progress progress, void* context,
             std::string& error);

}  // namespace sevenzip
