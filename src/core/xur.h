// XUI binary scenes (.xur, "XUIB" version 5) from the 2005 dashboards.
//
// Portable C++ (XDK compiler and g++). A .xur stores each class's properties
// by position, so a scene can only be read with its classes' exact property
// lists (the XUI runtime's own, plus the dashboard's custom classes).
//
// The launch system (2.0.1888) is one class older than the XDK's XUI runtime:
// its XuiControl has no NavTabForward and NavTabBackward, which the runtime
// has at positions 8 and 9, before Text, PointSize and ImagePath. Read as they
// are, a 1888 scene's control labels land in NavTabForward and its images in
// Text. Upgrade1888 renumbers them in place; the file keeps its size.
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace xur {

// A class the runtime doesn't have (the dashboard's own): its base and the
// types of its own stored properties (1 bool, 3 unsigned, 5 string...).
struct CustomClass
{
    std::string name;
    std::string base;
    std::vector<int> types;
};

struct UpgradeStats
{
    unsigned objects;           // elements read
    unsigned controlBlocks;     // XuiControl property blocks renumbered
    unsigned timelinePaths;     // animated XuiControl properties renumbered
};

// Whether the data is a XUIB scene.
bool IsScene(const unsigned char* data, size_t size);

// Rewrites a 2.0.1888 scene for the XDK's XUI runtime, in place. False (and
// the data untouched) if anything in it can't be read; `error` says what.
bool Upgrade1888(unsigned char* data, size_t size, const std::vector<CustomClass>& custom, UpgradeStats* stats,
                 std::string& error);

}  // namespace xur
