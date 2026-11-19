/**
 * @file avi_test.cpp
 * @brief Director Mode's video files: the AVI a reader sees, frame by frame,
 *        and the refusals.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/director/avi.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using namespace sw67::director;

namespace
{
std::vector<unsigned char> fakeJpeg(std::size_t size, unsigned char fill)
{
    std::vector<unsigned char> v(size, fill);
    v[0] = 0xff;
    v[1] = 0xd8;
    v[size - 2] = 0xff;
    v[size - 1] = 0xd9;
    return v;
}

std::uint32_t le32(const std::string& s, std::size_t at)
{
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + i])) << (8 * i);
    return v;
}

std::string tag(const std::string& s, std::size_t at) { return s.substr(at, 4); }

/// Walks the file as a reader does, by chunk sizes, and returns the frames.
struct Parsed
{
    std::uint32_t width = 0, height = 0, usPerFrame = 0, totalFrames = 0, streamLength = 0, rate = 0;
    std::string handler, compression;
    std::vector<std::string> frames;
    std::vector<std::uint32_t> indexSizes;
    bool ok = false;
};

Parsed parse(const std::string& s)
{
    Parsed p;
    if (s.size() < 12 || tag(s, 0) != "RIFF" || tag(s, 8) != "AVI ") return p;
    if (le32(s, 4) + 8 != s.size()) return p;
    std::size_t movi = 0, moviEnd = 0;
    bool index = false;
    for (std::size_t at = 12; at + 8 <= s.size();)
    {
        const std::string id = tag(s, at);
        const std::uint32_t size = le32(s, at + 4);
        if (at + 8 + size > s.size()) return p;
        if (id == "LIST" && tag(s, at + 8) == "hdrl")
        {
            const std::size_t avih = at + 12;
            if (tag(s, avih) != "avih" || le32(s, avih + 4) != 56) return p;
            p.usPerFrame = le32(s, avih + 8);
            p.totalFrames = le32(s, avih + 24);
            p.width = le32(s, avih + 40);
            p.height = le32(s, avih + 44);
            const std::size_t strl = avih + 8 + 56;
            if (tag(s, strl) != "LIST" || tag(s, strl + 8) != "strl") return p;
            const std::size_t strh = strl + 12;
            if (tag(s, strh) != "strh" || tag(s, strh + 8) != "vids") return p;
            p.handler = tag(s, strh + 12);
            p.rate = le32(s, strh + 8 + 24);
            p.streamLength = le32(s, strh + 8 + 32);
            const std::size_t strf = strh + 8 + 56;
            if (tag(s, strf) != "strf") return p;
            p.compression = tag(s, strf + 8 + 16);
        }
        else if (id == "LIST" && tag(s, at + 8) == "movi")
        {
            movi = at + 8;
            moviEnd = at + 8 + size;
            for (std::size_t c = at + 12; c < moviEnd;)
            {
                if (tag(s, c) != "00dc") return p;
                const std::uint32_t n = le32(s, c + 4);
                p.frames.push_back(s.substr(c + 8, n));
                c += 8 + n + (n & 1u);
            }
        }
        else if (id == "idx1")
        {
            index = true;
            for (std::size_t e = at + 8; e < at + 8 + size; e += 16)
            {
                if (tag(s, e) != "00dc" || le32(s, e + 4) != 0x10u) return p;
                const std::uint32_t off = le32(s, e + 8);
                const std::uint32_t n = le32(s, e + 12);
                // Offsets count from the 'movi' fourcc, and land on the chunk.
                if (tag(s, movi + off) != "00dc" || le32(s, movi + off + 4) != n) return p;
                p.indexSizes.push_back(n);
            }
        }
        at += 8 + size + (size & 1u);
    }
    p.ok = movi != 0 && index;
    return p;
}
} // namespace

TEST_CASE("A Motion JPEG AVI holds every frame where a reader looks", "[avi]")
{
    std::stringstream file;
    MjpegAviWriter w;
    REQUIRE(w.begin(file, 1280, 720, 30));
    const auto a = fakeJpeg(1001, 0x11); // odd, so it is padded
    const auto b = fakeJpeg(64, 0x22);
    const auto c = fakeJpeg(3000, 0x33);
    REQUIRE(w.addFrame(a.data(), a.size()));
    REQUIRE(w.addFrame(b.data(), b.size()));
    REQUIRE(w.addFrame(c.data(), c.size()));
    CHECK(w.frames() == 3);
    REQUIRE(w.finish());
    CHECK_FALSE(w.open());

    const Parsed p = parse(file.str());
    REQUIRE(p.ok);
    CHECK(p.width == 1280);
    CHECK(p.height == 720);
    CHECK(p.usPerFrame == 33333);
    CHECK(p.rate == 30);
    CHECK(p.totalFrames == 3);
    CHECK(p.streamLength == 3);
    CHECK(p.handler == "MJPG");
    CHECK(p.compression == "MJPG");
    REQUIRE(p.frames.size() == 3);
    CHECK(p.frames[0] == std::string(a.begin(), a.end()));
    CHECK(p.frames[1] == std::string(b.begin(), b.end()));
    CHECK(p.frames[2] == std::string(c.begin(), c.end()));
    CHECK(p.indexSizes == std::vector<std::uint32_t>{1001, 64, 3000});
}

TEST_CASE("An AVI with no frames is still a well-formed file", "[avi]")
{
    std::stringstream file;
    MjpegAviWriter w;
    REQUIRE(w.begin(file, 2, 2, 1));
    REQUIRE(w.finish());
    const Parsed p = parse(file.str());
    CHECK(p.ok);
    CHECK(p.frames.empty());
    CHECK(p.totalFrames == 0);
}

TEST_CASE("The AVI writer refuses what it cannot write", "[avi]")
{
    std::stringstream file;
    MjpegAviWriter w;
    std::string why;
    CHECK_FALSE(w.begin(file, 0, 720, 30, &why));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(w.begin(file, 1280, kMaxVideoSide + 1, 30));
    CHECK_FALSE(w.begin(file, 1280, 720, 0));
    CHECK_FALSE(w.begin(file, 1280, 720, 61));

    const auto jpeg = fakeJpeg(100, 0x44);
    CHECK_FALSE(w.addFrame(jpeg.data(), jpeg.size())); // not begun
    CHECK_FALSE(w.finish());

    REQUIRE(w.begin(file, 640, 480, 24));
    CHECK_FALSE(w.begin(file, 640, 480, 24)); // already open
    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', 0, 0};
    CHECK_FALSE(w.addFrame(png.data(), png.size(), &why));
    CHECK(why == "The frame is not a JPEG image.");
    CHECK_FALSE(w.addFrame(nullptr, 0));
    CHECK(w.frames() == 0);

    // A frame that would take the file past the cap is refused before it is written.
    CHECK(w.canAdd(1000));
    CHECK_FALSE(w.canAdd(static_cast<std::size_t>(kMaxAviFileBytes)));
    REQUIRE(w.finish());
}

TEST_CASE("Long exports continue in numbered parts", "[avi]")
{
    CHECK(partFileName("tour_2026-11-18", 1) == "tour_2026-11-18.avi");
    CHECK(partFileName("tour_2026-11-18", 2) == "tour_2026-11-18-part2.avi");
    CHECK(partFileName("", 1) == "video.avi");
    CHECK(looksLikeJpeg(fakeJpeg(4, 0).data(), 4));
    const unsigned char cut[] = {0xff, 0xd8, 0x00, 0x00};
    CHECK_FALSE(looksLikeJpeg(cut, sizeof(cut)));
}
