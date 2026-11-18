/**
 * @file equalizer_test.cpp
 * @brief Sound Equalizer and Sound Check: presets, persistence, bypass,
 *        the filter bank's measured response, and clipping warnings.
 *
 * Copyright (c) 2026, 6|7 World Studio contributors.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-license.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 */

#include "sw67/audio/equalizer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace sw67::audio;

namespace
{
/// Plays a stereo sine through the bank and returns the output's gain in dB,
/// measured after the filters settle.
double measuredGainDb(const Gains& g, double hz, double fs = 48000.0)
{
    FilterBank bank;
    bank.configure(static_cast<float>(fs), 2, g);
    const std::size_t frames = 48000;
    std::vector<float> buf(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const float v = 0.25f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / fs));
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
    bank.process(buf.data(), frames);
    double in = 0, out = 0;
    for (std::size_t i = frames / 2; i < frames; ++i)
    {
        const double x = 0.25 * std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / fs);
        in += x * x;
        out += static_cast<double>(buf[2 * i]) * static_cast<double>(buf[2 * i]);
    }
    return 10.0 * std::log10(out / in);
}
} // namespace

TEST_CASE("Ten bands with readable labels and clamped gains", "[eq]")
{
    CHECK(bandLabel(0) == "31 Hz");
    CHECK(bandLabel(5) == "1 kHz");
    CHECK(bandLabel(9) == "16 kHz");
    CHECK(clampGain(30.f) == kMaxGainDb);
    CHECK(clampGain(-30.f) == kMinGainDb);
    CHECK(clampGain(3.3f) == 3.5f);
    CHECK(clampGain(std::nanf("")) == 0.f);
}

TEST_CASE("Built-in presets, custom presets and reset", "[eq]")
{
    const auto presets = builtinPresets();
    REQUIRE(presets.size() == 4);
    CHECK(presets[0].key == "flat");
    CHECK(presets[1].name == "Bass Boost");
    CHECK(presets[2].name == "Voice Clarity");
    CHECK(presets[3].name == "Night Listening");
    Settings s;
    REQUIRE(applyPreset(s, Source::World, "bass_boost"));
    CHECK(s.source(Source::World).gains[0] == 6.f);
    setBand(s, Source::World, 9, 3.f);
    CHECK(s.source(Source::World).preset == "custom");
    const auto key = saveCustomPreset(s, Source::World, "My Club");
    REQUIRE(key);
    CHECK(*key == "custom_my_club");
    CHECK(s.source(Source::World).preset == *key);
    REQUIRE(applyPreset(s, Source::Ui, *key));
    CHECK(s.source(Source::Ui).gains[9] == 3.f);
    CHECK(removeCustomPreset(s, *key));
    CHECK(s.source(Source::Ui).preset == "custom");
    CHECK_FALSE(applyPreset(s, Source::Ui, *key));
    s.source(Source::World).on = true;
    reset(s, Source::World);
    CHECK_FALSE(s.source(Source::World).on);
    CHECK(s.source(Source::World).gains[0] == 0.f);
}

TEST_CASE("Bypass: off or master-off sends a flat curve", "[eq]")
{
    Settings s;
    applyPreset(s, Source::World, "bass_boost");
    s.source(Source::World).on = true;
    CHECK(effectiveGains(s, Source::World)[0] == 0.f); // master switch off
    s.enabled = true;
    CHECK(effectiveGains(s, Source::World)[0] == 6.f);
    s.source(Source::World).on = false;
    CHECK(effectiveGains(s, Source::World)[0] == 0.f);
    // Independent sources: World's curve never reaches Interface sounds.
    s.source(Source::World).on = true;
    CHECK(effectiveGains(s, Source::Ui)[0] == 0.f);
}

TEST_CASE("The filter bank does what its curve says", "[eq]")
{
    Gains flat{};
    FilterBank bank;
    bank.configure(48000.f, 2, flat);
    CHECK(bank.flat());
    std::vector<float> buf = {0.1f, -0.2f, 0.3f, -0.4f};
    bank.process(buf.data(), 2);
    CHECK(buf[1] == -0.2f); // flat is bit-exact bypass

    Gains one{};
    one[5] = 6.f; // +6 dB at 1 kHz
    CHECK(measuredGainDb(one, 1000.0) == Catch::Approx(6.0).margin(0.3));
    CHECK(measuredGainDb(one, 1000.0) == Catch::Approx(responseDb(one, 48000.0, 1000.0)).margin(0.3));
    CHECK(std::fabs(measuredGainDb(one, 60.0)) < 0.5);
    Gains cut{};
    cut[0] = -12.f;
    cut[1] = -12.f;
    CHECK(measuredGainDb(cut, 45.0) < -10.0);
    CHECK(std::fabs(measuredGainDb(cut, 4000.0)) < 0.5);
    // Bands past the sample rate's reach are left out, not run unstable.
    Gains high{};
    high[9] = 12.f;
    CHECK(std::isfinite(measuredGainDb(high, 1000.0, 22050.0)));
}

TEST_CASE("Sound Check's three bands and input gain", "[eq]")
{
    SoundCheck sc;
    sc.lowDb = 4.f;
    sc.midDb = -2.f;
    sc.highDb = 3.f;
    sc.inputDb = 1.f;
    CHECK(soundCheckCurve(sc)[0] == 0.f); // off
    sc.on = true;
    const Gains g = soundCheckCurve(sc);
    CHECK(g[0] == 5.f);
    CHECK(g[3] == 5.f);
    CHECK(g[5] == -1.f);
    CHECK(g[9] == 4.f);
    Settings s;
    s.soundCheck = sc;
    CHECK(effectiveGains(s, Source::World)[9] == 4.f); // applies even with the listener EQ off
    CHECK_FALSE(s.soundCheck.applyToBroadcast);
}

TEST_CASE("Meters describe levels and clipping in words", "[eq]")
{
    CHECK(clipState({0.f, 0.f}) == ClipState::Silent);
    CHECK(clipState({0.5f, 0.2f}) == ClipState::Ok);
    CHECK(clipState({0.95f, 0.5f}) == ClipState::Hot);
    CHECK(clipState({1.0f, 0.7f}) == ClipState::Clipping);
    CHECK(describeLevel({1.2f, 0.7f}).find("CLIPPING") != std::string::npos);
    CHECK(describeLevel({0.5f, 0.25f}) == "-6.0 dB peak, -12.0 dB RMS: OK");
}

TEST_CASE("Engine limitations are stated, not hidden", "[eq]")
{
    EngineSupport e;
    e.world = e.ui = true;
    e.engineName = "FAudio";
    CHECK(limitation(e, Source::World).empty());
    CHECK(limitation(e, Source::Music).find("media plugin") != std::string::npos);
    CHECK(limitation(e, Source::Voice).find("WebRTC") != std::string::npos);
    EngineSupport none;
    CHECK(limitation(none, Source::World).find("does not let") != std::string::npos);
}

TEST_CASE("Equalizer settings persist; broadcast stays off", "[eq]")
{
    Settings s;
    s.enabled = true;
    applyPreset(s, Source::World, "voice_clarity");
    s.source(Source::World).on = true;
    saveCustomPreset(s, Source::World, "Late");
    s.soundCheck.on = true;
    s.soundCheck.lowDb = 2.f;
    s.soundCheck.applyToBroadcast = true;
    const ReadResult r = readSettings(writeSettings(s));
    REQUIRE(r.settings);
    CHECK(r.settings->enabled);
    CHECK(r.settings->source(Source::World).on);
    CHECK(r.settings->source(Source::World).gains[6] == 4.f);
    CHECK(r.settings->custom.size() == 1);
    CHECK(r.settings->soundCheck.lowDb == 2.f);
    CHECK_FALSE(r.settings->soundCheck.applyToBroadcast);
    CHECK_FALSE(readSettings("{}").settings);
}
