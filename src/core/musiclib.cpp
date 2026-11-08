#include "core/musiclib.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <algorithm>
#include <map>

namespace musiclib {

namespace {

// ID3v1's genres (0-79, the ones every tagger knows).
const char* const kGenreNames[] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
    "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial",
    "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal",
    "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel",
    "Noise", "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock",
    "Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream",
    "Southern Rock", "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle",
    "Native American", "Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal",
    "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock"};

std::wstring Widen(const char* s)
{
    std::wstring w;
    for (; *s; s++)
        w += (wchar_t)(unsigned char)*s;
    return w;
}

void Trim(std::wstring& s)
{
    while (!s.empty() && (s[s.size() - 1] == L' ' || s[s.size() - 1] == 0))
        s.erase(s.size() - 1);
    size_t i = 0;
    while (i < s.size() && s[i] == L' ')
        i++;
    s.erase(0, i);
}

// A text frame's text: its encoding byte, then the text (0 Latin-1, 1 UTF-16
// with a byte-order mark, 2 UTF-16 big-endian, 3 UTF-8).
std::wstring FrameText(const unsigned char* p, size_t n)
{
    std::wstring out;
    if (n < 1)
        return out;
    unsigned char enc = p[0];
    p++, n--;
    if (enc == 1 || enc == 2)
    {
        bool little = false;
        if (enc == 1 && n >= 2)
        {
            little = p[0] == 0xFF && p[1] == 0xFE;
            if ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))
                p += 2, n -= 2;
        }
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            wchar_t c = little ? (wchar_t)(p[i] | (p[i + 1] << 8)) : (wchar_t)((p[i] << 8) | p[i + 1]);
            if (!c)
                break;
            out += c;
        }
    }
    else
    {
        for (size_t i = 0; i < n && p[i]; i++)
        {
            unsigned c = p[i];
            if (enc == 3 && (c & 0xE0) == 0xC0 && i + 1 < n)
                c = ((c & 0x1F) << 6) | (p[++i] & 0x3F);
            else if (enc == 3 && (c & 0xF0) == 0xE0 && i + 2 < n)
            {
                c = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
                i += 2;
            }
            out += (wchar_t)c;
        }
    }
    Trim(out);
    return out;
}

unsigned Syncsafe(const unsigned char* p)
{
    return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

int TrackNumber(const std::wstring& s)
{
    return (int)wcstol(s.c_str(), NULL, 10);       // "3/12": 3
}

std::wstring Lower(const std::wstring& s)
{
    std::wstring l(s);
    for (size_t i = 0; i < l.size(); i++)
        l[i] = (wchar_t)towlower(l[i]);
    return l;
}

// ASF's little-endian UTF-16 strings, `bytes` long (with or without the 0).
std::wstring Utf16Le(const unsigned char* p, size_t bytes)
{
    std::wstring out;
    for (size_t i = 0; i + 1 < bytes; i += 2)
    {
        wchar_t c = (wchar_t)(p[i] | (p[i + 1] << 8));
        if (!c)
            break;
        out += c;
    }
    Trim(out);
    return out;
}

unsigned Le16(const unsigned char* p) { return p[0] | (p[1] << 8); }
unsigned Le32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

const unsigned char kAsfHeader[16] = {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                      0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
const unsigned char kAsfContent[16] = {0x33, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                                       0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
const unsigned char kAsfExtended[16] = {0x40, 0xA4, 0xD0, 0xD2, 0x07, 0xE3, 0xD2, 0x11,
                                        0x97, 0xF0, 0x00, 0xA0, 0xC9, 0x5E, 0xA8, 0x50};

struct Less
{
    bool operator()(const std::wstring& a, const std::wstring& b) const { return Lower(a) < Lower(b); }
};

}  // namespace

std::wstring GenreName(const std::wstring& raw)
{
    std::wstring s = raw;
    Trim(s);
    // "(17)" or "(17)Rock": the number; a name after it wins.
    if (!s.empty() && s[0] == L'(')
    {
        size_t close = s.find(L')');
        if (close != std::wstring::npos)
        {
            std::wstring rest = s.substr(close + 1);
            Trim(rest);
            if (!rest.empty())
                return rest;
            s = s.substr(1, close - 1);
        }
    }
    if (!s.empty() && iswdigit(s[0]))
    {
        int n = (int)wcstol(s.c_str(), NULL, 10);
        if (n >= 0 && n < (int)(sizeof(kGenreNames) / sizeof(kGenreNames[0])))
            return Widen(kGenreNames[n]);
        return L"";
    }
    return s;
}

bool ReadId3v2(const unsigned char* data, size_t size, Tags& tags)
{
    if (size < 10 || memcmp(data, "ID3", 3) || data[3] < 2 || data[3] > 4)
        return false;
    int version = data[3];
    size_t end = 10 + Syncsafe(data + 6);
    if (end > size)
        end = size;
    size_t at = 10;
    if ((data[5] & 0x40) && version >= 3 && at + 4 <= end)
        at += version == 4 ? Syncsafe(data + 10) : 4 + ((size_t)data[10] << 24 | data[11] << 16 | data[12] << 8 | data[13]);
    size_t head = version == 2 ? 6 : 10;
    while (at + head <= end)
    {
        const unsigned char* f = data + at;
        if (!f[0])
            break;                              // padding
        size_t n = version == 2 ? ((size_t)f[3] << 16 | f[4] << 8 | f[5])
                 : version == 4 ? Syncsafe(f + 4)
                                : ((size_t)f[4] << 24 | f[5] << 16 | f[6] << 8 | f[7]);
        if (at + head + n > end)
            break;
        const unsigned char* body = f + head;
        bool v2 = version == 2;
        if (!memcmp(f, v2 ? "TT2" : "TIT2", v2 ? 3 : 4))
            tags.title = FrameText(body, n);
        else if (!memcmp(f, v2 ? "TP1" : "TPE1", v2 ? 3 : 4))
            tags.artist = FrameText(body, n);
        else if (!memcmp(f, v2 ? "TAL" : "TALB", v2 ? 3 : 4))
            tags.album = FrameText(body, n);
        else if (!memcmp(f, v2 ? "TCO" : "TCON", v2 ? 3 : 4))
            tags.genre = GenreName(FrameText(body, n));
        else if (!memcmp(f, v2 ? "TRK" : "TRCK", v2 ? 3 : 4))
            tags.track = TrackNumber(FrameText(body, n));
        at += head + n;
    }
    return true;
}

bool ReadId3v1(const unsigned char* t, Tags& tags)
{
    if (memcmp(t, "TAG", 3))
        return false;
    struct Field { size_t at, size; std::wstring* to; } fields[] = {
        {3, 30, &tags.title}, {33, 30, &tags.artist}, {63, 30, &tags.album}};
    for (size_t i = 0; i < 3; i++)
    {
        if (!fields[i].to->empty())
            continue;
        std::wstring w;
        for (size_t k = 0; k < fields[i].size && t[fields[i].at + k]; k++)
            w += (wchar_t)t[fields[i].at + k];
        Trim(w);
        *fields[i].to = w;
    }
    if (!tags.track && t[125] == 0 && t[126])
        tags.track = t[126];                    // ID3v1.1
    if (tags.genre.empty() && t[127] < sizeof(kGenreNames) / sizeof(kGenreNames[0]))
        tags.genre = Widen(kGenreNames[t[127]]);
    return true;
}

bool ReadAsf(const unsigned char* data, size_t size, Tags& tags)
{
    if (size < 30 || memcmp(data, kAsfHeader, 16))
        return false;
    unsigned count = Le32(data + 24);
    size_t at = 30;
    for (unsigned o = 0; o < count && at + 24 <= size; o++)
    {
        const unsigned char* obj = data + at;
        size_t len = Le32(obj + 16);            // (64-bit; headers are far under 4 GB)
        if (len < 24 || at + len > size)
            break;
        const unsigned char* b = obj + 24;
        size_t n = len - 24;
        if (!memcmp(obj, kAsfContent, 16) && n >= 10)
        {
            unsigned title = Le16(b), author = Le16(b + 2), copyright = Le16(b + 4), description = Le16(b + 6);
            if (10 + title + author <= n)
            {
                tags.title = Utf16Le(b + 10, title);
                tags.artist = Utf16Le(b + 10 + title, author);
            }
            if (10 + title + author + copyright + description <= n)
                tags.description = Utf16Le(b + 10 + title + author + copyright, description);
        }
        else if (!memcmp(obj, kAsfExtended, 16) && n >= 2)
        {
            unsigned items = Le16(b);
            size_t p = 2;
            for (unsigned i = 0; i < items && p + 2 <= n; i++)
            {
                unsigned nameLen = Le16(b + p);
                if (p + 2 + nameLen + 4 > n)
                    break;
                std::wstring name = Utf16Le(b + p + 2, nameLen);
                unsigned type = Le16(b + p + 2 + nameLen), valueLen = Le16(b + p + 4 + nameLen);
                const unsigned char* v = b + p + 6 + nameLen;
                if (p + 6 + nameLen + valueLen > n)
                    break;
                std::wstring text = type == 0 ? Utf16Le(v, valueLen) : L"";
                unsigned number = type == 3 && valueLen >= 4 ? Le32(v) : type == 5 && valueLen >= 2 ? Le16(v) : 0;
                if (name == L"WM/AlbumTitle")
                    tags.album = text;
                else if (name == L"WM/Genre")
                    tags.genre = GenreName(text);
                else if (name == L"WM/TrackNumber")
                    tags.track = type == 0 ? TrackNumber(text) : (int)number;
                else if (name == L"WM/Track" && !tags.track)
                    tags.track = (type == 0 ? TrackNumber(text) : (int)number) + 1;     // (0-based)
                else if (name == L"WM/AlbumArtist" && tags.artist.empty())
                    tags.artist = text;
                p += 6 + nameLen + valueLen;
            }
        }
        at += len;
    }
    return true;
}

std::vector<Group> GroupBy(const std::vector<Song>& songs, View view)
{
    std::map<std::wstring, Group, Less> groups;
    for (size_t i = 0; i < songs.size(); i++)
    {
        const Song& s = songs[i];
        std::wstring name = view == kAlbums ? s.album : view == kArtists ? s.artist : s.genre;
        if (name.empty())
            name = view == kAlbums ? L"Unknown Album" : view == kArtists ? L"Unknown Artist" : L"Unknown Genre";
        Group& g = groups[name];
        if (g.name.empty())
            g.name = name;
        if (view == kAlbums)
        {
            if (g.songs.empty())
                g.detail = s.artist;
            else if (Lower(g.detail) != Lower(s.artist))
                g.detail = L"Various Artists";
        }
        g.songs.push_back(i);
    }
    std::vector<Group> out;
    for (std::map<std::wstring, Group, Less>::iterator it = groups.begin(); it != groups.end(); ++it)
    {
        Group g = it->second;
        struct Order
        {
            const std::vector<Song>* s;
            bool byAlbum;
            bool operator()(size_t a, size_t b) const
            {
                const Song& x = (*s)[a];
                const Song& y = (*s)[b];
                if (byAlbum && Lower(x.album) != Lower(y.album))
                    return Lower(x.album) < Lower(y.album);
                if (x.track != y.track)
                    return (x.track ? x.track : 9999) < (y.track ? y.track : 9999);
                return Lower(x.title) < Lower(y.title);
            }
        } order = {&songs, view != kAlbums};
        std::stable_sort(g.songs.begin(), g.songs.end(), order);
        out.push_back(g);
    }
    return out;
}

std::vector<size_t> ByTitle(const std::vector<Song>& songs)
{
    std::vector<size_t> out(songs.size());
    for (size_t i = 0; i < songs.size(); i++)
        out[i] = i;
    struct Order
    {
        const std::vector<Song>* s;
        bool operator()(size_t a, size_t b) const { return Lower((*s)[a].title) < Lower((*s)[b].title); }
    } order = {&songs};
    std::stable_sort(out.begin(), out.end(), order);
    return out;
}

std::vector<std::string> ParseM3u(const std::string& text, const std::string& folder)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find_first_of("\r\n", at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line[0] == ' ' || line[0] == '\t'))
            line.erase(0, 1);
        while (!line.empty() && (line[line.size() - 1] == ' ' || line[line.size() - 1] == '\t'))
            line.erase(line.size() - 1);
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);                   // (a UTF-8 byte-order mark)
        if (line.empty() || line[0] == '#')
            continue;
        for (size_t i = 0; i < line.size(); i++)
            if (line[i] == '/')
                line[i] = '\\';
        bool absolute = line.find(':') != std::string::npos;
        out.push_back(absolute || folder.empty() ? line : folder + "\\" + line);
    }
    return out;
}

std::string WriteM3u(const std::vector<std::string>& paths)
{
    std::string out = "#EXTM3U\r\n";
    for (size_t i = 0; i < paths.size(); i++)
        out += paths[i] + "\r\n";
    return out;
}

}  // namespace musiclib
