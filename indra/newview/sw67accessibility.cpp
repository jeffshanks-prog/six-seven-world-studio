/**
 * @file sw67accessibility.cpp
 * @brief The viewer side of 6|7 World Studio's accessibility framework.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "llviewerprecompiledheaders.h"

#include "sw67accessibility.h"

#include "llviewercontrol.h"
#include "v4coloru.h"

using namespace sw67::a11y;

namespace sw67viewer
{
AccessibilityProfile loadAccessibility()
{
    std::map<std::string, std::string> values;
    const LLSD saved = gSavedSettings.getLLSD("SW67Accessibility");
    if (saved.isMap())
    {
        for (auto it = saved.beginMap(); it != saved.endMap(); ++it)
        {
            values[it->first] = it->second.asString();
        }
    }
    return deserialize(values);
}

void saveAccessibility(const AccessibilityProfile& profile)
{
    LLSD out = LLSD::emptyMap();
    for (const auto& [key, value] : serialize(profile))
    {
        out[key] = value;
    }
    gSavedSettings.setLLSD("SW67Accessibility", out);

    // The scene filter drives the renderer's existing CVD compensation pass
    // (the final blit's daltonisation, Machado et al. 2009, as sw67core
    // simulates). Simulation-only previews are not wired to the renderer yet.
    const SceneColorFilter& f = profile.sceneFilter;
    S32 mode = 0;
    if (f.mode == SceneColorFilter::Mode::Compensate)
    {
        switch (f.vision)
        {
        case VisionType::Protanopia: mode = 1; break;
        case VisionType::Deuteranopia: mode = 2; break;
        case VisionType::Tritanopia: mode = 3; break;
        case VisionType::Typical: mode = 0; break;
        }
    }
    gSavedSettings.setS32("RenderCVDMode", mode);
    gSavedSettings.setF32("RenderCVDAmount", mode ? (F32)f.strength : 0.f);
    gSavedSettings.setBOOL("SW67SceneFilterInSnapshots", f.includeInSnapshots);
}

LLColor4 toColor(const Rgb8& colour)
{
    return LLColor4(LLColor4U(colour.r, colour.g, colour.b, 255));
}

LLColor4 statusColor(StatusRole role)
{
    return toColor(resolvePalette(loadAccessibility()).statusColour(role));
}

std::string statusText(StatusRole role)
{
    const StatusIndicator s = indicator(resolvePalette(loadAccessibility()), role);
    return std::string(shapeGlyph(s.shape)) + " " + std::string(s.label);
}
} // namespace sw67viewer
