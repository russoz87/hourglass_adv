#include "settings.h"

#include <Preferences.h>

#include "app_state.h"  // kIntervalCount
#include "log.h"
#include "palette.h"  // kPaletteCount

namespace {

Preferences gPrefs;
bool        gOpen = false;

// The namespace is opened once, on the first load, and stays open for the life
// of the app: NVS handles are a limited resource and there is only one store.
bool openPrefs()
{
    if (gOpen) {
        return true;
    }

    gOpen = gPrefs.begin(settings::kNamespace, /*readOnly=*/false);
    if (!gOpen) {
        hgLog("nvs: cannot open namespace '%s', using defaults", settings::kNamespace);
    }
    return gOpen;
}

}  // namespace

void settings::Store::load()
{
    if (!openPrefs()) {
        return;
    }

    const uint8_t interval = gPrefs.getUChar(kKeyIntervalIdx, kDefaultIntervalIdx);
    const uint8_t color    = gPrefs.getUChar(kKeyColorIdx, kDefaultColorIdx);
    const bool    sound    = gPrefs.getBool(kKeySoundOn, kDefaultSoundOn);

    // A value written by an older build (or by hand) must not index past the
    // interval or palette tables.
    intervalIdx = interval < kIntervalCount ? interval : kDefaultIntervalIdx;
    colorIdx    = color < kPaletteCount ? color : kDefaultColorIdx;
    soundOn     = sound;

    hgLog("nvs: interval_idx=%u color_idx=%u sound_on=%u",
          static_cast<unsigned>(intervalIdx), static_cast<unsigned>(colorIdx),
          static_cast<unsigned>(soundOn));
}

void settings::Store::setInterval(uint8_t idx)
{
    if (idx >= kIntervalCount) {
        idx = kDefaultIntervalIdx;
    }
    if (idx == intervalIdx) {
        return;
    }

    intervalIdx = idx;
    if (openPrefs() && gPrefs.putUChar(kKeyIntervalIdx, idx) == 0) {
        hgLog("nvs: write interval_idx failed");
    }
    hgLog("settings: interval=%u min (interval_idx=%u)",
          static_cast<unsigned>(kIntervalMinutes[idx]), static_cast<unsigned>(idx));
}

void settings::Store::setColor(uint8_t idx)
{
    if (idx >= kPaletteCount) {
        idx = kDefaultColorIdx;
    }
    if (idx == colorIdx) {
        return;
    }

    colorIdx = idx;
    if (openPrefs() && gPrefs.putUChar(kKeyColorIdx, idx) == 0) {
        hgLog("nvs: write color_idx failed");
    }
    hgLog("settings: color=%u (%s)", static_cast<unsigned>(idx), paletteName(idx));
}

void settings::Store::setSound(bool on)
{
    if (on == soundOn) {
        return;
    }

    soundOn = on;
    if (openPrefs() && gPrefs.putBool(kKeySoundOn, on) == 0) {
        hgLog("nvs: write sound_on failed");
    }
    hgLog("settings: sound=%s", on ? "on" : "off");
}
