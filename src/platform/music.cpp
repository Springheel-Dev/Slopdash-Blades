#include "platform/music.h"

#include <xtl.h>
#include <xmp.h>
#include <string.h>
#include <vector>

#include "platform/log.h"

using musiclib::Group;
using musiclib::Song;

namespace music {

namespace {

std::vector<Song> g_songs;
const size_t kMost = 2000;
std::vector<Group> g_views[5];              // by musiclib::View
const char kPlaylists[] = "hdd:\\SlopDash\\Playlists";

// The current playlist: its songs, the console player's playlist of them and
// each song's handle in it (to tell which is playing).
std::vector<Song> g_queue;
std::vector<XMP_HANDLE> g_handles;
XMP_HANDLE g_playlist = NULL;

bool IsSong(const char* name, bool& mp3)
{
    const char* dot = strrchr(name, '.');
    if (!dot)
        return false;
    mp3 = !_stricmp(dot, ".mp3");
    return mp3 || !_stricmp(dot, ".wma");
}

// The drives' own folders, where songs aren't kept (as pictures.cpp's).
bool SkipFolder(const char* name)
{
    static const char* const kSkip[] = { "Content", "Apps", "Games", "Emulators", "Cache", "Xbox360",
                                         "$SystemUpdate", "SlopDash", "StoreDownloads", "Compatibility" };
    for (size_t i = 0; i < sizeof(kSkip) / sizeof(kSkip[0]); i++)
        if (!_stricmp(name, kSkip[i]))
            return true;
    return name[0] == '.' || name[0] == '$';
}

std::wstring Widen(const std::string& s)
{
    std::wstring w;
    for (size_t i = 0; i < s.size(); i++)
        w += (wchar_t)(unsigned char)s[i];
    return w;
}

// Paths and playlist names stay plain ASCII on the drive: anything else as '_'.
std::string Narrow(const std::wstring& w)
{
    std::string s;
    for (size_t i = 0; i < w.size(); i++)
        s += w[i] < 128 && w[i] >= 32 ? (char)w[i] : '_';
    return s;
}

bool ReadAll(const std::string& path, std::string& out, DWORD most)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(h, NULL), got = 0;
    if (size > most)
        size = most;
    out.resize(size);
    BOOL ok = !size || ReadFile(h, &out[0], size, &got, NULL);
    out.resize(got);
    CloseHandle(h);
    return ok != FALSE;
}

bool WriteAll(const std::string& path, const std::string& data)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD put = 0;
    BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, NULL);
    CloseHandle(h);
    return ok && put == data.size();
}

// A song's tags: MP3, its ID3v2 tag (else ID3v1); WMA, its ASF header. The
// file's name when there's no title.
void ReadTags(Song& s, const char* fileName)
{
    HANDLE h = CreateFileA(s.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        // The frames wanted come before any pictures: the first 64 KB.
        std::vector<unsigned char> head(64 * 1024);
        DWORD got = 0;
        if (!ReadFile(h, &head[0], (DWORD)head.size(), &got, NULL))
            got = 0;
        musiclib::Tags tags;
        bool found = s.mp3 ? musiclib::ReadId3v2(&head[0], got, tags) : musiclib::ReadAsf(&head[0], got, tags);
        if (s.mp3 && (!found || tags.title.empty()))
        {
            unsigned char tail[128];
            DWORD size = GetFileSize(h, NULL);
            if (size > 128 && SetFilePointer(h, size - 128, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
                ReadFile(h, tail, 128, &got, NULL) && got == 128)
                musiclib::ReadId3v1(tail, tags);
        }
        CloseHandle(h);
        s.title = tags.title;
        s.artist = tags.artist;
        s.album = tags.album;
        s.genre = tags.genre;
        s.track = tags.track;
    }
    if (s.title.empty())
    {
        std::string n = fileName;
        s.title = Widen(n.substr(0, n.find_last_of('.')));
    }
}

void Walk(const std::string& dir, int depth)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::string> subdirs;
    do
    {
        bool mp3 = false;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (depth > 0 && !SkipFolder(fd.cFileName))
                subdirs.push_back(fd.cFileName);
        }
        else if (IsSong(fd.cFileName, mp3) && g_songs.size() < kMost)
        {
            Song s;
            s.path = dir + "\\" + fd.cFileName;
            s.mp3 = mp3;
            ReadTags(s, fd.cFileName);
            g_songs.push_back(s);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (size_t i = 0; i < subdirs.size() && g_songs.size() < kMost; i++)
        Walk(dir + "\\" + subdirs[i], depth - 1);
}

// The saved playlists, as groups of the library's songs.
void ReadPlaylists()
{
    std::vector<Group>& out = g_views[musiclib::kPlaylists];
    out.clear();
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((std::string(kPlaylists) + "\\*.m3u").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
    {
        std::string text;
        if (!ReadAll(std::string(kPlaylists) + "\\" + fd.cFileName, text, 1024 * 1024))
            continue;
        std::vector<std::string> paths = musiclib::ParseM3u(text, kPlaylists);
        Group g;
        std::string name = fd.cFileName;
        g.name = Widen(name.substr(0, name.size() - 4));
        for (size_t p = 0; p < paths.size(); p++)
            for (size_t i = 0; i < g_songs.size(); i++)
                if (!_stricmp(g_songs[i].path.c_str(), paths[p].c_str()))
                {
                    g.songs.push_back(i);
                    break;
                }
        out.push_back(g);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

void BuildViews()
{
    g_views[musiclib::kAlbums] = musiclib::GroupBy(g_songs, musiclib::kAlbums);
    g_views[musiclib::kArtists] = musiclib::GroupBy(g_songs, musiclib::kArtists);
    g_views[musiclib::kGenres] = musiclib::GroupBy(g_songs, musiclib::kGenres);
    Group all;
    all.name = L"Songs";
    all.songs = musiclib::ByTitle(g_songs);
    g_views[musiclib::kSongs] = std::vector<Group>(1, all);
    ReadPlaylists();
}

// Plays the current playlist (g_queue) from song `from` of it: a new playlist
// for the console's player (one can't be changed once made), the old one let go.
bool PlayQueue(size_t from)
{
    if (g_queue.empty())
        return false;
    std::vector<std::wstring> paths(g_queue.size());
    std::vector<XMP_SONGDESCRIPTOR> d(g_queue.size());
    for (size_t i = 0; i < g_queue.size(); i++)
    {
        paths[i] = Widen(g_queue[i].path);      // (SlopDash's own drive names: the player takes them)
        ZeroMemory(&d[i], sizeof(d[i]));
        d[i].pwszFilePath = paths[i].c_str();
        d[i].pwszTitle = g_queue[i].title.c_str();
        d[i].pwszArtist = g_queue[i].artist.c_str();
        d[i].pwszAlbum = g_queue[i].album.c_str();
        d[i].pwszAlbumArtist = g_queue[i].artist.c_str();
        d[i].pwszGenre = g_queue[i].genre.c_str();
        d[i].dwTrackNumber = (DWORD)g_queue[i].track;
        d[i].eSongFormat = g_queue[i].mp3 ? XMP_SONGFORMAT_MP3 : XMP_SONGFORMAT_WMA;
    }
    std::vector<XMP_HANDLE> songs(g_queue.size());
    XMP_HANDLE list = NULL;
    DWORD r = XMPCreateTitlePlaylist(&d[0], (DWORD)d.size(), XMP_CREATETITLEPLAYLISTFLAG_NONE, L"SlopDash", &songs[0],
                                     &list);
    if (r != ERROR_SUCCESS)
    {
        Log("music: playlist of %u: %u", (unsigned)d.size(), (unsigned)r);
        return false;
    }
    if (from >= songs.size())
        from = 0;
    r = XMPPlayTitlePlaylist(list, songs[from], NULL);
    Log("music: playing %S (%u of %u): %u", g_queue[from].title.c_str(), (unsigned)(from + 1), (unsigned)d.size(),
        (unsigned)r);
    if (r != ERROR_SUCCESS)
    {
        XMPDeleteTitlePlaylist(list);
        return false;
    }
    if (g_playlist)
        XMPDeleteTitlePlaylist(g_playlist);     // (the one before, no longer playing)
    g_playlist = list;
    g_handles = songs;
    return true;
}

// After the current playlist changed: playing on from the song that was
// playing (from its start: the console's player can't change a playlist).
void Replay(int current)
{
    if (current < 0 || g_queue.empty() || PlayerState() == kIdle)
        return;
    PlayQueue(current < (int)g_queue.size() ? (size_t)current : g_queue.size() - 1);
}

std::string PlaylistPath(const std::wstring& name)
{
    return std::string(kPlaylists) + "\\" + Narrow(name) + ".m3u";
}

}  // namespace

size_t Find(const std::string& root, int depth)
{
    DWORD t0 = GetTickCount();
    g_songs.clear();
    Walk(root, depth);
    BuildViews();
    Log("music: %u songs in %s (%u ms): %u albums, %u artists, %u genres, %u saved playlists", (unsigned)g_songs.size(),
        root.c_str(), (unsigned)(GetTickCount() - t0), (unsigned)g_views[musiclib::kAlbums].size(),
        (unsigned)g_views[musiclib::kArtists].size(), (unsigned)g_views[musiclib::kGenres].size(),
        (unsigned)g_views[musiclib::kPlaylists].size());
    return g_songs.size();
}

size_t Count()
{
    return g_songs.size();
}

std::wstring Title(size_t i)
{
    return i < g_songs.size() ? g_songs[i].title : std::wstring();
}

std::wstring Artist(size_t i)
{
    return i < g_songs.size() ? g_songs[i].artist : std::wstring();
}

const Song& SongAt(size_t i)
{
    static const Song none;
    return i < g_songs.size() ? g_songs[i] : none;
}

const std::vector<Group>& Groups(musiclib::View view)
{
    return g_views[view >= 0 && view < 5 ? view : 0];
}

bool Play(const std::vector<size_t>& songs, size_t from)
{
    g_queue.clear();
    for (size_t i = 0; i < songs.size(); i++)
        if (songs[i] < g_songs.size())
            g_queue.push_back(g_songs[songs[i]]);
    return PlayQueue(from);
}

bool Play(size_t from)
{
    std::vector<size_t> all(g_songs.size());
    for (size_t i = 0; i < all.size(); i++)
        all[i] = i;
    return Play(all, from);
}

bool Add(const std::vector<size_t>& songs)
{
    int current = QueueCurrent();
    bool playing = PlayerState() != kIdle && current >= 0;
    for (size_t i = 0; i < songs.size(); i++)
        if (songs[i] < g_songs.size())
            g_queue.push_back(g_songs[songs[i]]);
    if (playing)
    {
        Replay(current);
        return true;
    }
    return PlayQueue(0);
}

size_t QueueCount()
{
    return g_queue.size();
}

const Song& QueueSong(size_t i)
{
    static const Song none;
    return i < g_queue.size() ? g_queue[i] : none;
}

int QueueCurrent()
{
    XMP_STATE state = XMP_STATE_IDLE;
    if (g_handles.empty() || XMPGetStatus(&state) != ERROR_SUCCESS || state == XMP_STATE_IDLE)
        return -1;
    XMP_SONGINFO info;
    ZeroMemory(&info, sizeof(info));
    if (XMPGetCurrentSong(&info, NULL) != ERROR_SUCCESS)
        return -1;
    for (size_t i = 0; i < g_handles.size(); i++)
        if (g_handles[i] == info.hSong)
            return (int)i;
    return -1;
}

bool PlayQueueFrom(size_t i)
{
    return PlayQueue(i);
}

void QueueMove(size_t i, int by)
{
    size_t j = (size_t)((int)i + by);
    if (i >= g_queue.size() || j >= g_queue.size())
        return;
    int current = QueueCurrent();
    std::swap(g_queue[i], g_queue[j]);
    if (current == (int)i)
        current = (int)j;
    else if (current == (int)j)
        current = (int)i;
    Replay(current);
}

void QueueRemove(size_t i)
{
    if (i >= g_queue.size())
        return;
    int current = QueueCurrent();
    g_queue.erase(g_queue.begin() + i);
    if (g_queue.empty())
    {
        Stop();
        return;
    }
    if (current > (int)i)
        current--;
    Replay(current);
}

void QueueClear()
{
    Stop();
    g_queue.clear();
}

State PlayerState()
{
    XMP_STATE state = XMP_STATE_IDLE;
    if (XMPGetStatus(&state) != ERROR_SUCCESS)
        return kIdle;
    return state == XMP_STATE_PLAYING ? kPlaying : state == XMP_STATE_PAUSED ? kPaused : kIdle;
}

void PlayPause()
{
    State s = PlayerState();
    if (s == kPlaying)
        XMPPause(NULL);
    else if (s == kPaused)
        XMPContinue(NULL);
    else
        PlayQueue(0);
}

void Stop()
{
    XMPStop(NULL);
}

void Next()
{
    XMPNext(NULL);
}

void Previous()
{
    XMPPrevious(NULL);
}

bool Shuffle()
{
    XMP_PLAYBACKMODE mode = XMP_PLAYBACKMODE_INORDER;
    XMP_REPEATMODE repeat = XMP_REPEATMODE_PLAYLIST;
    DWORD flags = 0;
    if (XMPGetPlaybackBehavior(&mode, &repeat, &flags) != ERROR_SUCCESS)
        return false;
    return mode == XMP_PLAYBACKMODE_SHUFFLE;
}

void SetShuffle(bool on)
{
    XMP_PLAYBACKMODE mode = XMP_PLAYBACKMODE_INORDER;
    XMP_REPEATMODE repeat = XMP_REPEATMODE_PLAYLIST;
    DWORD flags = 0;
    XMPGetPlaybackBehavior(&mode, &repeat, &flags);
    XMPSetPlaybackBehavior(on ? XMP_PLAYBACKMODE_SHUFFLE : XMP_PLAYBACKMODE_INORDER, repeat, flags, NULL);
}

bool NowPlaying(std::wstring& title, std::wstring& artist)
{
    XMP_STATE state = XMP_STATE_IDLE;
    // (a song just started reads idle for a moment)
    if (XMPGetStatus(&state) != ERROR_SUCCESS || state == XMP_STATE_IDLE)
        return false;
    XMP_SONGINFO info;
    ZeroMemory(&info, sizeof(info));
    if (XMPGetCurrentSong(&info, NULL) != ERROR_SUCCESS)
        return false;
    title = info.Details.wszTitle;
    artist = info.Details.wszArtist;
    return true;
}

SaveResult SavePlaylist(const std::wstring& name, bool replace)
{
    if (g_queue.empty())
        return kNothingToSave;
    std::string path = PlaylistPath(name);
    if (!replace && GetFileAttributesA(path.c_str()) != (DWORD)-1)
        return kExists;
    std::vector<std::string> paths;
    for (size_t i = 0; i < g_queue.size(); i++)
        if (!_strnicmp(g_queue[i].path.c_str(), "hdd:", 4))
            paths.push_back(g_queue[i].path);
    if (paths.empty())
        return kNothingToSave;
    CreateDirectoryA("hdd:\\SlopDash", NULL);
    CreateDirectoryA(kPlaylists, NULL);
    if (!WriteAll(path, musiclib::WriteM3u(paths)))
        return kSaveFailed;
    ReadPlaylists();
    Log("music: saved playlist %s (%u songs)", path.c_str(), (unsigned)paths.size());
    return paths.size() < g_queue.size() ? kSomeLeftOut : kSaved;
}

bool DeletePlaylist(const std::wstring& name)
{
    BOOL ok = DeleteFileA(PlaylistPath(name).c_str());
    ReadPlaylists();
    return ok != FALSE;
}

bool RenamePlaylist(const std::wstring& from, const std::wstring& to)
{
    BOOL ok = MoveFileA(PlaylistPath(from).c_str(), PlaylistPath(to).c_str());
    ReadPlaylists();
    return ok != FALSE;
}

size_t DeleteSongs(const std::vector<size_t>& songs)
{
    size_t gone = 0;
    std::vector<std::string> paths;
    for (size_t i = 0; i < songs.size(); i++)
        if (songs[i] < g_songs.size())
            paths.push_back(g_songs[songs[i]].path);
    for (size_t i = 0; i < paths.size(); i++)
        if (DeleteFileA(paths[i].c_str()))
            gone++;
    // The library without them (read again would take seconds: the songs left, kept).
    std::vector<Song> left;
    for (size_t i = 0; i < g_songs.size(); i++)
        if (GetFileAttributesA(g_songs[i].path.c_str()) != (DWORD)-1)
            left.push_back(g_songs[i]);
    g_songs.swap(left);
    BuildViews();
    Log("music: deleted %u of %u songs", (unsigned)gone, (unsigned)paths.size());
    return gone;
}

}  // namespace music
