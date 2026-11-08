// Pictures for Media > Pictures: found on a drive, and loaded as textures no
// bigger than they're shown (a camera's photo, loaded whole, would be a 48 MB
// texture for every thumbnail).
#pragma once

#include <xtl.h>
#include <xui.h>
#include <string>

namespace pictures {

// Looks for pictures (.jpg, .jpeg, .png, .bmp) in root and up to `depth`
// folders below it (the drives' own folders - Content, Apps, Games... - left
// out); they become the list the locators below name. How many.
size_t Find(const std::string& root, int depth, bool videos = false);   // videos: the recorder's .avi files only
size_t Count();
std::string Path(size_t i);
std::wstring Name(size_t i);                    // the file's name

// "slopdash://picture/<i>/thumb" (at most 320 x 240) or ".../full" (at most
// the screen, 1920 x 1080), for an image element.
std::wstring Locator(size_t i, bool thumb);
bool IsLocator(LPCWSTR name);

// The screen recorder's clips (Media > Recordings: Motion-JPEG AVI, made by
// SlopGuide, plugin/Recorder.inc): one of the list's a video, its frames and
// speed, and a locator for each frame (its JPEG, read out of the AVI).
bool IsVideo(size_t i);
unsigned VideoFrames(size_t i);
unsigned VideoFps(size_t i);
std::wstring FrameLocator(size_t i, unsigned frame);

// The texture loader's part (main.cpp): one of these locators as a texture.
HRESULT Texture(IXuiDevice* device, LPCWSTR name, XUIImageInfo* info, IDirect3DTexture9** tex);

}  // namespace pictures
