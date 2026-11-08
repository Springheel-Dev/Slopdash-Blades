// A video playing full screen (Media > Videos: theme/video1888.inc), by the
// console's own XMedia2 player (WMV9: .wmv, .xmv). This XDK has no library for
// XUI's video element, so the player draws each frame itself, over what XUI
// drew and before the frame is presented (main.cpp's Render).
#pragma once

#include <string>

struct IDirect3DDevice9;
struct IXAudio2;

namespace movie {

// The device and the title's XAudio2, once both exist (main.cpp).
void Attach(IDirect3DDevice9* device, IXAudio2* audio);

bool Start(const std::string& path);
void Stop();
void Pause(bool pause);
bool Paused();
bool Active();              // a video is up (playing or paused)
bool Ended();               // it played to its end (Stop clears it)

// Its next frame (or the last again), drawn now: between XUI's drawing and
// the present.
void Draw();

}  // namespace movie
