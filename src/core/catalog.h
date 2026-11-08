// Game details from online sources, and the cache they're kept in.
// Portable: URLs, response parsing and the cache file format.
//
//   Microsoft's Xbox catalog (catalog.xboxlive.com, plain http): description,
//     developer, publisher, genre, release date, screenshot / box-art URLs.
//   XboxUnity (xboxunity.net, plain http): the title's 64x64 icon.
//
// When the catalog doesn't know a title (every Original Xbox game, homebrew,
// apps and emulators), the second look:
//   xemu's xdb (github.com/xemu-project/xdb, https): Original Xbox titles'
//     official names (from Redump) and front covers, by title ID.
//   XboxUnity's public lists (the ones its website uses): titles by title ID
//     or by name (Original Xbox, homebrew) and their covers, whole case wraps
//     (back, spine, front) that SlopDash crops to the front.
#pragma once

#include <string>
#include <vector>

namespace catalog {

struct Details
{
    bool found;
    std::wstring description, developer, publisher, genre, released;   // released: "2007"
    std::string screenshotUrl;      // first screenshot, else the large box art
    std::string coverUrl;           // the front cover (large box art, 219 x 300), if any
    std::wstring title;             // the official name the second look found ("" none)
    std::string source;             // where the details came from: "", "xdb", "unity"
    bool wrap;                      // coverUrl is a whole case wrap: its front is cropped
    bool second;                    // the second look has been made (found or not)

    Details() : found(false), wrap(false), second(false) {}
};

std::string CatalogUrl(unsigned titleId);
std::string IconUrl(unsigned titleId);

// The catalog's FindGames XML for one title.
bool ParseCatalog(const std::string& xml, Details& out);

// xdb: titles/<publisher letters>/<number>/ ("" if the ID can't be one).
std::string XdbInfoUrl(unsigned titleId);
std::string XdbCoverUrl(unsigned titleId);  // its front cover's thumbnail (256 x 369)
bool ParseXdbName(const std::string& json, std::wstring& name);

// XboxUnity's title list, searched by title ID or name.
struct UnityTitle
{
    std::string id;                 // XboxUnity's (homebrew has IDs of its own)
    std::wstring name;
    std::string type;               // "Xbox360", "Xbox1", "XBLA", "HomeBrew"...
    unsigned covers;
};
std::string UnitySearchUrl(const std::string& query);
bool ParseUnityTitles(const std::string& json, std::vector<UnityTitle>& out);
// The one that is this game: the same title ID, else (no ID, or none listed
// with it) the same name, ignoring case, spaces and punctuation. -1: none.
int PickUnityTitle(const std::vector<UnityTitle>& titles, unsigned titleId, const std::wstring& name);
std::string UnityCoversUrl(const std::string& unityId);
// Its icon (64 x 64; homebrew's are JPEGs, whatever the reply says they are).
std::string UnityIconUrl(const std::string& unityId);
// The best cover's image URL from the covers list: official ones first, then
// by rating. "" if there's none.
std::string UnityCoverUrl(const std::string& coversJson);

// A game folder's name as a title: "Splinter Cell - Chaos Theory.xiso" ->
// "Splinter Cell: Chaos Theory" (no ".xiso", no region in brackets, " - " a colon).
std::wstring FolderTitle(const std::string& folder);

// Letters and digits only, lower case: names compared this way.
std::string Plain(const std::wstring& s);

// The cache's info.txt: UTF-8 "key=value" lines (description with \n escaped).
std::string Serialize(const Details& d);
bool Deserialize(const std::string& text, Details& out);

}  // namespace catalog
