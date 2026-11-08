// The homebrew store behind the Marketplace blade: the same repos Aurora's
// Homebrew Store script reads (a repo .ini naming its categories, one .ini per
// category listing its items), fetched over HTTPS on a worker thread, and a
// download queue that installs items (a .7z is unpacked) on another. The
// screens (theme/store_screens.inc) only read snapshots of the state here and
// poll Generation() for changes.
#pragma once

#include <string>
#include <vector>

namespace store {

// The Marketplace hub's tiles, in order.
// (kDashboards has no tile on the Marketplace any more, its items are on kApps;
// the numbers stay: they name the hub's tiles, btnTile<shelf>)
enum Shelf { kGames, kApps, kEmulators, kDashboards, kThemes, kOther, kShelfCount };
const wchar_t* ShelfName(Shelf s);

// Whether an item is on the console: the store's record of its install
// (folder and version), else its own folder being there.
enum InstallState { kNotInstalled, kInstalled, kUpdateAvailable };

struct Item
{
    std::wstring title, version, author, size, description;
    std::string path;                       // as the listing gives it
    std::string type;                       // the category's type (App, Homebrew, Emulator...)
    std::vector<std::string> urls;          // dataurl, then dataurlpart2...
    std::vector<std::string> partPaths;     // per url: its own path ("" = the item's)
    std::wstring repo;                      // the repo's name, for the details
    std::string destination;                // where it installs on the console ("" = it can't)
    std::wstring blocked;                   // why it can't ("" = it can)
    bool auroraOnly;                        // installs into Aurora's own folders
    InstallState installed;                 // (set by GetShelf)
    std::wstring installedVersion;          // the version installed, when the store recorded it
};

// A shelf's items, as fetched so far. `loading` while repos are still being
// read; `error` when nothing could be read.
struct ShelfState
{
    std::vector<Item> items;
    int updates;                            // items with an update available
    bool loading;
    std::wstring error;
};
ShelfState GetShelf(Shelf s);
void LoadShelf(Shelf s, bool again = false);   // starts reading (once, unless again)

// Repos: ConsoleMods' (offered, off until ticked) and the ones added with Add
// Repo (settings store.repo.N: the repo .ini's URL). The shelves read the
// ticked ones.
struct Repo
{
    std::string url;
    std::wstring name;                      // its categories' prefix, once read (its host before)
    bool offered;                           // ConsoleMods': listed always, never removed
    bool on;                                // ticked
};
std::vector<Repo> Repos();                  // every one, ticked or not
bool AddRepo(const std::string& url);       // false: already there, or not http(s)
bool RemoveRepo(const std::string& url);    // offered ones stay
bool SetRepoOn(const std::string& url, bool on);

// The download queue.
enum JobState { kQueued, kDownloading, kUnpacking, kDone, kFailed, kCancelled };
struct Job
{
    int id;
    std::wstring title;
    std::string destination;
    JobState state;
    int part, parts;
    unsigned long long done, total;         // bytes of the current part
    std::wstring message;                   // what failed, or where it went
};
std::vector<Job> Jobs();
int Install(const Item& item);              // the job's id, or -1
void Cancel(int id);
int ActiveJobs();                           // queued or running

// Bumped whenever anything above changes (the screens redraw then).
unsigned Generation();

}  // namespace store
