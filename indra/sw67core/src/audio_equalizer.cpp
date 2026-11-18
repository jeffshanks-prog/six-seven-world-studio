/**
 * @file audio_equalizer.cpp
 * @brief Sound Equalizer and Sound Check. See equalizer.h.
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

#include "sw67/io/json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>

namespace sw67::audio
{
namespace
{
using io::Json;
constexpr double kPi = 3.14159265358979323846;
constexpr double kQ = 1.41; ///< About one octave.

std::string presetKeyFor(std::string_view name)
{
    std::string out = "custom_";
    for (char c : name)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
        else if (out.back() != '_') out += '_';
        if (out.size() >= 48) break;
    }
    while (out.back() == '_') out.pop_back();
    return out;
}

Json gainsJson(const Gains& g)
{
    Json a = Json::array();
    for (float v : g) a.push(Json(static_cast<double>(v)));
    return a;
}

Gains gainsFrom(const Json& j)
{
    Gains g{};
    const auto& items = j.items();
    for (std::size_t i = 0; i < g.size() && i < items.size(); ++i) g[i] = clampGain(static_cast<float>(items[i].asNumber(0)));
    return g;
}

float clampTo(float v, float lo, float hi)
{
    if (!std::isfinite(v)) return 0.f;
    return std::clamp(v, lo, hi);
}

/// RBJ peaking filter.
void peaking(double fs, double f0, double db, double& b0, double& b1, double& b2, double& a1, double& a2)
{
    const double A = std::pow(10.0, db / 40.0);
    const double w0 = 2.0 * kPi * f0 / fs;
    const double alpha = std::sin(w0) / (2.0 * kQ);
    const double c = std::cos(w0);
    const double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;
    b1 = (-2.0 * c) / a0;
    b2 = (1.0 - alpha * A) / a0;
    a1 = (-2.0 * c) / a0;
    a2 = (1.0 - alpha / A) / a0;
}

bool bandUsable(double fs, int band) { return kBandHz[static_cast<std::size_t>(band)] < 0.45 * fs; }
} // namespace

std::string bandLabel(int band)
{
    const float hz = kBandHz[static_cast<std::size_t>(std::clamp(band, 0, kBands - 1))];
    char buf[48];
    if (hz >= 1000.f) std::snprintf(buf, sizeof(buf), "%g kHz", static_cast<double>(hz / 1000.f));
    else std::snprintf(buf, sizeof(buf), "%g Hz", static_cast<double>(hz));
    return buf;
}

float clampGain(float db) { return std::round(clampTo(db, kMinGainDb, kMaxGainDb) * 2.f) / 2.f; }

std::string_view sourceKey(Source s)
{
    switch (s)
    {
        case Source::World: return "world";
        case Source::Ui: return "ui";
        case Source::Music: return "music";
        case Source::Voice: return "voice";
        default: return "world";
    }
}

std::string_view sourceName(Source s)
{
    switch (s)
    {
        case Source::World: return "World sounds";
        case Source::Ui: return "Interface sounds";
        case Source::Music: return "Parcel music";
        case Source::Voice: return "Voice chat";
        default: return "";
    }
}

bool EngineSupport::supports(Source s) const
{
    switch (s)
    {
        case Source::World: return world;
        case Source::Ui: return ui;
        case Source::Music: return music;
        case Source::Voice: return voice;
        default: return false;
    }
}

std::string limitation(const EngineSupport& e, Source s)
{
    if (e.supports(s)) return {};
    switch (s)
    {
        case Source::Music:
            return "Parcel music plays inside the media plugin, outside this viewer's sound mixer, so it can't be "
                   "equalized here. Use the music volume, or an equalizer in your computer's sound settings.";
        case Source::Voice:
            return "Voice chat plays through WebRTC, outside this viewer's sound mixer, so it can't be equalized here. "
                   "Use the voice volume, or your computer's sound settings.";
        default:
            return "This sound engine (" + (e.engineName.empty() ? std::string("unknown") : e.engineName) +
                   ") does not let 6|7 equalize " + std::string(sourceName(s)) + ".";
    }
}

std::vector<Preset> builtinPresets()
{
    return {
        {"flat", "Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, true},
        {"bass_boost", "Bass Boost", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}, true},
        {"voice_clarity", "Voice Clarity", {-4, -3, -2, -1, 0, 2, 4, 4, 2, 0}, true},
        {"night_listening", "Night Listening", {-6, -4, -2, 0, 0, 1, 1, 0, -2, -4}, true},
    };
}

std::vector<Preset> allPresets(const Settings& s)
{
    std::vector<Preset> out = builtinPresets();
    out.insert(out.end(), s.custom.begin(), s.custom.end());
    return out;
}

std::optional<Preset> findPreset(const Settings& s, const std::string& key)
{
    for (const Preset& p : allPresets(s))
    {
        if (p.key == key) return p;
    }
    return std::nullopt;
}

bool applyPreset(Settings& s, Source src, const std::string& key)
{
    const auto p = findPreset(s, key);
    if (!p) return false;
    s.source(src).gains = p->gains;
    s.source(src).preset = p->key;
    return true;
}

void setBand(Settings& s, Source src, int band, float db)
{
    if (band < 0 || band >= kBands) return;
    s.source(src).gains[static_cast<std::size_t>(band)] = clampGain(db);
    s.source(src).preset = "custom";
}

std::optional<std::string> saveCustomPreset(Settings& s, Source src, const std::string& name)
{
    if (name.find_first_not_of(" \t") == std::string::npos) return std::nullopt;
    const std::string key = presetKeyFor(name);
    if (key == "custom") return std::nullopt;
    Preset p{key, name.substr(0, 40), s.source(src).gains, false};
    auto it = std::find_if(s.custom.begin(), s.custom.end(), [&](const Preset& c) { return c.key == key; });
    if (it != s.custom.end()) *it = p;
    else if (s.custom.size() < 50) s.custom.push_back(p);
    else return std::nullopt;
    s.source(src).preset = key;
    return key;
}

bool removeCustomPreset(Settings& s, const std::string& key)
{
    const auto before = s.custom.size();
    s.custom.erase(std::remove_if(s.custom.begin(), s.custom.end(), [&](const Preset& p) { return p.key == key; }), s.custom.end());
    if (s.custom.size() == before) return false;
    for (SourceEq& e : s.sources)
    {
        if (e.preset == key) e.preset = "custom";
    }
    return true;
}

void reset(Settings& s, Source src)
{
    s.source(src) = SourceEq{};
}

Gains soundCheckCurve(const SoundCheck& sc)
{
    Gains g{};
    if (!sc.on) return g;
    for (int b = 0; b < kBands; ++b)
    {
        const float band = b <= 3 ? sc.lowDb : b <= 6 ? sc.midDb : sc.highDb;
        g[static_cast<std::size_t>(b)] = clampGain(band) + clampGain(sc.inputDb);
    }
    return g;
}

Gains effectiveGains(const Settings& s, Source src)
{
    Gains g{};
    const SourceEq& e = s.source(src);
    if (s.enabled && e.on) g = e.gains;
    const Gains sc = soundCheckCurve(s.soundCheck);
    for (std::size_t i = 0; i < g.size(); ++i) g[i] = clampTo(g[i] + sc[i], -kMaxTotalGainDb, kMaxTotalGainDb);
    return g;
}

void FilterBank::configure(float sampleRate, int channels, const Gains& gainsDb)
{
    mChannels = std::clamp(channels, 0, kMaxChannels);
    mFlat = true;
    const double fs = sampleRate > 1000.f ? sampleRate : 48000.0;
    for (int b = 0; b < kBands; ++b)
    {
        Coeffs& c = mCoeffs[static_cast<std::size_t>(b)];
        const float db = clampTo(gainsDb[static_cast<std::size_t>(b)], -kMaxTotalGainDb, kMaxTotalGainDb);
        if (std::fabs(db) < 0.01f || !bandUsable(fs, b))
        {
            c = Coeffs{};
            continue;
        }
        mFlat = false;
        double b0, b1, b2, a1, a2;
        peaking(fs, kBandHz[static_cast<std::size_t>(b)], db, b0, b1, b2, a1, a2);
        c = {static_cast<float>(b0), static_cast<float>(b1), static_cast<float>(b2), static_cast<float>(a1), static_cast<float>(a2)};
    }
}

void FilterBank::process(float* samples, std::size_t frames)
{
    if (mFlat || !samples || mChannels <= 0) return;
    const std::size_t ch = static_cast<std::size_t>(mChannels);
    for (std::size_t b = 0; b < static_cast<std::size_t>(kBands); ++b)
    {
        const Coeffs c = mCoeffs[b];
        if (c.b0 == 1.f && c.b1 == 0.f && c.b2 == 0.f && c.a1 == 0.f && c.a2 == 0.f) continue;
        for (std::size_t k = 0; k < ch; ++k)
        {
            float z1 = mState[k][b][0];
            float z2 = mState[k][b][1];
            float* p = samples + k;
            for (std::size_t f = 0; f < frames; ++f, p += ch)
            {
                const float x = *p;
                const float y = c.b0 * x + z1;
                z1 = c.b1 * x - c.a1 * y + z2;
                z2 = c.b2 * x - c.a2 * y;
                *p = y;
            }
            // Denormals and a filter knocked unstable by bad input would
            // otherwise ring for ever: start that band again.
            if (!std::isfinite(z1) || !std::isfinite(z2)) z1 = z2 = 0.f;
            if (std::fabs(z1) < 1e-20f) z1 = 0.f;
            if (std::fabs(z2) < 1e-20f) z2 = 0.f;
            mState[k][b][0] = z1;
            mState[k][b][1] = z2;
        }
    }
}

void FilterBank::reset()
{
    for (auto& ch : mState)
    {
        for (auto& band : ch) band = {0.f, 0.f};
    }
}

double responseDb(const Gains& gainsDb, double sampleRate, double hz)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = std::polar(1.0, -2.0 * w);
    double db = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        const double g = gainsDb[static_cast<std::size_t>(b)];
        if (std::fabs(g) < 0.01 || !bandUsable(sampleRate, b)) continue;
        double b0, b1, b2, a1, a2;
        peaking(sampleRate, kBandHz[static_cast<std::size_t>(b)], g, b0, b1, b2, a1, a2);
        const std::complex<double> h = (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
        db += 20.0 * std::log10(std::abs(h));
    }
    return db;
}

float toDb(float linear)
{
    return linear <= 1e-6f ? -120.f : 20.f * std::log10(linear);
}

ClipState clipState(const Level& l)
{
    if (l.peak <= 1e-4f) return ClipState::Silent;
    if (l.peak >= 1.f) return ClipState::Clipping;
    if (l.peak >= 0.891f) return ClipState::Hot; // -1 dBFS
    return ClipState::Ok;
}

std::string describeLevel(const Level& l)
{
    const ClipState s = clipState(l);
    if (s == ClipState::Silent) return "Silent";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.1f dB peak, %.1f dB RMS: %s", static_cast<double>(toDb(l.peak)),
                  static_cast<double>(toDb(l.rms)),
                  s == ClipState::Clipping ? "CLIPPING" : s == ClipState::Hot ? "HOT, near clipping" : "OK");
    return buf;
}

std::string writeSettings(const Settings& s)
{
    Json root = Json::object();
    root.set("format", Json(kFormat));
    root.set("version", Json(kFormatVersion));
    root.set("enabled", Json(s.enabled));
    Json sources = Json::object();
    for (int i = 0; i < static_cast<int>(Source::Count); ++i)
    {
        const SourceEq& e = s.sources[static_cast<std::size_t>(i)];
        Json j = Json::object();
        j.set("on", Json(e.on));
        j.set("preset", Json(e.preset));
        j.set("gains", gainsJson(e.gains));
        sources.set(std::string(sourceKey(static_cast<Source>(i))), std::move(j));
    }
    root.set("sources", std::move(sources));
    Json custom = Json::array();
    for (const Preset& p : s.custom)
    {
        Json j = Json::object();
        j.set("key", Json(p.key));
        j.set("name", Json(p.name));
        j.set("gains", gainsJson(p.gains));
        custom.push(std::move(j));
    }
    root.set("custom", std::move(custom));
    Json sc = Json::object();
    sc.set("on", Json(s.soundCheck.on));
    sc.set("low", Json(static_cast<double>(s.soundCheck.lowDb)));
    sc.set("mid", Json(static_cast<double>(s.soundCheck.midDb)));
    sc.set("high", Json(static_cast<double>(s.soundCheck.highDb)));
    sc.set("input", Json(static_cast<double>(s.soundCheck.inputDb)));
    // applyToBroadcast is never saved: it starts off every session.
    root.set("sound_check", std::move(sc));
    return io::writeJson(root);
}

ReadResult readSettings(std::string_view text)
{
    ReadResult r;
    const auto root = io::parseJson(text);
    if (!root || !root->isObject() || (*root)["format"].asString("") != kFormat)
    {
        r.notes.push_back("This is not a 6|7 equalizer file.");
        return r;
    }
    const int version = (*root)["version"].asInt(0);
    if (version < 1 || version > kFormatVersion)
    {
        r.notes.push_back("These equalizer settings were saved by a newer 6|7 World Studio.");
        return r;
    }
    Settings s;
    s.enabled = (*root)["enabled"].asBool(false);
    for (const Json& p : (*root)["custom"].items())
    {
        if (s.custom.size() >= 50) break;
        const std::string key = p["key"].asString("");
        if (key.rfind("custom_", 0) != 0 || key.size() > 48) continue;
        s.custom.push_back({key, p["name"].asString(key).substr(0, 40), gainsFrom(p["gains"]), false});
    }
    for (int i = 0; i < static_cast<int>(Source::Count); ++i)
    {
        const Json& j = (*root)["sources"][sourceKey(static_cast<Source>(i))];
        SourceEq& e = s.sources[static_cast<std::size_t>(i)];
        e.on = j["on"].asBool(false);
        e.gains = gainsFrom(j["gains"]);
        e.preset = j["preset"].asString("flat").substr(0, 48);
    }
    const Json& sc = (*root)["sound_check"];
    s.soundCheck.on = sc["on"].asBool(false);
    s.soundCheck.lowDb = clampGain(static_cast<float>(sc["low"].asNumber(0)));
    s.soundCheck.midDb = clampGain(static_cast<float>(sc["mid"].asNumber(0)));
    s.soundCheck.highDb = clampGain(static_cast<float>(sc["high"].asNumber(0)));
    s.soundCheck.inputDb = clampGain(static_cast<float>(sc["input"].asNumber(0)));
    r.settings = std::move(s);
    return r;
}

} // namespace sw67::audio
