#include "platform/pictures.h"

#include <xtl.h>
#include <xui.h>
#include <d3dx9.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "platform/log.h"

namespace pictures {

namespace {

std::vector<std::string> g_paths;
const size_t kMost = 1000;                      // (a whole stick of photos stops here)
const wchar_t kPrefix[] = L"slopdash://picture/";

bool IsPicture(const char* name)
{
    const char* dot = strrchr(name, '.');
    if (!dot)
        return false;
    return !_stricmp(dot, ".jpg") || !_stricmp(dot, ".jpeg") || !_stricmp(dot, ".png") || !_stricmp(dot, ".bmp");
}

// The drives' own folders, where pictures aren't kept (and walking them is slow).
bool SkipFolder(const char* name)
{
    static const char* const kSkip[] = { "Content", "Apps", "Games", "Emulators", "Cache", "Xbox360",
                                         "$SystemUpdate", "SlopDash", "StoreDownloads", "Compatibility" };
    for (size_t i = 0; i < sizeof(kSkip) / sizeof(kSkip[0]); i++)
        if (!_stricmp(name, kSkip[i]))
            return true;
    return name[0] == '.' || name[0] == '$';
}

bool IsAvi(const std::string& path);
extern bool g_videos;

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
        else if ((g_videos ? IsAvi(fd.cFileName) : IsPicture(fd.cFileName)) && g_paths.size() < kMost)
            g_paths.push_back(dir + "\\" + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (size_t i = 0; i < subdirs.size() && g_paths.size() < kMost; i++)
        Walk(dir + "\\" + subdirs[i], depth - 1);
}

// --- Recordings (Motion-JPEG AVI) ------------------------------------------------------
// One clip's frames known at a time (the one being shown or listed): where each
// frame's JPEG is in the file. From its index (idx1); a clip cut off before its
// index was written (the console turned off while recording) by walking its
// movie list's chunks.
struct AviIndex
{
    std::string path;
    unsigned fps;
    std::vector<unsigned> offsets, sizes;       // each frame's JPEG: its place in the file and size
};
AviIndex g_avi;

bool IsAvi(const std::string& path)
{
    size_t dot = path.find_last_of('.');
    return dot != std::string::npos && !_stricmp(path.c_str() + dot, ".avi");
}

unsigned Le32(const unsigned char* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

bool ReadAt(HANDLE h, unsigned at, void* out, unsigned size)
{
    if (SetFilePointer(h, (LONG)at, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        return false;
    DWORD got = 0;
    return ReadFile(h, out, size, &got, NULL) && got == size;
}

const AviIndex* LoadAvi(const std::string& path)
{
    if (g_avi.path == path)
        return &g_avi;
    g_avi.path = path;
    g_avi.fps = 30;
    g_avi.offsets.clear();
    g_avi.sizes.clear();
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return &g_avi;
    unsigned size = GetFileSize(h, NULL);
    unsigned char head[12];
    unsigned movi = 0, moviEnd = 0, idx = 0, idxSize = 0;
    if (ReadAt(h, 0, head, 12) && !memcmp(head, "RIFF", 4) && !memcmp(head + 8, "AVI ", 4))
    {
        for (unsigned at = 12; at + 8 <= size;)
        {
            unsigned char ck[12];
            if (!ReadAt(h, at, ck, 12))
                break;
            unsigned len = Le32(ck + 4);
            if (!memcmp(ck, "LIST", 4) && !memcmp(ck + 8, "hdrl", 4))
            {
                unsigned char avih[16];
                if (ReadAt(h, at + 12 + 8, avih, 16) && Le32(avih))
                    g_avi.fps = (1000000 + Le32(avih) / 2) / Le32(avih);   // (microseconds a frame)
            }
            else if (!memcmp(ck, "LIST", 4) && !memcmp(ck + 8, "movi", 4))
            {
                movi = at + 8;                                  // (the 'movi' tag: idx1's offsets count from it)
                moviEnd = len && at + 8 + len <= size ? at + 8 + len : size;
            }
            else if (!memcmp(ck, "idx1", 4))
            {
                idx = at + 8;
                idxSize = len;
            }
            if (!len && !memcmp(ck, "LIST", 4))
                break;                                          // (cut off: its size never written)
            at += 8 + len + (len & 1);
        }
    }
    if (movi && idx && idxSize >= 16)
    {
        std::vector<unsigned char> entries(idxSize);
        if (ReadAt(h, idx, &entries[0], idxSize))
            for (unsigned i = 0; i + 16 <= idxSize; i += 16)
                if (!memcmp(&entries[i], "00dc", 4))
                {
                    g_avi.offsets.push_back(movi + Le32(&entries[i + 8]) + 8);
                    g_avi.sizes.push_back(Le32(&entries[i + 12]));
                }
    }
    else if (movi)
    {
        // No index: the movie list's chunks, one by one, to the end of what's there.
        for (unsigned at = movi + 4; at + 8 <= (moviEnd ? moviEnd : size);)
        {
            unsigned char ck[8];
            if (!ReadAt(h, at, ck, 8))
                break;
            unsigned len = Le32(ck + 4);
            if (at + 8 + len > size)
                break;
            if (!memcmp(ck, "00dc", 4))
            {
                g_avi.offsets.push_back(at + 8);
                g_avi.sizes.push_back(len);
            }
            at += 8 + len + (len & 1);
        }
    }
    CloseHandle(h);
    Log("recording %s: %u frames, %u a second%s", path.c_str(), (unsigned)g_avi.sizes.size(), g_avi.fps,
        idx ? "" : " (no index: walked)");
    return &g_avi;
}

// A frame's JPEG bytes.
bool AviFrame(const std::string& path, unsigned frame, std::vector<unsigned char>& jpeg)
{
    const AviIndex* a = LoadAvi(path);
    if (frame >= a->sizes.size() || a->sizes[frame] > 4 * 1024 * 1024)
        return false;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    jpeg.resize(a->sizes[frame]);
    bool ok = ReadAt(h, a->offsets[frame], &jpeg[0], a->sizes[frame]);
    CloseHandle(h);
    return ok;
}

bool g_videos = false;              // the list is the recorder's clips (Find)

}  // namespace

size_t Find(const std::string& root, int depth, bool videos)
{
    DWORD t0 = GetTickCount();
    g_paths.clear();
    g_videos = videos;
    Walk(root, depth);
    Log("pictures: %u in %s (%u ms)", (unsigned)g_paths.size(), root.c_str(), (unsigned)(GetTickCount() - t0));
    return g_paths.size();
}

size_t Count()
{
    return g_paths.size();
}

std::string Path(size_t i)
{
    return i < g_paths.size() ? g_paths[i] : std::string();
}

std::wstring Name(size_t i)
{
    std::string p = Path(i);
    std::string n = p.substr(p.find_last_of('\\') + 1);
    return std::wstring(n.begin(), n.end());
}

std::wstring Locator(size_t i, bool thumb)
{
    wchar_t b[64];
    _snwprintf_s(b, 64, _TRUNCATE, L"%s%u/%s", kPrefix, (unsigned)i, thumb ? L"thumb" : L"full");
    return b;
}

bool IsLocator(LPCWSTR name)
{
    return name && !wcsncmp(name, kPrefix, ARRAYSIZE(kPrefix) - 1);
}

bool IsVideo(size_t i)
{
    return IsAvi(Path(i));
}

unsigned VideoFrames(size_t i)
{
    return IsVideo(i) ? (unsigned)LoadAvi(Path(i))->sizes.size() : 0;
}

unsigned VideoFps(size_t i)
{
    return IsVideo(i) ? LoadAvi(Path(i))->fps : 0;
}

std::wstring FrameLocator(size_t i, unsigned frame)
{
    wchar_t b[64];
    _snwprintf_s(b, 64, _TRUNCATE, L"%s%u/f%u", kPrefix, (unsigned)i, frame);
    return b;
}

HRESULT Texture(IXuiDevice* device, LPCWSTR name, XUIImageInfo* info, IDirect3DTexture9** tex)
{
    const wchar_t* rest = name + ARRAYSIZE(kPrefix) - 1;
    size_t i = (size_t)_wtoi(rest);
    const wchar_t* slash = wcschr(rest, L'/');
    bool thumb = slash && !wcscmp(slash + 1, L"thumb");
    std::string path = Path(i);
    if (path.empty() || !device)
        return E_FAIL;
    if (IsAvi(path))
    {
        // A recording's frame: "thumb" and "full" its first, "f<n>" its nth.
        unsigned frame = slash && slash[1] == L'f' && slash[2] != L'u' ? (unsigned)_wtoi(slash + 2) : 0;
        std::vector<unsigned char> jpeg;
        if (!AviFrame(path, frame, jpeg))
            return E_FAIL;
        D3DXIMAGE_INFO fi;
        HRESULT fr = D3DXCreateTextureFromFileInMemoryEx(device->GetD3DDevice(), &jpeg[0], (UINT)jpeg.size(),
                                                        D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1, 0,
                                                        D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE,
                                                        D3DX_DEFAULT, 0, &fi, NULL, tex);
        if (FAILED(fr))
            return fr;
        if (info)
        {
            ZeroMemory(info, sizeof(*info));
            info->Width = fi.Width;
            info->Height = fi.Height;
            info->Depth = 1;
            info->MipLevels = 1;
            info->ImageFileFormat = fi.ImageFileFormat;
            info->Format = D3DFMT_A8R8G8B8;
            info->ResourceType = D3DRTYPE_TEXTURE;
        }
        return S_OK;
    }
    D3DXIMAGE_INFO ii;
    HRESULT hr = D3DXGetImageInfoFromFileA(path.c_str(), &ii);
    if (FAILED(hr) || !ii.Width || !ii.Height)
    {
        Log("picture %s: %08X", path.c_str(), (unsigned)hr);
        return FAILED(hr) ? hr : E_FAIL;
    }
    // Scaled to fit the box, its shape kept; never enlarged.
    float boxW = thumb ? 320.0f : 1920.0f, boxH = thumb ? 240.0f : 1080.0f;
    float scale = min(boxW / ii.Width, boxH / ii.Height);
    if (scale > 1.0f)
        scale = 1.0f;
    UINT w = max(1u, (UINT)(ii.Width * scale + 0.5f)), h = max(1u, (UINT)(ii.Height * scale + 0.5f));
    DWORD t0 = GetTickCount();
    hr = D3DXCreateTextureFromFileExA(device->GetD3DDevice(), path.c_str(), w, h, 1, 0, D3DFMT_A8R8G8B8,
                                      D3DPOOL_MANAGED, D3DX_FILTER_LINEAR, D3DX_DEFAULT, 0, NULL, NULL, tex);
    DWORD ms = GetTickCount() - t0;
    if (FAILED(hr) || ms > 200)
        Log("picture %s (%ux%u as %ux%u): %08X, %u ms", path.c_str(), ii.Width, ii.Height, w, h, (unsigned)hr,
            (unsigned)ms);
    if (FAILED(hr))
        return hr;
    if (info)
    {
        ZeroMemory(info, sizeof(*info));
        info->Width = w;
        info->Height = h;
        info->Depth = 1;
        info->MipLevels = 1;
        info->ImageFileFormat = ii.ImageFileFormat;
        info->Format = D3DFMT_A8R8G8B8;
        info->ResourceType = D3DRTYPE_TEXTURE;
    }
    return S_OK;
}

}  // namespace pictures
