/**
 * @file color.h
 * @brief Colour maths for 6|7 World Studio's accessibility framework:
 *        sRGB parsing, WCAG contrast, colour-vision-deficiency simulation
 *        and perceptual distance.
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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sw67::a11y
{

/// An 8-bit-per-channel sRGB colour, as the UI stores it.
struct Rgb8
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(const Rgb8&, const Rgb8&) = default;
};

/// Linear-light RGB, 0..1 per channel.
struct LinearRgb
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

/// OKLab coordinates (Ottosson 2020).
struct OkLab
{
    double L = 0.0;
    double a = 0.0;
    double b = 0.0;
};

/// The colour-vision types the framework simulates and designs presets for.
enum class VisionType
{
    Typical,
    Protanopia,   ///< No L cones (red-blind).
    Deuteranopia, ///< No M cones (green-blind).
    Tritanopia,   ///< No S cones (blue-blind).
};

/// Parses "#RRGGBB" or "RRGGBB" (case-insensitive). Returns nullopt on
/// anything else, so a malformed setting falls back to its default.
std::optional<Rgb8> parseHex(std::string_view text);

/// Formats as "#RRGGBB" in upper case.
std::string toHex(const Rgb8& colour);

LinearRgb toLinear(const Rgb8& colour);
Rgb8 fromLinear(const LinearRgb& colour);

/// WCAG 2.x relative luminance, 0 (black) .. 1 (white).
double relativeLuminance(const Rgb8& colour);

/// WCAG 2.x contrast ratio, 1 .. 21, symmetric in its arguments.
double contrastRatio(const Rgb8& a, const Rgb8& b);

/// Simulates how a colour appears with full-severity dichromacy, using the
/// Machado, Oliveira and Fernandes (2009) matrices in linear light.
/// VisionType::Typical returns the colour unchanged.
Rgb8 simulate(const Rgb8& colour, VisionType vision);

OkLab toOkLab(const Rgb8& colour);

/// Euclidean distance in OKLab. Around 0.02 is a just-noticeable
/// difference; the framework asks for far more between status colours.
double perceptualDistance(const Rgb8& a, const Rgb8& b);

/// Display name, e.g. "Protanopia".
std::string_view visionTypeName(VisionType vision);

/// Stable settings key, e.g. "protanopia". parseVisionType is its inverse.
std::string_view visionTypeKey(VisionType vision);
std::optional<VisionType> parseVisionType(std::string_view key);

} // namespace sw67::a11y
