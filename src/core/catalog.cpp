#include "core/catalog.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/json.h"

namespace catalog {

namespace {

std::wstring FromUtf8(const std::string& s)
{
    std::wstring w;
    for (size_t k = 0; k < s.size();)
    {
        unsigned char c = (unsigned char)s[k];
        if (c < 0x80) { w += (wchar_t)c; k += 1; }
        else if ((c & 0xE0) == 0xC0 && k + 1 < s.size()) { w += (wchar_t)(((c & 0x1F) << 6) | (s[k + 1] & 0x3F)); k += 2; }
        else if ((c & 0xF0) == 0xE0 && k + 2 < s.size()) { w += (wchar_t)(((c & 0x0F) << 12) | ((s[k + 1] & 0x3F) << 6) | (s[k + 2] & 0x3F)); k += 3; }
        else k += 1;
    }
    return w;
}

std::string ToUtf8(const std::wstring& w)
{
    std::string s;
    for (size_t i = 0; i < w.size(); i++)
    {
        unsigned c = (unsigned)w[i];
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}

std::string Unescape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos) { out += s[i]; continue; }
        std::string e = s.substr(i + 1, semi - i - 1);
        if (e == "amp") out += '&';
        else if (e == "lt") out += '<';
        else if (e == "gt") out += '>';
        else if (e == "quot") out += '"';
        else if (e == "apos") out += '\'';
        else if (!e.empty() && e[0] == '#')
        {
            unsigned code = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? (unsigned)strtoul(e.c_str() + 2, NULL, 16)
                                                                         : (unsigned)strtoul(e.c_str() + 1, NULL, 10);
            out += ToUtf8(std::wstring(1, (wchar_t)code));
        }
        else { out += s.substr(i, semi - i + 1); }
        i = semi;
    }
    return out;
}

// The text of the first <live:tag>...</live:tag> at or after `from`.
std::string Tag(const std::string& xml, const char* tag, size_t from = 0)
{
    std::string open = std::string("<live:") + tag + ">", close = std::string("</live:") + tag + ">";
    size_t a = xml.find(open, from);
    if (a == std::string::npos)
        return "";
    a += open.size();
    size_t b = xml.find(close, a);
    return b == std::string::npos ? "" : Unescape(xml.substr(a, b - a));
}

std::string Hex8(unsigned v)
{
    char b[9];
    sprintf(b, "%08x", v);
    return b;
}

}  // namespace

std::string CatalogUrl(unsigned titleId)
{
    return "http://catalog.xboxlive.com/Catalog/Catalog.asmx/Query?methodName=FindGames"
           "&Names=Locale&Values=en-US&Names=LegalLocale&Values=en-US&Names=Store&Values=1"
           "&Names=PageSize&Values=1&Names=PageNum&Values=1&Names=DetailView&Values=5"
           "&Names=OfferFilterLevel&Values=1&Names=MediaIds&Values=66acd000-77fe-1000-9115-d802" +
           Hex8(titleId) +
           "&Names=UserTypes&Values=2&Names=MediaTypes&Values=1&Names=MediaTypes&Values=21"
           "&Names=MediaTypes&Values=23&Names=MediaTypes&Values=37&Names=MediaTypes&Values=46";
}

std::string IconUrl(unsigned titleId)
{
    char b[80];
    sprintf(b, "http://xboxunity.net/Resources/Lib/Icon.php?tid=%08X", titleId);
    return b;
}

bool ParseCatalog(const std::string& xml, Details& out)
{
    out = Details();
    size_t entry = xml.find("<entry");
    if (entry == std::string::npos)
        return false;
    out.found = true;
    std::string desc = Tag(xml, "fullDescription", entry);
    if (desc.empty())
        desc = Tag(xml, "reducedDescription", entry);
    out.description = FromUtf8(desc);
    out.developer = FromUtf8(Tag(xml, "developer", entry));
    out.publisher = FromUtf8(Tag(xml, "publisher", entry));
    std::string date = Tag(xml, "originalReleaseDate", entry);
    if (date.empty())
        date = Tag(xml, "releaseDate", entry);
    out.released = FromUtf8(date.substr(0, 4));
    // Genre: the first category under "Game Genres" (system 3000) other than the
    // "Game Genres" heading itself and "Xbox LIVE Games".
    for (size_t p = xml.find("<live:category>", entry); p != std::string::npos; p = xml.find("<live:category>", p + 1))
    {
        std::string name = Tag(xml, "name", p), system = Tag(xml, "system", p);
        if (system == "3000" && name != "Game Genres" && name.find("Xbox LIVE") == std::string::npos && !name.empty())
        {
            out.genre = FromUtf8(name);
            break;
        }
    }
    std::string screen, boxart;
    for (size_t p = xml.find("<live:fileUrl>", entry); p != std::string::npos; p = xml.find("<live:fileUrl>", p + 1))
    {
        std::string u = Tag(xml, "fileUrl", p);
        if (screen.empty() && u.find("screenlg") != std::string::npos && u.find(".jpg") != std::string::npos)
            screen = u;
        if (boxart.empty() && u.find("boxartlg") != std::string::npos)
            boxart = u;
    }
    out.screenshotUrl = !screen.empty() ? screen : boxart;
    out.coverUrl = boxart;
    return true;
}

// --- the second look -----------------------------------------------------------

namespace {

const char kXdb[] = "https://raw.githubusercontent.com/xemu-project/xdb/master/titles/";

// xdb's folder for a title ID, as xemu's site makes it (xemu-website
// generate.py: the first two bytes as letters, the rest as a 3-digit number).
std::string XdbFolder(unsigned titleId)
{
    char a = (char)(titleId >> 24), b = (char)(titleId >> 16);
    if (!isupper((unsigned char)a) && !isdigit((unsigned char)a))
        return "";
    if (!isupper((unsigned char)b) && !isdigit((unsigned char)b))
        return "";
    char buf[96];
    sprintf(buf, "%s%c%c/%03u/", kXdb, a, b, titleId & 0xFFFF);
    return buf;
}

bool ParseJson(const std::string& text, json::Value& v)
{
    std::string error;
    return !text.empty() && json::Parse(text.data(), text.size(), v, error);
}

std::string Narrow(const std::wstring& w)
{
    std::string s;
    for (size_t i = 0; i < w.size(); i++)
        s += w[i] < 128 ? (char)w[i] : '?';
    return s;
}

}  // namespace

std::string XdbInfoUrl(unsigned titleId)
{
    std::string f = XdbFolder(titleId);
    return f.empty() ? "" : f + "info.json";
}

std::string XdbCoverUrl(unsigned titleId)
{
    std::string f = XdbFolder(titleId);
    return f.empty() ? "" : f + "cover_front_thumbnail.jpg";
}

bool ParseXdbName(const std::string& text, std::wstring& name)
{
    json::Value v;
    if (!ParseJson(text, v) || v.type != json::Value::kObject)
        return false;
    const json::Value* n = v.Get("name");
    if (!n || n->type != json::Value::kString || n->s.empty())
        return false;
    name = n->s;
    return true;
}

std::string UnitySearchUrl(const std::string& query)
{
    std::string q;
    for (size_t i = 0; i < query.size(); i++)
    {
        unsigned char c = (unsigned char)query[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.')
            q += (char)c;
        else
        {
            char b[4];
            sprintf(b, "%%%02X", c);
            q += b;
        }
    }
    return "http://xboxunity.net/Resources/Lib/TitleList.php?page=0&count=10&search=" + q +
           "&sort=3&direction=1&category=0&filter=0";
}

bool ParseUnityTitles(const std::string& text, std::vector<UnityTitle>& out)
{
    out.clear();
    json::Value v;
    if (!ParseJson(text, v))
        return false;
    const json::Value* items = v.Get("Items");
    if (!items || items->type != json::Value::kArray)
        return false;
    for (size_t i = 0; i < items->items.size(); i++)
    {
        const json::Value& it = items->items[i];
        const json::Value *id = it.Get("TitleID"), *name = it.Get("Name"), *type = it.Get("TitleType"),
                          *covers = it.Get("Covers");
        if (!id || !name)
            continue;
        UnityTitle t;
        t.id = Narrow(id->Text());
        t.name = name->Text();
        t.type = type ? Narrow(type->Text()) : "";
        t.covers = covers ? (unsigned)_wtoi(covers->Text().c_str()) : 0;
        out.push_back(t);
    }
    return true;
}

std::string Plain(const std::wstring& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] < 128 && isalnum((int)s[i]))
            out += (char)tolower((int)s[i]);
    return out;
}

int PickUnityTitle(const std::vector<UnityTitle>& titles, unsigned titleId, const std::wstring& name)
{
    if (titleId)
    {
        char hex[9];
        sprintf(hex, "%08X", titleId);
        for (size_t i = 0; i < titles.size(); i++)
            if (!_stricmp(titles[i].id.c_str(), hex))
                return (int)i;
    }
    std::string want = Plain(name);
    if (want.empty())
        return -1;
    for (size_t i = 0; i < titles.size(); i++)
        if (Plain(titles[i].name) == want)
            return (int)i;
    return -1;
}

std::string UnityCoversUrl(const std::string& unityId)
{
    return "http://xboxunity.net/Resources/Lib/CoverInfo.php?titleid=" + unityId;
}

std::string UnityIconUrl(const std::string& unityId)
{
    return "http://xboxunity.net/Resources/Lib/Icon.php?tid=" + unityId;
}

std::string UnityCoverUrl(const std::string& text)
{
    json::Value v;
    if (!ParseJson(text, v))
        return "";
    const json::Value* covers = v.Get("Covers");
    if (!covers || covers->type != json::Value::kArray)
        return "";
    int best = -1, bestScore = -1;
    for (size_t i = 0; i < covers->items.size(); i++)
    {
        const json::Value& c = covers->items[i];
        const json::Value *id = c.Get("CoverID"), *official = c.Get("Official"), *rating = c.Get("Rating");
        if (!id)
            continue;
        int score = (official && official->Truth() ? 100 : 0) +
                    (rating && rating->type != json::Value::kNull ? _wtoi(rating->Text().c_str()) : 0);
        if (score > bestScore)
        {
            best = (int)i;
            bestScore = score;
        }
    }
    if (best < 0)
        return "";
    return "http://xboxunity.net/Resources/Lib/Cover.php?size=large&cid=" +
           Narrow(covers->items[best].Get("CoverID")->Text());
}

std::wstring FolderTitle(const std::string& folder)
{
    std::string s = folder;
    if (s.size() > 5 && !_stricmp(s.c_str() + s.size() - 5, ".xiso"))
        s.resize(s.size() - 5);
    // Brackets at the end: (USA), [!], (En,Fr,De)...
    for (;;)
    {
        while (!s.empty() && s[s.size() - 1] == ' ')
            s.resize(s.size() - 1);
        if (s.empty() || (s[s.size() - 1] != ')' && s[s.size() - 1] != ']'))
            break;
        size_t open = s.find_last_of(s[s.size() - 1] == ')' ? '(' : '[');
        if (open == std::string::npos || open == 0)
            break;
        s.resize(open);
    }
    std::wstring out;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s.compare(i, 3, " - ") == 0)
        {
            out += L": ";
            i += 2;
            continue;
        }
        out += (wchar_t)(unsigned char)s[i];
    }
    return out;
}

std::string Serialize(const Details& d)
{
    std::string desc = ToUtf8(d.description), escaped;
    for (size_t i = 0; i < desc.size(); i++)
    {
        if (desc[i] == '\n') escaped += "\\n";
        else if (desc[i] != '\r') escaped += desc[i];
    }
    return std::string("found=") + (d.found ? "1" : "0") + "\ndeveloper=" + ToUtf8(d.developer) +
           "\npublisher=" + ToUtf8(d.publisher) + "\ngenre=" + ToUtf8(d.genre) + "\nreleased=" +
           ToUtf8(d.released) + "\nscreenshot=" + d.screenshotUrl +
           "\ncover=" + d.coverUrl + "\ntitle=" + ToUtf8(d.title) + "\nsource=" + d.source +
           "\nwrap=" + (d.wrap ? "1" : "0") + "\nsecond=" + (d.second ? "1" : "0") + "\ndescription=" + escaped +
           "\n";
}

bool Deserialize(const std::string& text, Details& out)
{
    out = Details();
    bool any = false;
    size_t p = 0;
    while (p < text.size())
    {
        size_t eol = text.find('\n', p);
        std::string line = text.substr(p, eol == std::string::npos ? std::string::npos : eol - p);
        p = eol == std::string::npos ? text.size() : eol + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        any = true;
        if (k == "found") out.found = v == "1";
        else if (k == "developer") out.developer = FromUtf8(v);
        else if (k == "publisher") out.publisher = FromUtf8(v);
        else if (k == "genre") out.genre = FromUtf8(v);
        else if (k == "released") out.released = FromUtf8(v);
        else if (k == "screenshot") out.screenshotUrl = v;
        else if (k == "cover") out.coverUrl = v;
        else if (k == "title") out.title = FromUtf8(v);
        else if (k == "source") out.source = v;
        else if (k == "wrap") out.wrap = v == "1";
        else if (k == "second") out.second = v == "1";
        else if (k == "description")
        {
            std::string un;
            for (size_t i = 0; i < v.size(); i++)
            {
                if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == 'n') { un += "\r\n"; i++; }
                else un += v[i];
            }
            out.description = FromUtf8(un);
        }
    }
    return any;
}

}  // namespace catalog
