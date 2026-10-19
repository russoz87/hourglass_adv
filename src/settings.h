/*
 * Persisted settings (NVS namespace "hourglass").
 *
 * The Launcher keeps its own entries in other namespaces and the app never
 * touches them: four keys of our own are enough.
 *
 * Values are written the moment they change, so there is no save step in the
 * UI and a power cut cannot lose a choice that is already on screen.
 */
#pragma once

#include <cstdint>

namespace settings {

constexpr char kNamespace[]      = "hourglass";
constexpr char kKeyIntervalIdx[] = "interval_idx";
constexpr char kKeyColorIdx[]    = "color_idx";
constexpr char kKeySoundOn[]     = "sound_on";
constexpr char kKeyClockOn[]     = "clock_on";

// 10 minutes: long enough to look like a timer, short enough for a first test.
constexpr uint8_t kDefaultIntervalIdx = 3;
constexpr uint8_t kDefaultColorIdx    = 0;
constexpr bool    kDefaultSoundOn     = true;
constexpr bool    kDefaultClockOn     = true;  // hide it on purpose, not by accident

struct Store
{
    uint8_t intervalIdx = kDefaultIntervalIdx;
    uint8_t colorIdx    = kDefaultColorIdx;
    bool    soundOn     = kDefaultSoundOn;
    bool    clockOn     = kDefaultClockOn;

    // Reads the namespace, falling back to the defaults above for missing or
    // out-of-range values.
    void load();

    void setInterval(uint8_t idx);
    void setColor(uint8_t idx);
    void setSound(bool on);
    void setClock(bool on);
};

}  // namespace settings
