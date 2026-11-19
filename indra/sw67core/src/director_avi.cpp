/**
 * @file director_avi.cpp
 * @brief Director Mode's Motion JPEG AVI writer.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/director/avi.h"

#include <algorithm>
#include <array>

namespace sw67::director
{

namespace
{

// The layout written by begin(), as offsets from the file's start.
constexpr std::uint32_t kHeaderBytes = 12 + 8 + 4 + (8 + 56) + 8 + 4 + (8 + 56) + (8 + 40) + 12;
constexpr std::streamoff kRiffSize = 4;
constexpr std::streamoff kAvihMaxBytesPerSec = 12 + 8 + 4 + 8 + 4;
constexpr std::streamoff kAvihTotalFrames = 12 + 8 + 4 + 8 + 16;
constexpr std::streamoff kAvihSuggestedBuffer = 12 + 8 + 4 + 8 + 28;
constexpr std::streamoff kStrh = 12 + 8 + 4 + (8 + 56) + 8 + 4 + 8;
constexpr std::streamoff kStrhLength = kStrh + 32;
constexpr std::streamoff kStrhSuggestedBuffer = kStrh + 36;
constexpr std::streamoff kMoviSize = kHeaderBytes - 8;

void put32(std::string& s, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xffu));
}

void put16(std::string& s, std::uint16_t v)
{
    s.push_back(static_cast<char>(v & 0xffu));
    s.push_back(static_cast<char>((v >> 8) & 0xffu));
}

void fourcc(std::string& s, const char* c) { s.append(c, 4); }

void patch32(std::ostream& out, std::streamoff at, std::uint32_t v)
{
    std::string s;
    put32(s, v);
    out.seekp(at);
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

void fail(std::string* why, const char* text)
{
    if (why) *why = text;
}

} // namespace

bool looksLikeJpeg(const unsigned char* data, std::size_t size)
{
    return data && size >= 4 && data[0] == 0xff && data[1] == 0xd8 && data[size - 2] == 0xff && data[size - 1] == 0xd9;
}

std::string partFileName(std::string_view stem, int part)
{
    std::string name(stem.empty() ? std::string_view("video") : stem);
    if (part > 1) name += "-part" + std::to_string(part);
    return name + ".avi";
}

bool MjpegAviWriter::begin(std::ostream& out, int width, int height, int fps, std::string* why)
{
    if (mOut) return fail(why, "A video file is already open."), false;
    if (width < 1 || height < 1 || width > kMaxVideoSide || height > kMaxVideoSide)
        return fail(why, "The video size is out of range."), false;
    if (fps < 1 || fps > 60) return fail(why, "The frame rate must be 1 to 60."), false;

    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    std::string s;
    fourcc(s, "RIFF");
    put32(s, 0); // patched
    fourcc(s, "AVI ");

    fourcc(s, "LIST");
    put32(s, 4 + (8 + 56) + 8 + 4 + (8 + 56) + (8 + 40));
    fourcc(s, "hdrl");

    fourcc(s, "avih");
    put32(s, 56);
    put32(s, 1000000u / static_cast<std::uint32_t>(fps)); // microseconds per frame
    put32(s, 0);                                          // max bytes per second, patched
    put32(s, 0);                                          // padding granularity
    put32(s, 0x10u | 0x100u);                             // has index, interleaved
    put32(s, 0);                                          // total frames, patched
    put32(s, 0);                                          // initial frames
    put32(s, 1);                                          // streams
    put32(s, 0);                                          // suggested buffer, patched
    put32(s, w);
    put32(s, h);
    for (int i = 0; i < 4; ++i) put32(s, 0);

    fourcc(s, "LIST");
    put32(s, 4 + (8 + 56) + (8 + 40));
    fourcc(s, "strl");

    fourcc(s, "strh");
    put32(s, 56);
    fourcc(s, "vids");
    fourcc(s, "MJPG");
    put32(s, 0);                                // flags
    put16(s, 0);                                // priority
    put16(s, 0);                                // language
    put32(s, 0);                                // initial frames
    put32(s, 1);                                // scale
    put32(s, static_cast<std::uint32_t>(fps));  // rate
    put32(s, 0);                                // start
    put32(s, 0);                                // length, patched
    put32(s, 0);                                // suggested buffer, patched
    put32(s, 0xffffffffu);                      // quality: default
    put32(s, 0);                                // sample size: varies
    put16(s, 0);
    put16(s, 0);
    put16(s, static_cast<std::uint16_t>(w));
    put16(s, static_cast<std::uint16_t>(h));

    fourcc(s, "strf");
    put32(s, 40);
    put32(s, 40);
    put32(s, w);
    put32(s, h);
    put16(s, 1);  // planes
    put16(s, 24); // bits per pixel
    fourcc(s, "MJPG");
    put32(s, w * h * 3);
    for (int i = 0; i < 4; ++i) put32(s, 0);

    fourcc(s, "LIST");
    put32(s, 0); // patched
    fourcc(s, "movi");

    if (s.size() != kHeaderBytes) return fail(why, "Internal error laying out the header."), false;

    mStart = out.tellp();
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
    if (!out || mStart < 0) return fail(why, "Could not write the video file."), false;
    mOut = &out;
    mWidth = width;
    mHeight = height;
    mFps = fps;
    mBytes = kHeaderBytes;
    mLargest = 0;
    mIndex.clear();
    return true;
}

bool MjpegAviWriter::canAdd(std::size_t jpegBytes) const
{
    const std::uint64_t chunk = 8 + jpegBytes + (jpegBytes & 1u);
    const std::uint64_t index = 8 + 16 * (mIndex.size() + 1);
    return mOut && mBytes + chunk + index <= kMaxAviFileBytes;
}

bool MjpegAviWriter::addFrame(const unsigned char* jpeg, std::size_t size, std::string* why)
{
    if (!mOut) return fail(why, "No video file is open."), false;
    if (!looksLikeJpeg(jpeg, size)) return fail(why, "The frame is not a JPEG image."), false;
    if (!canAdd(size)) return fail(why, "The video file is full."), false;

    std::string head;
    fourcc(head, "00dc");
    put32(head, static_cast<std::uint32_t>(size));
    mOut->write(head.data(), 8);
    mOut->write(reinterpret_cast<const char*>(jpeg), static_cast<std::streamsize>(size));
    if (size & 1u) mOut->put('\0');
    if (!*mOut) return fail(why, "Could not write the video file (is the disk full?)."), false;

    // idx1 offsets count from the 'movi' fourcc.
    const auto offset = static_cast<std::uint32_t>(mBytes - (kHeaderBytes - 4));
    mIndex.push_back({offset, static_cast<std::uint32_t>(size)});
    mLargest = std::max(mLargest, static_cast<std::uint32_t>(size));
    mBytes += 8 + size + (size & 1u);
    return true;
}

bool MjpegAviWriter::finish(std::string* why)
{
    if (!mOut) return fail(why, "No video file is open."), false;
    std::ostream& out = *mOut;
    mOut = nullptr;

    std::string idx;
    fourcc(idx, "idx1");
    put32(idx, static_cast<std::uint32_t>(16 * mIndex.size()));
    for (const Entry& e : mIndex)
    {
        fourcc(idx, "00dc");
        put32(idx, 0x10u); // key frame
        put32(idx, e.offset);
        put32(idx, e.size);
    }
    out.seekp(mStart + static_cast<std::streamoff>(mBytes));
    out.write(idx.data(), static_cast<std::streamsize>(idx.size()));
    const std::uint64_t total = mBytes + idx.size();

    const auto frames = static_cast<std::uint32_t>(mIndex.size());
    const std::uint32_t buffer = mLargest + 8;
    const std::uint64_t perSec = static_cast<std::uint64_t>(buffer) * static_cast<std::uint64_t>(mFps);
    patch32(out, mStart + kRiffSize, static_cast<std::uint32_t>(total - 8));
    patch32(out, mStart + kAvihMaxBytesPerSec, static_cast<std::uint32_t>(std::min<std::uint64_t>(perSec, 0xffffffffu)));
    patch32(out, mStart + kAvihTotalFrames, frames);
    patch32(out, mStart + kAvihSuggestedBuffer, buffer);
    patch32(out, mStart + kStrhLength, frames);
    patch32(out, mStart + kStrhSuggestedBuffer, buffer);
    patch32(out, mStart + kMoviSize, static_cast<std::uint32_t>(mBytes - (kHeaderBytes - 4)));
    out.seekp(mStart + static_cast<std::streamoff>(total));
    out.flush();
    mIndex.clear();
    if (!out) return fail(why, "Could not finish the video file (is the disk full?)."), false;
    return true;
}

} // namespace sw67::director
