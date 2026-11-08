// The importer's scene upgrade (upgrade_scenes.exe): every XUI scene in a 2.0.1888 module renumbered
// for the XDK's XUI runtime (core/xur Upgrade1888). Our own code only.
#pragma once

#include <string>
#include <vector>

namespace scenes {

struct Scene
{
    std::string name;                   // <package>#<file>, e.g. "dashmain#dashmain.xur"
    std::vector<unsigned char> data;
};

// Every scene of one module (a whole .xex in memory), upgraded. A scene that
// won't upgrade is left out and its "FAIL: ..." line added to `failures`.
// False (and `error`) if the module itself can't be read.
bool Upgrade(const unsigned char* file, size_t size, std::vector<Scene>& out, std::vector<std::string>& failures,
             std::string& error);

}  // namespace scenes
