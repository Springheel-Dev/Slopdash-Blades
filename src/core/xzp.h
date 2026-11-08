// XUI packages (.xzp, "XUIZ"): a header, a table of {size, offset, name}, data.
// Portable C++ (XDK compiler and g++).
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace xzp {

struct Entry
{
    std::wstring name;      // as stored, e.g. L"ja-jp\\strings.xus"
    unsigned offset;        // from the start of the package
    unsigned size;
};

// The package's entries; false if it isn't a well-formed XUIZ package.
bool List(const unsigned char* data, size_t size, std::vector<Entry>& out);

}  // namespace xzp
