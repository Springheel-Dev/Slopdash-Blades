#include "platform/videos.h"

#include <xtl.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

#include "core/musiclib.h"
#include "platform/log.h"

namespace videos {

namespace {

std::vector<Video> g_videos;
const size_t kMost = 1000;

bool IsVideo(const char* name)
{
    const char* dot = strrchr(name, '.');
    return dot && (!_stricmp(dot, ".wmv") || !_stricmp(dot, ".xmv"));
}

// The drives' own folders (as music.cpp's).
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

void ReadHeader(Video& v, const char* fileName)
{
    HANDLE h = CreateFileA(v.path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        std::vector<unsigned char> head(64 * 1024);
        DWORD got = 0;
        if (!ReadFile(h, &head[0], (DWORD)head.size(), &got, NULL))
            got = 0;
        CloseHandle(h);
        musiclib::Tags tags;
        if (musiclib::ReadAsf(&head[0], got, tags))
        {
            v.title = tags.title;
            v.publisher = tags.artist;
            v.description = tags.description;
        }
    }
    if (v.title.empty())
    {
        std::string n = fileName;
        v.title = Widen(n.substr(0, n.find_last_of('.')));
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
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (depth > 0 && !SkipFolder(fd.cFileName))
                subdirs.push_back(fd.cFileName);
        }
        else if (IsVideo(fd.cFileName) && g_videos.size() < kMost)
        {
            Video v;
            v.path = dir + "\\" + fd.cFileName;
            v.size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            ReadHeader(v, fd.cFileName);
            g_videos.push_back(v);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (size_t i = 0; i < subdirs.size() && g_videos.size() < kMost; i++)
        Walk(dir + "\\" + subdirs[i], depth - 1);
}

struct ByTitle
{
    bool operator()(const Video& a, const Video& b) const { return _wcsicmp(a.title.c_str(), b.title.c_str()) < 0; }
};

}  // namespace

size_t Find()
{
    DWORD t0 = GetTickCount();
    g_videos.clear();
    static const char* const kRoots[] = { "hdd:", "usb:", "dvd:" };
    for (size_t i = 0; i < sizeof(kRoots) / sizeof(kRoots[0]); i++)
        Walk(kRoots[i], 4);
    std::stable_sort(g_videos.begin(), g_videos.end(), ByTitle());
    Log("videos: %u found (%u ms)", (unsigned)g_videos.size(), (unsigned)(GetTickCount() - t0));
    return g_videos.size();
}

size_t Count()
{
    return g_videos.size();
}

const Video& At(size_t i)
{
    static const Video none = { "", L"", L"", L"", 0 };
    return i < g_videos.size() ? g_videos[i] : none;
}

std::wstring SizeText(size_t i)
{
    double mb = (double)At(i).size / (1024.0 * 1024.0);
    wchar_t b[32];
    if (mb >= 1024.0)
        _snwprintf_s(b, 32, _TRUNCATE, L"%.1f GB", mb / 1024.0);
    else
        _snwprintf_s(b, 32, _TRUNCATE, L"%.1f MB", mb);
    return b;
}

}  // namespace videos
