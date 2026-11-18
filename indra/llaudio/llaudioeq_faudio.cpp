/**
 * @file llaudioeq_faudio.cpp
 * @brief 6|7 World Studio equalizer FAPO. See llaudioeq_faudio.h.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "llaudioeq_faudio.h"

#include "sw67/audio/equalizer.h"

#include <FAPO.h>
#include <FAPOBase.h>
#include <FAudio.h>

#include <cstring>
#include <new>

namespace
{
static_assert(sizeof(SW67EqParameters) == sizeof(float) * sw67::audio::kBands, "one gain per band");

const FAudioGUID kSW67EqClsid = {0x5f67e001, 0x6f37, 0x4e51, {0x9a, 0x67, 0x67, 0x77, 0x6f, 0x72, 0x6c, 0x64}};

FAPORegistrationProperties makeProperties()
{
    FAPORegistrationProperties p{};
    p.clsid = kSW67EqClsid;
    const char16_t name[] = u"SW67Equalizer";
    const char16_t copy[] = u"6|7 World Studio";
    static_assert(sizeof(name) / sizeof(name[0]) <= sizeof(p.FriendlyName) / sizeof(p.FriendlyName[0]), "name fits");
    static_assert(sizeof(copy) / sizeof(copy[0]) <= sizeof(p.CopyrightInfo) / sizeof(p.CopyrightInfo[0]), "copyright fits");
    for (size_t i = 0; i < sizeof(name) / sizeof(name[0]); ++i) p.FriendlyName[i] = static_cast<int16_t>(name[i]);
    for (size_t i = 0; i < sizeof(copy) / sizeof(copy[0]); ++i) p.CopyrightInfo[i] = static_cast<int16_t>(copy[i]);
    p.MajorVersion = 1;
    p.MinorVersion = 0;
    p.Flags = FAPO_FLAG_CHANNELS_MUST_MATCH | FAPO_FLAG_FRAMERATE_MUST_MATCH | FAPO_FLAG_BITSPERSAMPLE_MUST_MATCH |
              FAPO_FLAG_BUFFERCOUNT_MUST_MATCH | FAPO_FLAG_INPLACE_SUPPORTED | FAPO_FLAG_INPLACE_REQUIRED;
    p.MinInputBufferCount = 1;
    p.MaxInputBufferCount = 1;
    p.MinOutputBufferCount = 1;
    p.MaxOutputBufferCount = 1;
    return p;
}

const FAPORegistrationProperties kProperties = makeProperties();

struct SW67EqFapo
{
    FAPOBase base; // First: FAudio treats a pointer to this as an FAPO*.
    SW67EqParameters blocks[3];
    sw67::audio::FilterBank bank;
    float sampleRate = 48000.f;
    int channels = 0;
    bool configured = false;
};

uint32_t FAPOCALL eqLockForProcess(SW67EqFapo* fapo, uint32_t inCount, const FAPOLockForProcessBufferParameters* in,
                          uint32_t outCount, const FAPOLockForProcessBufferParameters* out)
{
    const uint32_t hr = FAPOBase_LockForProcess(&fapo->base, inCount, in, outCount, out);
    if (hr != 0) return hr;
    if (!in || !in->pFormat || in->pFormat->nChannels > sw67::audio::FilterBank::kMaxChannels ||
        in->pFormat->wBitsPerSample != 32)
    {
        fapo->base.m_fIsLocked = 0;
        return FAUDIO_E_INVALID_ARG;
    }
    fapo->sampleRate = static_cast<float>(in->pFormat->nSamplesPerSec);
    fapo->channels = in->pFormat->nChannels;
    fapo->configured = false;
    fapo->bank.reset();
    return 0;
}

void FAPOCALL eqProcess(SW67EqFapo* fapo, uint32_t inCount, const FAPOProcessBufferParameters* in, uint32_t outCount,
               FAPOProcessBufferParameters* out, int32_t isEnabled)
{
    const bool changed = FAPOBase_ParametersChanged(&fapo->base) != 0;
    const auto* params = reinterpret_cast<const SW67EqParameters*>(FAPOBase_BeginProcess(&fapo->base));
    if (changed || !fapo->configured)
    {
        sw67::audio::Gains g{};
        for (int i = 0; i < sw67::audio::kBands; ++i) g[static_cast<size_t>(i)] = params->gainsDb[i];
        fapo->bank.configure(fapo->sampleRate, fapo->channels, g);
        fapo->configured = true;
    }
    if (isEnabled && inCount >= 1 && in && in->pBuffer && in->BufferFlags == FAPO_BUFFER_VALID)
    {
        fapo->bank.process(static_cast<float*>(in->pBuffer), in->ValidFrameCount);
    }
    else if (!isEnabled)
    {
        fapo->bank.reset(); // A bypassed filter starts clean when turned back on.
    }
    if (outCount >= 1 && out && in)
    {
        // In place: the output buffer is the input buffer.
        out->BufferFlags = in->BufferFlags;
        out->ValidFrameCount = in->ValidFrameCount;
    }
    FAPOBase_EndProcess(&fapo->base);
}

void FAPOCALL eqDestroy(void* p)
{
    delete static_cast<SW67EqFapo*>(p);
}
} // namespace

FAPO* sw67_create_eq_fapo()
{
    auto* fapo = new (std::nothrow) SW67EqFapo();
    if (!fapo) return nullptr;
    std::memset(fapo->blocks, 0, sizeof(fapo->blocks));
    CreateFAPOBase(&fapo->base, &kProperties, reinterpret_cast<uint8_t*>(fapo->blocks), sizeof(SW67EqParameters), 0);
    fapo->base.base.LockForProcess = reinterpret_cast<LockForProcessFunc>(eqLockForProcess);
    fapo->base.base.Process = reinterpret_cast<ProcessFunc>(eqProcess);
    fapo->base.Destructor = eqDestroy;
    return &fapo->base.base;
}
