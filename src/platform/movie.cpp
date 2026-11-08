#include "platform/movie.h"

#include <xtl.h>
#include <xaudio2.h>
#include <xmedia2.h>

#include "platform/log.h"

namespace movie {

namespace {

IDirect3DDevice9* g_device = NULL;
IXAudio2* g_audio = NULL;
IXMedia2XmvPlayer* g_player = NULL;
bool g_paused = false, g_ended = false;

// The video's whole picture on the screen at its own shape (black bars).
void Fit()
{
    XMEDIA_VIDEO_DESCRIPTOR v;
    ZeroMemory(&v, sizeof(v));
    D3DSURFACE_DESC back;
    IDirect3DSurface9* bb = NULL;
    if (FAILED(IXMedia2XmvPlayer_GetVideoDescriptor(g_player, &v)) || !v.dwWidth || !v.dwHeight ||
        FAILED(g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb)
        return;
    bb->GetDesc(&back);
    bb->Release();
    float sx = (float)back.Width / v.dwWidth, sy = (float)back.Height / v.dwHeight;
    float s = sx < sy ? sx : sy;
    LONG w = (LONG)(v.dwWidth * s), h = (LONG)(v.dwHeight * s);
    RECT r = { ((LONG)back.Width - w) / 2, ((LONG)back.Height - h) / 2, 0, 0 };
    r.right = r.left + w;
    r.bottom = r.top + h;
    IXMedia2XmvPlayer_SetRectangle(g_player, &r);
    Log("movie: %ux%u at %.2f fps, %u ms, shown %dx%d", (unsigned)v.dwWidth, (unsigned)v.dwHeight, v.fFrameRate,
        (unsigned)v.dwClipDuration, (int)w, (int)h);
}

}  // namespace

void Attach(IDirect3DDevice9* device, IXAudio2* audio)
{
    g_device = device;
    g_audio = audio;
}

bool Start(const std::string& path)
{
    Stop();
    if (!g_device || !g_audio)
        return false;
    XMEDIA_XMV_CREATE_PARAMETERS p;
    ZeroMemory(&p, sizeof(p));
    p.createType = XMEDIA_CREATE_FROM_FILE;
    p.createFromFile.szFileName = path.c_str();
    p.createFromFile.dwIoBlockSize = 64 * 1024;
    p.createFromFile.dwIoBlockCount = 8;
    p.dwAudioStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;
    p.dwVideoStreamId = XMEDIA_STREAM_ID_USE_DEFAULT;
    HRESULT hr = XMedia2CreateXmvPlayer(g_device, g_audio, &p, &g_player);
    Log("movie: %s: %08X", path.c_str(), (unsigned)hr);
    if (FAILED(hr))
    {
        g_player = NULL;
        return false;
    }
    g_paused = g_ended = false;
    Fit();
    return true;
}

void Stop()
{
    if (!g_player)
        return;
    IXMedia2XmvPlayer_Stop(g_player, 0);
    IXMedia2XmvPlayer_Release(g_player);
    g_player = NULL;
    g_paused = false;
}

void Pause(bool pause)
{
    if (!g_player || pause == g_paused)
        return;
    if (pause)
        IXMedia2XmvPlayer_Pause(g_player);
    else
        IXMedia2XmvPlayer_Resume(g_player);
    g_paused = pause;
}

bool Paused()
{
    return g_paused;
}

bool Active()
{
    return g_player != NULL;
}

bool Ended()
{
    return g_ended;
}

void Draw()
{
    if (!g_player || g_ended)
        return;
    // Each frame at its time (the call waits for it: the dashboard runs at
    // the video's pace while it plays). Not waiting (DISABLE_AV_SYNC with
    // REPEAT_FRAME) showed the first frame for ever.
    HRESULT hr = IXMedia2XmvPlayer_RenderNextFrame(g_player, 0, NULL);
    static unsigned s_calls = 0;
    static HRESULT s_last = S_OK;
    if (++s_calls <= 5 || hr != s_last || s_calls % 300 == 0)
        Log("movie: frame call %u: %08X", s_calls, (unsigned)hr);
    s_last = hr;
    if (hr == XMEDIA_W_EOF)
    {
        g_ended = true;
        Log("movie: ended");
    }
    else if (FAILED(hr))
    {
        g_ended = true;
        Log("movie: %08X: stopped", (unsigned)hr);
    }
}

}  // namespace movie
