/**
 * @file color_test.cpp
 * @brief Tests for the accessibility colour maths, against published values.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors. LGPL-2.1.
 */

#include "sw67/a11y/color.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace sw67::a11y;
using Catch::Approx;

TEST_CASE("parseHex accepts #RRGGBB and RRGGBB and rejects the rest", "[color]")
{
    CHECK(parseHex("#FF8000") == Rgb8{255, 128, 0});
    CHECK(parseHex("ff8000") == Rgb8{255, 128, 0});
    CHECK_FALSE(parseHex("#FF80").has_value());
    CHECK_FALSE(parseHex("#GG0000").has_value());
    CHECK_FALSE(parseHex("").has_value());
    CHECK(toHex({255, 128, 0}) == "#FF8000");
}

TEST_CASE("WCAG contrast matches the specification's anchor values", "[color]")
{
    const Rgb8 black{0, 0, 0}, white{255, 255, 255};
    CHECK(contrastRatio(black, white) == Approx(21.0).margin(1e-6));
    CHECK(contrastRatio(white, black) == Approx(21.0).margin(1e-6));
    CHECK(contrastRatio(white, white) == Approx(1.0));
    // #767676 on white is the well-known 4.54:1 minimum grey.
    CHECK(contrastRatio(*parseHex("#767676"), white) == Approx(4.54).margin(0.01));
}

TEST_CASE("CVD simulation keeps greys grey and is identity for typical vision", "[color]")
{
    const Rgb8 grey{128, 128, 128};
    for (VisionType v : {VisionType::Protanopia, VisionType::Deuteranopia, VisionType::Tritanopia})
    {
        const Rgb8 s = simulate(grey, v);
        CHECK(std::abs(int(s.r) - 128) <= 1);
        CHECK(std::abs(int(s.g) - 128) <= 1);
        CHECK(std::abs(int(s.b) - 128) <= 1);
    }
    CHECK(simulate({200, 10, 50}, VisionType::Typical) == Rgb8{200, 10, 50});
}

TEST_CASE("Red and green collapse for red-green dichromats but not for tritanopes", "[color]")
{
    const Rgb8 red = *parseHex("#BF4040");
    const Rgb8 green = *parseHex("#40804A");
    const double typical = perceptualDistance(red, green);
    const double protan = perceptualDistance(simulate(red, VisionType::Protanopia), simulate(green, VisionType::Protanopia));
    const double deutan = perceptualDistance(simulate(red, VisionType::Deuteranopia), simulate(green, VisionType::Deuteranopia));
    const double tritan = perceptualDistance(simulate(red, VisionType::Tritanopia), simulate(green, VisionType::Tritanopia));
    CHECK(protan < typical * 0.5);
    CHECK(deutan < typical * 0.5);
    CHECK(tritan > typical * 0.5);
}

TEST_CASE("OKLab of white is L=1, a=b=0", "[color]")
{
    const OkLab w = toOkLab({255, 255, 255});
    CHECK(w.L == Approx(1.0).margin(1e-3));
    CHECK(w.a == Approx(0.0).margin(1e-3));
    CHECK(w.b == Approx(0.0).margin(1e-3));
}

TEST_CASE("Vision type keys round trip", "[color]")
{
    for (VisionType v : {VisionType::Typical, VisionType::Protanopia, VisionType::Deuteranopia, VisionType::Tritanopia})
    {
        CHECK(parseVisionType(visionTypeKey(v)) == v);
    }
    CHECK_FALSE(parseVisionType("colourblind").has_value());
}
