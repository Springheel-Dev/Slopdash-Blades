// Baseline JPEG encoding (one frame of a Motion-JPEG recording) and the AVI
// file that holds them, for the screen recorder (plugin/SlopGuide: the
// console's screen, recorded to Hdd:\SlopDash\Recordings; SlopDash's Media >
// Recordings plays them). Plain C++, no system calls: tested on the PC
// (tests/test_mjpeg.cpp).
#pragma once

#include <stddef.h>
#include <vector>

namespace mjpeg {

// The JPEG of an image: rgb is width x height pixels, 3 bytes each (R, G, B),
// rows of stride bytes. quality 1-100. YCbCr 4:2:0, the standard tables.
// Appends to out.
void Encode(const unsigned char* rgb, int width, int height, int stride, int quality,
            std::vector<unsigned char>& out);
// The same into out (capacity bytes), allocating nothing: its size, or 0 if
// it didn't fit.
size_t Encode(const unsigned char* rgb, int width, int height, int stride, int quality, unsigned char* out,
              size_t capacity);

// An AVI being written (RIFF 'AVI ', one video stream 'MJPG'), its bytes in
// memory piece by piece: the caller writes each Take() to the file and, at
// the end, Header() over the file's start and Index() at its end.
class AviWriter
{
public:
    AviWriter(int width, int height, int fps);
    // The file's first bytes (headers and the movie list's start); written
    // again by Finish once the frame count is known. Always the same size.
    std::vector<unsigned char> Header() const;
    // The same into out (kHeaderSize bytes), allocating nothing: its size.
    static const size_t kHeaderSize = 224;
    size_t Header(unsigned char* out) const;
    // One frame's chunk ('00dc' and the JPEG): to be appended to the file.
    std::vector<unsigned char> Frame(const std::vector<unsigned char>& jpeg);
    // The end: the index ('idx1'), to append; then Header() again at offset 0.
    std::vector<unsigned char> Index() const;
    // A frame of jpegSize bytes written by the caller itself ('00dc', its
    // size, the JPEG, a 0 if odd), allocating nothing: its offset for the
    // index, which the caller keeps and writes (Index() has only Frame()'s).
    unsigned Count(size_t jpegSize);
    size_t Frames() const { return m_frames; }
    size_t MovieBytes() const { return m_movie; }   // the movie list's frames so far

private:
    template <class O>
    void HeaderTo(O& o) const;
    int m_width, m_height, m_fps;
    size_t m_movie;                                 // bytes of frame chunks so far
    size_t m_frames;
    std::vector<unsigned> m_offsets, m_sizes;       // each frame's place in the movie list
    unsigned m_largest;
};

}  // namespace mjpeg
