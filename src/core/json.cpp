#include "core/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace json {

namespace {

struct Reader
{
    const char* p;
    const char* end;
    std::string error;
    int depth;

    void Space()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            p++;
    }

    bool Fail(const char* what)
    {
        if (error.empty())
            error = what;
        return false;
    }

    bool Word(const char* w)
    {
        size_t n = strlen(w);
        if ((size_t)(end - p) < n || memcmp(p, w, n))
            return false;
        p += n;
        return true;
    }

    unsigned Hex4()
    {
        unsigned v = 0;
        for (int i = 0; i < 4; i++, p++)
        {
            if (p >= end)
                return 0xFFFFFFFF;
            char c = *p;
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= c - '0';
            else if (c >= 'a' && c <= 'f')
                v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                v |= c - 'A' + 10;
            else
                return 0xFFFFFFFF;
        }
        return v;
    }

    bool String(std::wstring& out)
    {
        // (at the opening quote)
        p++;
        while (p < end && *p != '"')
        {
            unsigned char c = (unsigned char)*p;
            if (c == '\\')
            {
                if (++p >= end)
                    return Fail("unfinished escape");
                char e = *p++;
                switch (e)
                {
                case '"': out += L'"'; break;
                case '\\': out += L'\\'; break;
                case '/': out += L'/'; break;
                case 'b': out += L'\b'; break;
                case 'f': out += L'\f'; break;
                case 'n': out += L'\n'; break;
                case 'r': out += L'\r'; break;
                case 't': out += L'\t'; break;
                case 'u':
                {
                    unsigned v = Hex4();
                    if (v == 0xFFFFFFFF)
                        return Fail("bad \\u escape");
                    out += (wchar_t)v;
                    break;
                }
                default:
                    return Fail("unknown escape");
                }
                continue;
            }
            // UTF-8 (anything beyond 16 bits becomes '?')
            unsigned cp = c;
            int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
            if (more)
                cp = c & (0x3F >> more);
            p++;
            for (int i = 0; i < more && p < end; i++, p++)
                cp = (cp << 6) | ((unsigned char)*p & 0x3F);
            out += cp > 0xFFFF ? L'?' : (wchar_t)cp;
        }
        if (p >= end)
            return Fail("unfinished string");
        p++;
        return true;
    }

    bool Number(Value& v)
    {
        const char* start = p;
        if (p < end && (*p == '-' || *p == '+'))
            p++;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+'))
            p++;
        if (p == start)
            return Fail("unexpected character");
        std::string t(start, p);
        v.type = Value::kNumber;
        v.n = strtod(t.c_str(), NULL);
        return true;
    }

    bool Parse(Value& v)
    {
        if (++depth > 64)
            return Fail("nested too deep");
        Space();
        if (p >= end)
            return Fail("unexpected end");
        bool ok = true;
        if (*p == '{')
        {
            v.type = Value::kObject;
            p++;
            Space();
            if (p < end && *p == '}')
                p++;
            else
                for (;;)
                {
                    Space();
                    if (p >= end || *p != '"')
                    {
                        ok = Fail("expected a key");
                        break;
                    }
                    std::wstring key;
                    if (!String(key))
                    {
                        ok = false;
                        break;
                    }
                    Space();
                    if (p >= end || *p != ':')
                    {
                        ok = Fail("expected ':'");
                        break;
                    }
                    p++;
                    Value item;
                    if (!Parse(item))
                    {
                        ok = false;
                        break;
                    }
                    v.keys.push_back(std::string(key.begin(), key.end()));
                    v.items.push_back(item);
                    Space();
                    if (p < end && *p == ',')
                    {
                        p++;
                        continue;
                    }
                    if (p < end && *p == '}')
                    {
                        p++;
                        break;
                    }
                    ok = Fail("expected ',' or '}'");
                    break;
                }
        }
        else if (*p == '[')
        {
            v.type = Value::kArray;
            p++;
            Space();
            if (p < end && *p == ']')
                p++;
            else
                for (;;)
                {
                    Value item;
                    if (!Parse(item))
                    {
                        ok = false;
                        break;
                    }
                    v.items.push_back(item);
                    Space();
                    if (p < end && *p == ',')
                    {
                        p++;
                        continue;
                    }
                    if (p < end && *p == ']')
                    {
                        p++;
                        break;
                    }
                    ok = Fail("expected ',' or ']'");
                    break;
                }
        }
        else if (*p == '"')
        {
            v.type = Value::kString;
            ok = String(v.s);
        }
        else if (Word("true"))
        {
            v.type = Value::kBool;
            v.b = true;
        }
        else if (Word("false"))
            v.type = Value::kBool;
        else if (Word("null"))
            v.type = Value::kNull;
        else
            ok = Number(v);
        depth--;
        return ok;
    }
};

}  // namespace

const Value* Value::Get(const char* key) const
{
    if (type != kObject)
        return NULL;
    for (size_t i = 0; i < keys.size() && i < items.size(); i++)
        if (!_stricmp(keys[i].c_str(), key))
            return &items[i];
    return NULL;
}

std::wstring Value::Text() const
{
    if (type == kString)
        return s;
    if (type == kNumber)
    {
        wchar_t b[32];
        if (n == (double)(long long)n)
            _snwprintf(b, 32, L"%lld", (long long)n);
        else
            _snwprintf(b, 32, L"%g", n);
        return b;
    }
    if (type == kBool)
        return b ? L"true" : L"false";
    return L"";
}

bool Value::Truth() const
{
    if (type == kBool)
        return b;
    if (type == kNumber)
        return n != 0;
    if (type == kString)
        return s == L"true" || s == L"1";
    return false;
}

unsigned Value::Unsigned() const
{
    if (type == kNumber)
        return (unsigned)(long long)n;
    if (type != kString || s.empty())
        return 0;
    std::string t(s.begin(), s.end());
    const char* c = t.c_str();
    bool hex = !_strnicmp(c, "0x", 2) || strpbrk(c, "abcdefABCDEF") != NULL;
    if (!_strnicmp(c, "0x", 2))
        c += 2;
    return (unsigned)strtoul(c, NULL, hex ? 16 : 10);
}

bool Parse(const char* text, size_t size, Value& out, std::string& error)
{
    Reader r;
    r.p = text;
    r.end = text + size;
    r.depth = 0;
    // (a UTF-8 byte order mark first is allowed)
    if (size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        r.p += 3;
    out = Value();
    if (!r.Parse(out))
    {
        error = r.error;
        return false;
    }
    r.Space();
    // (trailing NULs, as a resource padded to its section size has)
    while (r.p < r.end && *r.p == 0)
        r.p++;
    if (r.p != r.end)
    {
        error = "text after the value";
        return false;
    }
    return true;
}

}  // namespace json
