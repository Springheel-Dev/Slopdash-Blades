// A small JSON reader (for the Aurora trainers' "aurora" resource): objects,
// arrays, strings (with \" \\ \/ \b \f \n \r \t and \uXXXX), numbers, true,
// false and null. Text is UTF-8.
//
// Portable C++: builds with the XDK compiler (VS2010) and with g++ for tests.
#pragma once

#include <string>
#include <vector>

namespace json {

struct Value
{
    enum Type { kNull, kBool, kNumber, kString, kArray, kObject };
    Type type;
    bool b;
    double n;
    std::wstring s;
    std::vector<Value> items;               // an array's, or an object's values
    std::vector<std::string> keys;          // an object's keys, one per item

    Value() : type(kNull), b(false), n(0) {}

    // An object's member (the key matched without regard to case), or NULL.
    const Value* Get(const char* key) const;

    std::wstring Text() const;              // a string, or a number as text
    bool Truth() const;                     // true, a nonzero number, "true", "1"
    // A number, or a string read as one: decimal, or hex with 0x or with A-F
    // in it ("415607E6"); 0 if it's neither.
    unsigned Unsigned() const;
};

bool Parse(const char* text, size_t size, Value& out, std::string& error);

}  // namespace json
