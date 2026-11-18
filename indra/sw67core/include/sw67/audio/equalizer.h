/**
 * @file equalizer.h
 * @brief The listener's 10-band Sound Equalizer and the DJ's Sound Check:
 *        settings, presets, the filter bank the audio engine runs, level
 *        meter readings and clipping warnings.
 *
 *        Which sources can be equalized depends on the audio engine. In this
 *        viewer's FAudio engine, world sounds and interface sounds pass
 *        through its mixer and can be; parcel music plays inside the media
 *        plugin and voice inside WebRTC, outside the mixer, so they can't,
 *        and the settings say so rather than pretend. Everything here
 *        changes only what this listener hears. The viewer does not
 *        broadcast a DJ's stream, so Sound Check is monitor only.
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

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sw67::audio
{

inline constexpr std::string_view kFormat = "sw67.equalizer";
inline constexpr int kFormatVersion = 1;
inline constexpr int kBands = 10;
inline constexpr std::array<float, kBands> kBandHz = {31.f, 62.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f};
inline constexpr float kMinGainDb = -12.f;
inline constexpr float kMaxGainDb = 12.f;
/// The filter bank's own limit, for band gain plus Sound Check's input gain.
inline constexpr float kMaxTotalGainDb = 24.f;
using Gains = std::array<float, kBands>;

/// "31 Hz", "1 kHz", "16 kHz".
std::string bandLabel(int band);
/// Clamps to kMinGainDb..kMaxGainDb and rounds to 0.5 dB.
float clampGain(float db);

enum class Source
{
    World,  ///< Object, ambient and collision sounds: equalized through the mixer.
    Ui,     ///< Interface sounds: equalized through the mixer.
    Music,  ///< Parcel music: plays in the media plugin, outside the mixer.
    Voice,  ///< Voice chat: plays in WebRTC, outside the mixer.
    Count
};
std::string_view sourceKey(Source s);
std::string_view sourceName(Source s);

/// What the running engine lets the equalizer reach.
struct EngineSupport
{
    bool world = false;
    bool ui = false;
    bool music = false;
    bool voice = false;
    bool meters = false;
    std::string engineName;
    bool supports(Source s) const;
};
/// Why a source can't be equalized here, in a sentence; empty when it can.
std::string limitation(const EngineSupport& e, Source s);

struct Preset
{
    std::string key;
    std::string name;
    Gains gains{};
    bool builtin = false;
};
/// Flat, Bass Boost, Voice Clarity, Night Listening.
std::vector<Preset> builtinPresets();

struct SourceEq
{
    bool on = false;
    std::string preset = "flat"; ///< The preset last chosen; "custom" once a band moves.
    Gains gains{};
};

/// Sound Check's monitor: 3 bands and input gain, on what this viewer mixes.
struct SoundCheck
{
    bool on = false;
    float lowDb = 0.f;   ///< Below about 250 Hz.
    float midDb = 0.f;   ///< About 500 Hz to 2 kHz.
    float highDb = 0.f;  ///< 4 kHz and up.
    float inputDb = 0.f; ///< Broadband gain before the meters.
    /// Off by default, and only offered when the viewer itself broadcasts.
    bool applyToBroadcast = false;
};

struct Settings
{
    bool enabled = false; ///< The equalizer's master switch.
    std::array<SourceEq, static_cast<std::size_t>(Source::Count)> sources{};
    std::vector<Preset> custom;
    SoundCheck soundCheck;

    SourceEq& source(Source s) { return sources[static_cast<std::size_t>(s)]; }
    const SourceEq& source(Source s) const { return sources[static_cast<std::size_t>(s)]; }
};

/// Every preset: built-in, then custom.
std::vector<Preset> allPresets(const Settings& s);
std::optional<Preset> findPreset(const Settings& s, const std::string& key);
bool applyPreset(Settings& s, Source src, const std::string& key);
/// Sets one band; the source's preset becomes "custom".
void setBand(Settings& s, Source src, int band, float db);
/// Saves the source's current bands as a custom preset (replacing one of the same name).
std::optional<std::string> saveCustomPreset(Settings& s, Source src, const std::string& name);
bool removeCustomPreset(Settings& s, const std::string& key);
/// Flat and off for one source.
void reset(Settings& s, Source src);

/// The gains the engine should run for a source: zero when off or bypassed,
/// plus Sound Check's curve when it is on. Clamped to kMaxTotalGainDb.
Gains effectiveGains(const Settings& s, Source src);
/// Sound Check's 3 bands and input gain as a 10-band curve.
Gains soundCheckCurve(const SoundCheck& sc);

/// The filter bank: one peaking filter per band, per channel, in series.
class FilterBank
{
public:
    static constexpr int kMaxChannels = 8;
    void configure(float sampleRate, int channels, const Gains& gainsDb);
    /// Interleaved float samples, in place.
    void process(float* samples, std::size_t frames);
    void reset();
    bool flat() const { return mFlat; }
    int channels() const { return mChannels; }

private:
    struct Coeffs
    {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    };
    std::array<Coeffs, kBands> mCoeffs{};
    std::array<std::array<std::array<float, 2>, kBands>, kMaxChannels> mState{}; ///< Direct form II transposed.
    int mChannels = 0;
    bool mFlat = true;
};

/// The bank's response at `hz`, in dB, for the meters' and tests' use.
double responseDb(const Gains& gainsDb, double sampleRate, double hz);

/// A meter reading turned into words.
struct Level
{
    float peak = 0.f; ///< Linear, 1.0 = full scale.
    float rms = 0.f;
};
enum class ClipState
{
    Silent,
    Ok,
    Hot,      ///< Within 1 dB of full scale.
    Clipping, ///< At or over full scale.
};
ClipState clipState(const Level& l);
std::string describeLevel(const Level& l); ///< "-12.0 dB peak, -20.5 dB RMS: OK"
float toDb(float linear);

std::string writeSettings(const Settings& s);
struct ReadResult
{
    std::optional<Settings> settings;
    std::vector<std::string> notes;
};
ReadResult readSettings(std::string_view text);

} // namespace sw67::audio
