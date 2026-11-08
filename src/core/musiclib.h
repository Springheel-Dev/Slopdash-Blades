// The music library's logic, for Media > Music (1888's music area: Albums,
// Artists, Saved Playlists, Songs, Genres; music#1003_IndividualDevice.xur):
// songs' tags read from their files (MP3: ID3v2, else ID3v1; WMA: the ASF
// header), songs grouped by album, artist and genre, and saved playlists as
// .m3u files. Plain C++, no system calls: tested on the PC
// (tests/test_musiclib.cpp). platform/music.cpp finds the files and plays them.
#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace musiclib {

struct Tags
{
    std::wstring title, artist, album, genre;
    std::wstring description;               // (ASF's: a video's, for Media > Videos)
    int track;                              // 0: none
    Tags() : track(0) {}
};

// From the start of an MP3 (its ID3v2 tag, versions 2-4): false if it has none.
bool ReadId3v2(const unsigned char* data, size_t size, Tags& tags);
// From an MP3's last 128 bytes (ID3v1): fills only what's still empty.
bool ReadId3v1(const unsigned char* last128, Tags& tags);
// From the start of a WMA (its ASF header: Content Description, Extended
// Content Description).
bool ReadAsf(const unsigned char* data, size_t size, Tags& tags);
// An ID3 genre as written ("(17)", "17", "(17)Rock", "Rock"): its name.
std::wstring GenreName(const std::wstring& raw);

struct Song
{
    std::string path;
    std::wstring title, artist, album, genre;
    int track;
    bool mp3;
    Song() : track(0), mp3(true) {}
};

// 1888's music library views, in the order of its buttons.
enum View { kAlbums, kArtists, kPlaylists, kSongs, kGenres };

struct Group
{
    std::wstring name;                      // the album, artist or genre
    std::wstring detail;                    // an album's artist ("Various Artists" for several)
    std::vector<size_t> songs;              // into the song list, in playing order
};

// Albums, artists or genres, by name (case ignored); songs without one under
// "Unknown Album" / "Unknown Artist" / "Unknown Genre". An album's songs by
// track number, then title; an artist's and a genre's by album, then track.
std::vector<Group> GroupBy(const std::vector<Song>& songs, View view);
// Every song, by title.
std::vector<size_t> ByTitle(const std::vector<Song>& songs);

// A saved playlist (.m3u): its songs' paths, comments and blank lines left
// out; relative paths taken from `folder`.
std::vector<std::string> ParseM3u(const std::string& text, const std::string& folder);
std::string WriteM3u(const std::vector<std::string>& paths);

}  // namespace musiclib
