/**
 * @file product.h
 * @brief The Product Photography Assistant's planning: reusable camera
 *        angles around a subject, lighting and background choices, a batch
 *        of shots, export sizes, watermark placement, file naming and the
 *        output folder layout.
 *
 *        Originals are always kept: every shot is saved once, untouched, in
 *        originals/, and every resized, cropped, watermarked or AI-altered
 *        file is derived from it into exports/ with the steps recorded in a
 *        batch manifest, so an edit can be redone from the original. Files a
 *        generative service altered carry "_ai" in their names and are
 *        marked in the manifest.
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

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::photo
{

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/// A camera angle around the subject, in the subject's frame: azimuth 0 is
/// in front of it (+X, the way a Second Life object faces), increasing
/// counter-clockwise seen from above; elevation is up from level.
struct CameraAngle
{
    std::string name;
    double azimuthDeg = 0.0;
    double elevationDeg = 10.0;
    double distanceFactor = 2.5; ///< Times the subject's bounding radius.
};

std::vector<CameraAngle> builtinAngles();

struct CameraPlacement
{
    Vec3 eye;
    Vec3 focus;
};

/// Where to put the camera for `angle` on a subject centred at `centre`
/// with bounding radius `radius`, facing `subjectYawDeg` (its rotation about
/// the vertical axis). The distance is never under 0.5 m.
CameraPlacement placeCamera(const CameraAngle& angle, const Vec3& centre, double radius, double subjectYawDeg);

struct ExportSize
{
    std::string key;   ///< For file names, e.g. "square1080".
    std::string label; ///< For people.
    int width = 0;
    int height = 0;
};

/// Common sizes. Check each destination's current requirements: these are
/// starting points, not any site's rules.
std::vector<ExportSize> builtinExportSizes();

struct PixelRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    friend bool operator==(const PixelRect&, const PixelRect&) = default;
};

/// The centred crop of a source image that fills `target` without
/// distortion ("cover").
PixelRect coverCrop(int sourceWidth, int sourceHeight, int targetWidth, int targetHeight);

enum class Corner
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Centre,
};

struct Watermark
{
    std::string text;          ///< Or empty with an image.
    std::string imagePath;     ///< A PNG of the user's own brand mark.
    Corner corner = Corner::BottomRight;
    double widthFraction = 0.2; ///< Of the image width, 0.02..0.8.
    double marginFraction = 0.03;
    double opacity = 0.6;       ///< 0.05..1.
};

/// Where a mark whose aspect ratio is `markAspect` (width / height) goes on
/// an image of the given size. The rect always lies inside the image.
PixelRect placeWatermark(const Watermark& mark, double markAspect, int imageWidth, int imageHeight);

/// Turns any text into a safe file-name stem: no path separators, reserved
/// characters or control characters, no Windows device names, no leading or
/// trailing dots or spaces, at most 80 bytes. Never empty.
std::string safeFileStem(std::string_view text);

/// Expands a naming pattern. Tokens: {project} {angle} {size} {index}
/// ({index:3} pads to 3 digits) {date} {background} {lighting}. Unknown
/// tokens are left out. The result is passed through safeFileStem.
std::string expandName(std::string_view pattern, const std::map<std::string, std::string>& values, int index);

/// `stem`, or `stem-2`, `stem-3`... whichever is not in `taken`.
std::string uniqueStem(const std::string& stem, const std::set<std::string>& taken);

/// One step of the reversible edit chain from an original to an export.
struct EditStep
{
    enum class Kind
    {
        CoverCrop,
        Resize,
        Watermark,
        AIEdit, ///< Done by a generative service; always labelled.
    };
    Kind kind = Kind::Resize;
    std::string detail;
};

struct PlannedFile
{
    std::string path; ///< Relative to the output folder, '/' separated.
    int width = 0;
    int height = 0;
    bool original = false;
    bool aiAltered = false;
    std::string derivedFrom; ///< The original's path, for exports.
    std::vector<EditStep> steps;
};

struct Shot
{
    int index = 1;
    CameraAngle angle;
    std::string background;
    std::string lighting;
    PlannedFile original;
    std::vector<PlannedFile> exports;
};

struct PhotoSession
{
    std::string project = "product";
    std::vector<CameraAngle> angles;
    std::vector<std::string> backgrounds{"default"}; ///< Names of backdrops the user set up.
    std::vector<std::string> lighting{"current"};    ///< Lightbox looks or environment presets, by name.
    std::vector<ExportSize> sizes;
    std::optional<Watermark> watermark;
    std::string namePattern = "{project}_{angle}_{index:3}";
    std::string date; ///< YYYY-MM-DD, for {date}.
    int captureWidth = 2048;
    int captureHeight = 2048;
    std::string originalFormat = "png"; ///< Lossless, always.
    std::string exportFormat = "jpg";
};

/// Plans every shot: each angle under each background and lighting choice,
/// its original, and an export per size. Names are unique within the batch
/// and do not collide with `existing` files in the output folder.
std::vector<Shot> planBatch(const PhotoSession& session, const std::set<std::string>& existing = {});

/// The label AI-altered files carry, and the derived file for an AI edit of
/// `original`: same size, "_ai" before the extension, marked and recorded.
inline constexpr std::string_view kAIMarker = "_ai";
PlannedFile planAIEdit(const PlannedFile& original, std::string operation);

/// The batch manifest (manifest.sw67.json in the output folder): every
/// file, whether it is an original, what it was made from and how.
std::string writeBatchManifest(const PhotoSession& session, const std::vector<Shot>& shots);

} // namespace sw67::photo
