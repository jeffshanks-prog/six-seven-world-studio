/**
 * @file avi.h
 * @brief Director Mode's video files: a Motion JPEG AVI writer.
 *
 *        Each frame is a whole JPEG image, so the file plays in common
 *        players and editors (VLC, Windows Media Player, Premiere, DaVinci
 *        Resolve, ffmpeg) and needs no codec licence. The writer streams
 *        frames out as they arrive, keeps only the index in memory, and
 *        fills in the headers when the file is finished.
 *
 *        A file is kept under kMaxFileBytes (1 GiB), which every AVI reader
 *        takes; a longer export continues in a numbered part (partFileName).
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::director
{

inline constexpr std::uint64_t kMaxAviFileBytes = 1ull << 30;
inline constexpr int kMaxVideoSide = 8192;

/// True when `data` holds a JPEG image: starts with SOI, ends with EOI.
bool looksLikeJpeg(const unsigned char* data, std::size_t size);

/// "stem.avi" for part 1, "stem-part2.avi" for part 2, and so on.
std::string partFileName(std::string_view stem, int part);

class MjpegAviWriter
{
public:
    /// Writes the headers to `out`, which must be seekable and stay open
    /// until finish(). Refuses sizes outside 1..kMaxVideoSide and frame
    /// rates outside 1..60.
    bool begin(std::ostream& out, int width, int height, int fps, std::string* why = nullptr);

    /// Whether a frame of `jpegBytes` still fits in this file.
    bool canAdd(std::size_t jpegBytes) const;

    /// Appends one JPEG frame. Refuses anything that is not a JPEG, or that
    /// would take the file past kMaxAviFileBytes.
    bool addFrame(const unsigned char* jpeg, std::size_t size, std::string* why = nullptr);

    /// Writes the index and fills in the headers. The file is unreadable
    /// until this has run.
    bool finish(std::string* why = nullptr);

    int frames() const { return static_cast<int>(mIndex.size()); }
    std::uint64_t bytes() const { return mBytes; }
    bool open() const { return mOut != nullptr; }

private:
    struct Entry
    {
        std::uint32_t offset;
        std::uint32_t size;
    };

    std::ostream* mOut = nullptr;
    std::streamoff mStart = 0;
    int mWidth = 0;
    int mHeight = 0;
    int mFps = 0;
    std::uint64_t mBytes = 0;      ///< Everything written so far, the index excluded.
    std::uint32_t mLargest = 0;
    std::vector<Entry> mIndex;
};

} // namespace sw67::director
