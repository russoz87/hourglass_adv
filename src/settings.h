/*
 * Persisted settings (NVS namespace "hourglass").
 *
 * The Launcher keeps its own entries in other namespaces and the app never
 * touches them: three keys of our own are enough.
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

// 10 minutes: long enough to look like a timer, short enough for a first test.
constexpr uint8_t kDefaultIntervalIdx = 3;
constexpr uint8_t kDefaultColorIdx    = 0;
constexpr bool    kDefaultSoundOn     = true;

struct Store
{
    uint8_t intervalIdx = kDefaultIntervalIdx;
    uint8_t colorIdx    = kDefaultColorIdx;
    bool    soundOn     = kDefaultSoundOn;

    // Reads the namespace, falling back to the defaults above for missing or
    // out-of-range values.
    void load();

    void setInterval(uint8_t idx);
    void setColor(uint8_t idx);
    void setSound(bool on);
};

}  // namespace settings
