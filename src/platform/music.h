// Music for Media > Music. Songs found on a drive
// (.mp3, .wma), their tags read (core/musiclib.h), grouped as 1888's library
// shows them (Albums, Artists, Saved Playlists, Songs, Genres), played by the
// console's own music player (XMP: the Guide's player shows them, and they
// play on in games). The current playlist (1888's Music Player and Edit
// Current Playlist) is kept here; saved playlists are .m3u files in
// hdd:\SlopDash\Playlists.
#pragma once

#include <string>
#include <vector>

#include "core/musiclib.h"

namespace music {

// --- The library (one source at a time: Hard Drive, Current Disc, Portable Device)
// Looks for songs in root and up to `depth` folders below it (the drives' own
// folders - Content, Apps, Games... - left out). How many.
size_t Find(const std::string& root, int depth);
size_t Count();
std::wstring Title(size_t i);
std::wstring Artist(size_t i);
const musiclib::Song& SongAt(size_t i);

// A view's entries: albums, artists, genres, saved playlists (by name; a
// playlist's songs are the library's songs it names), or every song by title
// (kSongs: one group, "Songs").
const std::vector<musiclib::Group>& Groups(musiclib::View view);

// --- The current playlist and the console's player
// Plays these library songs (in this order) from song `from` of them: the
// current playlist becomes them.
bool Play(const std::vector<size_t>& songs, size_t from);
// Every song found, from song `from` on (the library's own order).
bool Play(size_t from);
// Adds library songs to the end of the current playlist (playing on).
bool Add(const std::vector<size_t>& songs);

size_t QueueCount();
const musiclib::Song& QueueSong(size_t i);
int QueueCurrent();                 // the one playing now, -1 if none
bool PlayQueueFrom(size_t i);
void QueueMove(size_t i, int by);   // up (-1) or down (+1)
void QueueRemove(size_t i);
void QueueClear();

enum State { kIdle, kPlaying, kPaused };
State PlayerState();
void PlayPause();
void Stop();
void Next();
void Previous();
bool Shuffle();
void SetShuffle(bool on);

// The song playing now (the console's player), if any.
bool NowPlaying(std::wstring& title, std::wstring& artist);

// --- Saved playlists (hard drive songs only, as 1888's)
enum SaveResult { kSaved, kExists, kSomeLeftOut, kSaveFailed, kNothingToSave };
// The current playlist under `name`; kExists unless `replace`. Songs not on
// the hard drive are left out (kSomeLeftOut, saved anyway).
SaveResult SavePlaylist(const std::wstring& name, bool replace);
bool DeletePlaylist(const std::wstring& name);
bool RenamePlaylist(const std::wstring& from, const std::wstring& to);

// --- Deleting from the drive (1888's Delete Song / Delete Album)
// The files of these library songs; the library is read again. How many went.
size_t DeleteSongs(const std::vector<size_t>& songs);

}  // namespace music
