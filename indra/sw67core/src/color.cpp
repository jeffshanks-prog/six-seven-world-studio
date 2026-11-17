/**
 * @file color.cpp
 * @brief Colour maths for the accessibility framework.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/a11y/color.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sw67::a11y
{
namespace
{
int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

double channelToLinear(std::uint8_t v)
{
    const double c = v / 255.0;
    return (c <= 0.04045) ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

std::uint8_t channelFromLinear(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    const double c = (v <= 0.0031308) ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0));
}

using Matrix = std::array<std::array<double, 3>, 3>;

// Machado, Oliveira & Fernandes, "A Physiologically-based Model for
// Simulation of Color Vision Deficiency", IEEE TVCG 2009, severity 1.0.
// Each row sums to 1 so that greys stay grey.
constexpr Matrix kProtanopia{{
    {0.152286, 1.052583, -0.204868},
    {0.114503, 0.786281, 0.099216},
    {-0.003882, -0.048116, 1.051998},
}};
constexpr Matrix kDeuteranopia{{
    {0.367322, 0.860646, -0.227968},
    {0.280085, 0.672501, 0.047413},
    {-0.011820, 0.042940, 0.968881},
}};
constexpr Matrix kTritanopia{{
    {1.255528, -0.076749, -0.178779},
    {-0.078411, 0.930809, 0.147602},
    {0.004733, 0.691367, 0.303900},
}};

LinearRgb applyMatrix(const Matrix& m, const LinearRgb& c)
{
    return {m[0][0] * c.r + m[0][1] * c.g + m[0][2] * c.b,
            m[1][0] * c.r + m[1][1] * c.g + m[1][2] * c.b,
            m[2][0] * c.r + m[2][1] * c.g + m[2][2] * c.b};
}
} // namespace

std::optional<Rgb8> parseHex(std::string_view text)
{
    if (!text.empty() && text.front() == '#')
    {
        text.remove_prefix(1);
    }
    if (text.size() != 6)
    {
        return std::nullopt;
    }
    std::array<int, 6> d{};
    for (std::size_t i = 0; i < 6; ++i)
    {
        d[i] = hexDigit(text[i]);
        if (d[i] < 0)
        {
            return std::nullopt;
        }
    }
    return Rgb8{static_cast<std::uint8_t>(d[0] * 16 + d[1]),
                static_cast<std::uint8_t>(d[2] * 16 + d[3]),
                static_cast<std::uint8_t>(d[4] * 16 + d[5])};
}

std::string toHex(const Rgb8& colour)
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out = "#";
    for (std::uint8_t v : {colour.r, colour.g, colour.b})
    {
        out.push_back(kDigits[v >> 4]);
        out.push_back(kDigits[v & 0xF]);
    }
    return out;
}

LinearRgb toLinear(const Rgb8& colour)
{
    return {channelToLinear(colour.r), channelToLinear(colour.g), channelToLinear(colour.b)};
}

Rgb8 fromLinear(const LinearRgb& colour)
{
    return {channelFromLinear(colour.r), channelFromLinear(colour.g), channelFromLinear(colour.b)};
}

double relativeLuminance(const Rgb8& colour)
{
    const LinearRgb l = toLinear(colour);
    return 0.2126 * l.r + 0.7152 * l.g + 0.0722 * l.b;
}

double contrastRatio(const Rgb8& a, const Rgb8& b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

Rgb8 simulate(const Rgb8& colour, VisionType vision)
{
    switch (vision)
    {
    case VisionType::Typical:
        return colour;
    case VisionType::Protanopia:
        return fromLinear(applyMatrix(kProtanopia, toLinear(colour)));
    case VisionType::Deuteranopia:
        return fromLinear(applyMatrix(kDeuteranopia, toLinear(colour)));
    case VisionType::Tritanopia:
        return fromLinear(applyMatrix(kTritanopia, toLinear(colour)));
    }
    return colour;
}

OkLab toOkLab(const Rgb8& colour)
{
    const LinearRgb c = toLinear(colour);
    const double l = std::cbrt(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b);
    const double m = std::cbrt(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b);
    const double s = std::cbrt(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

double perceptualDistance(const Rgb8& a, const Rgb8& b)
{
    const OkLab x = toOkLab(a);
    const OkLab y = toOkLab(b);
    return std::sqrt((x.L - y.L) * (x.L - y.L) + (x.a - y.a) * (x.a - y.a) + (x.b - y.b) * (x.b - y.b));
}

std::string_view visionTypeName(VisionType vision)
{
    switch (vision)
    {
    case VisionType::Typical: return "Typical colour vision";
    case VisionType::Protanopia: return "Protanopia";
    case VisionType::Deuteranopia: return "Deuteranopia";
    case VisionType::Tritanopia: return "Tritanopia";
    }
    return "";
}

std::string_view visionTypeKey(VisionType vision)
{
    switch (vision)
    {
    case VisionType::Typical: return "typical";
    case VisionType::Protanopia: return "protanopia";
    case VisionType::Deuteranopia: return "deuteranopia";
    case VisionType::Tritanopia: return "tritanopia";
    }
    return "";
}

std::optional<VisionType> parseVisionType(std::string_view key)
{
    for (VisionType v : {VisionType::Typical, VisionType::Protanopia, VisionType::Deuteranopia, VisionType::Tritanopia})
    {
        if (visionTypeKey(v) == key)
        {
            return v;
        }
    }
    return std::nullopt;
}

} // namespace sw67::a11y
