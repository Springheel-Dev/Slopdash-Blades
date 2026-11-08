// Reading game metadata from the files games are installed as. Portable C++
// (XDK compiler and g++): the console scanner and the PC tests share it.
//
//   STFS packages ("CON " / "LIVE" / "PIRS"): GOD installs (content type
//     0x7000), Xbox LIVE Arcade (0xD0000), original Xbox as GOD (0x5000).
//     The header holds the names, publisher, description and two PNGs.
//   default.xex (an extracted Xbox 360 game): title ID from the execution
//     info; name and icon from the title's XDBF resource.
//   default.xbe (an extracted original Xbox game): title ID and name from the
//     certificate.
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace library {

enum Kind
{
    kUnknown,
    kXbox360,           // GOD install or extracted default.xex
    kArcade,            // Xbox LIVE Arcade package
    kXboxOriginal,      // original Xbox: GOD install or extracted default.xbe
};

const char* KindName(Kind k);

struct Game
{
    Kind kind;
    unsigned titleId;
    unsigned contentType;           // STFS only
    std::wstring name;              // display name (title name if there's none)
    std::wstring publisher;
    std::wstring description;
    std::vector<unsigned char> icon;    // PNG, or empty
    std::string art;                // with no icon: a JPEG found beside it (the scanner's PictureBeside)
    std::string path;               // what launches it (package / default.xex / default.xbe)
    int place;                      // where it was found (the scanner's Place: games, apps, emulators)

    Game() : kind(kUnknown), titleId(0), contentType(0), place(0) {}
};

// Any STFS package's header (a saved game's, content type 1): its type, title
// ID, display name, title name, and its two images (the content's thumbnail
// and the title's), mended as ReadStfs's are.
struct ContentHeader
{
    unsigned contentType, titleId;
    std::wstring name, titleName;
    std::vector<unsigned char> icon, titleIcon;
    ContentHeader() : contentType(0), titleId(0) {}
};
bool ReadContentHeader(const unsigned char* data, size_t size, ContentHeader& out);

// STFS content types that are games (others are saves, profiles, caches...).
bool IsGameContentType(unsigned contentType);

// How many bytes of a file each reader needs (the scanner reads only these).
const size_t kStfsHeaderBytes = 0x971A;
const size_t kXbeHeaderBytes = 0x2000;

// Each returns false if the data isn't that kind of file (or not a game).
bool ReadStfs(const unsigned char* data, size_t size, Game& out);
bool ReadXbe(const unsigned char* data, size_t size, Game& out);
bool ReadXex(const unsigned char* data, size_t size, Game& out);   // needs the whole file

// The ESRB rating in a xex's game ratings header (optional header 0x40310, its
// first byte: 0 EC, 2 E, 4 E10+, 6 T, 8 M, 14 AO), from the xex's first bytes
// (its header: XEX2's own header size at 8). -1: no header, or 0xFF unrated.
int XexEsrb(const unsigned char* data, size_t size);

// A game's execution ID: which release of it this is (what trainers and
// title updates are made for). From an STFS package's header (0x354: media
// ID, version, base version, title ID, platform, type, disc, discs) or an
// XEX's execution ID header (0x40006, the same fields). False if neither.
struct Execution
{
    unsigned mediaId, version, baseVersion, titleId, discNum, discCount;
    Execution() : mediaId(0), version(0), baseVersion(0), titleId(0), discNum(0), discCount(0) {}
};
bool ReadExecution(const unsigned char* data, size_t size, Execution& out);

// A title update's number from its file name, as Microsoft names them
// ("tu00000003_00000000": update 3, for base version 0). -1 if it isn't one.
int TitleUpdateNumber(const std::string& fileName);

// A "cache" title update (Hdd:\Cache\TU_<id>..., a LIVE package of content
// type 000B0000): its game and release, and its number from its display
// name ("Halo 3 FINAL (English) Title Update #3": 3; -1 if the name has no
// "#<n>"). False if the header isn't a title update's.
struct TitleUpdate
{
    unsigned titleId, mediaId;
    int number;
    TitleUpdate() : titleId(0), mediaId(0), number(-1) {}
};
bool ReadTitleUpdate(const unsigned char* data, size_t size, TitleUpdate& out);

// An original Xbox game's title image: where its XBE keeps it (the
// $$XTIMAGE section, an XPR0 texture; the header, data, enough), and that
// texture as a PNG icon.
bool XbeTitleImage(const unsigned char* data, size_t size, size_t& offset, size_t& length);
bool IconFromXpr(const unsigned char* data, size_t size, std::vector<unsigned char>& icon);

// A gamer profile package (STFS content type 0x10000, in
// Content\<XUID>\FFFE07D1\00010000\<XUID>): its display name is the
// gamertag and its thumbnail the gamer picture (PNG, may be empty).
bool ReadProfileHeader(const unsigned char* data, size_t size, std::wstring& gamertag,
                       std::vector<unsigned char>& picture);

// Title name and icon from an XDBF (SPA) block, e.g. a title's resource.
bool ReadXdbf(const unsigned char* data, size_t size, std::wstring& name, std::vector<unsigned char>& icon);

}  // namespace library
