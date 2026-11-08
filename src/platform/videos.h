// Videos for Media > Videos:
// the Windows Media videos (.wmv, .xmv: WMV9, what the console's own player,
// XMedia2, plays) on the hard drive, a USB drive and the disc, with their
// title, publisher (author) and description from the file's header. 1888's
// Video area showed these (videos#VideosMain.xur) and played them full screen
// (Video.xur). Other formats (MP4, DivX) need a decoder the console doesn't
// have, and aren't listed.
#pragma once

#include <string>
#include <vector>

namespace videos {

struct Video
{
    std::string path;
    std::wstring title, publisher, description;
    unsigned long long size;
};

// Looks on the drives (the drives' own folders - Content, Apps, Games... -
// left out). How many.
size_t Find();
size_t Count();
const Video& At(size_t i);
std::wstring SizeText(size_t i);        // "12.3 MB"

}  // namespace videos
