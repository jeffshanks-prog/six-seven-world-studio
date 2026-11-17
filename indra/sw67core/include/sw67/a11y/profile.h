/**
 * @file profile.h
 * @brief The accessibility profile every 6|7 World Studio module reads:
 *        interface palette, status indicators that never rely on colour
 *        alone, text scale, keyboard focus, and the scene colour filter,
 *        which is kept apart from the interface theme.
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

#include "sw67/a11y/color.h"

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::a11y
{

/// What a status means. Modules ask for a role, never for a raw colour.
enum class StatusRole
{
    Success,
    Warning,
    Error,
    Info,
    Neutral,
    Restricted, ///< An active RLV restriction or a blocked permission.
    Live,       ///< Recording, streaming or broadcasting is on.
    Count
};

constexpr std::size_t kStatusRoleCount = static_cast<std::size_t>(StatusRole::Count);

/// A shape drawn with every status, so the status reads without colour.
/// Each has a glyph in the viewer's bundled fonts (shapeGlyph).
enum class StatusShape
{
    Circle,         ///< U+25CF
    Triangle,       ///< U+25B2
    Cross,          ///< U+2716
    Square,         ///< U+25A0
    Diamond,        ///< U+25C6
    DiamondOutline, ///< U+25C7
    Ring,           ///< U+25CB
};

/// The shape as UTF-8 text, for labels that draw it inline.
std::string_view shapeGlyph(StatusShape shape);

/// Everything a module needs to draw one status: colour, an icon name from
/// the skin, a distinct shape, and a text label. All four are always set.
struct StatusIndicator
{
    StatusRole role = StatusRole::Neutral;
    Rgb8 colour;
    std::string_view icon;  ///< Skin texture name, e.g. "SW67_Status_Error".
    StatusShape shape = StatusShape::Circle;
    std::string_view label; ///< Short default label, e.g. "Error".
};

/// The interface palette. Colours here theme the UI only; they never touch
/// the 3D scene or exported images.
struct InterfacePalette
{
    Rgb8 background;
    Rgb8 surface;
    Rgb8 text;
    Rgb8 mutedText;
    Rgb8 focusRing;
    std::array<Rgb8, kStatusRoleCount> status{};

    Rgb8 statusColour(StatusRole role) const { return status[static_cast<std::size_t>(role)]; }
};

/// The built-in interface presets.
enum class PalettePreset
{
    Standard,
    Protanopia,
    Deuteranopia,
    Tritanopia,
    HighContrast,
    Custom, ///< Standard with the user's overrides on top.
};

/// An optional filter over the rendered 3D scene (simulation or
/// compensation). Deliberately a separate type from the interface palette:
/// choosing an interface preset never turns this on, and it is left out of
/// snapshots and recordings unless the user opts in.
struct SceneColorFilter
{
    enum class Mode
    {
        Off,
        Simulate,   ///< Preview how others with a CVD see the scene.
        Compensate, ///< Daltonise the scene for the viewer's own CVD.
    };
    Mode mode = Mode::Off;
    VisionType vision = VisionType::Typical;
    double strength = 1.0;             ///< 0..1.
    bool includeInSnapshots = false;   ///< Opt-in only.

    friend bool operator==(const SceneColorFilter&, const SceneColorFilter&) = default;
};

/// Limits on the text scale the UI accepts.
inline constexpr double kMinTextScale = 0.75;
inline constexpr double kMaxTextScale = 3.0;

struct AccessibilityProfile
{
    PalettePreset preset = PalettePreset::Standard;
    double textScale = 1.0;           ///< Clamped to [kMinTextScale, kMaxTextScale].
    bool keyboardFocusHighlight = true;
    int focusRingWidthPx = 2;         ///< Clamped to [1, 6].
    bool reduceMotion = false;
    /// Custom colour overrides, by key ("background", "text", "status.error"...).
    /// Only applied when preset is Custom.
    std::map<std::string, Rgb8> overrides;
    SceneColorFilter sceneFilter;

    friend bool operator==(const AccessibilityProfile&, const AccessibilityProfile&) = default;
};

/// The palette a built-in preset defines.
InterfacePalette presetPalette(PalettePreset preset);

/// The palette a profile resolves to, overrides applied for Custom.
InterfacePalette resolvePalette(const AccessibilityProfile& profile);

/// The indicator for a role in a palette, with its icon, shape and label.
StatusIndicator indicator(const InterfacePalette& palette, StatusRole role);

/// A font size scaled by the profile, rounded, never below 8 px.
int scaledFontPx(const AccessibilityProfile& profile, int basePx);

/// The colour-vision type a preset is designed for (Typical for the others).
VisionType presetVision(PalettePreset preset);

std::string_view presetKey(PalettePreset preset);
std::string_view statusRoleKey(StatusRole role);

/// A problem validatePalette found.
struct PaletteIssue
{
    std::string what;
};

/// Checks a palette against the framework's rules:
///  - text on background and surface: WCAG contrast >= 4.5 (7.0 when
///    strict, as the high-contrast preset is held to);
///  - each status colour on background: >= 3.0 (WCAG 1.4.11 non-text),
///    4.5 when strict;
///  - the focus ring on background: >= 3.0;
///  - Success, Warning, Error, Info and Restricted pairwise at least
///    minDistance apart in OKLab as seen with `vision`.
/// Shape, icon and label distinctness are checked too, so a status never
/// relies on colour alone.
std::vector<PaletteIssue> validatePalette(const InterfacePalette& palette,
                                          VisionType vision,
                                          bool strict = false,
                                          double minDistance = 0.08);

/// Flattens a profile to string keys and values, for the viewer's settings.
std::map<std::string, std::string> serialize(const AccessibilityProfile& profile);

/// Reads a profile back. Unknown keys are ignored; malformed or
/// out-of-range values fall back to their defaults or are clamped, so a
/// bad settings file never produces an unreadable UI.
AccessibilityProfile deserialize(const std::map<std::string, std::string>& values);

} // namespace sw67::a11y
