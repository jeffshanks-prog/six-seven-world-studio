/**
 * @file sw67audioeq.cpp
 * @brief Sound Equalizer and Sound Check in the viewer. See sw67audioeq.h.
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

#include "sw67audioeq.h"

#include "sw67viewerfiles.h"

#include "llaudioengine.h"
#ifdef LL_FAUDIO
#include "llaudioengine_faudio.h"
#endif

#include <memory>

using namespace sw67::audio;

namespace sw67viewer::audio
{
namespace
{
std::unique_ptr<Settings> sSettings;

std::string filePath()
{
    return sw67viewer::joinPath(sw67viewer::userDir("audio"), "equalizer.sw67eq.json");
}

#ifdef LL_FAUDIO
LLAudioEngine_FAudio* engine()
{
    return dynamic_cast<LLAudioEngine_FAudio*>(gAudiop);
}

/// The engine's sound types each source covers.
std::vector<S32> typesFor(Source s)
{
    switch (s)
    {
        case Source::World:
            return {LLAudioEngine::AUDIO_TYPE_NONE, LLAudioEngine::AUDIO_TYPE_SFX, LLAudioEngine::AUDIO_TYPE_AMBIENT};
        case Source::Ui: return {LLAudioEngine::AUDIO_TYPE_UI};
        default: return {};
    }
}
#endif
} // namespace

Settings& settings()
{
    if (!sSettings)
    {
        sSettings = std::make_unique<Settings>();
        if (const auto text = sw67viewer::readFile(filePath(), 1024 * 1024))
        {
            ReadResult r = readSettings(*text);
            if (r.settings) *sSettings = std::move(*r.settings);
        }
    }
    return *sSettings;
}

bool saveAndApply()
{
    const bool ok = sw67viewer::writeFileAtomic(filePath(), writeSettings(settings()));
    apply();
    return ok;
}

void apply()
{
#ifdef LL_FAUDIO
    LLAudioEngine_FAudio* fa = engine();
    if (!fa) return;
    for (Source s : {Source::World, Source::Ui})
    {
        const Gains g = effectiveGains(settings(), s);
        float gains[LLAudioEngine_FAudio::SW67_EQ_BANDS];
        bool any = false;
        for (int b = 0; b < kBands; ++b)
        {
            gains[b] = g[static_cast<size_t>(b)];
            any = any || gains[b] != 0.f;
        }
        for (S32 type : typesFor(s)) fa->setGroupEq(type, any, gains);
    }
#endif
}

EngineSupport support()
{
    EngineSupport e;
#ifdef LL_FAUDIO
    if (LLAudioEngine_FAudio* fa = engine())
    {
        e.engineName = "FAudio";
        e.world = fa->hasGroupEq(LLAudioEngine::AUDIO_TYPE_SFX);
        e.ui = fa->hasGroupEq(LLAudioEngine::AUDIO_TYPE_UI);
        F32 peak = 0.f, rms = 0.f;
        e.meters = fa->getGroupLevel(LLAudioEngine::AUDIO_TYPE_SFX, peak, rms);
        return e;
    }
#endif
    e.engineName = gAudiop ? gAudiop->getDriverName(false) : std::string("none");
    return e;
}

bool level(Source s, Level& out)
{
    out = Level{};
#ifdef LL_FAUDIO
    LLAudioEngine_FAudio* fa = engine();
    if (!fa) return false;
    bool any = false;
    for (S32 type : typesFor(s))
    {
        F32 peak = 0.f, rms = 0.f;
        if (fa->getGroupLevel(type, peak, rms))
        {
            any = true;
            out.peak = llmax(out.peak, peak);
            out.rms = llmax(out.rms, rms);
        }
    }
    return any;
#else
    (void)s;
    return false;
#endif
}
} // namespace sw67viewer::audio
