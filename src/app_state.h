/*
 * Timer state machine and the sand progress model.
 *
 * The model is a single number: `progress` in [0, 1], the share of the sand that
 * has run from the upper bulb into the lower bulb of the picture. Time is not
 * derived from the animation: `update()` integrates progress from millisecond
 * deltas, so the countdown stays right whatever the frame rate does.
 *
 *   Menu --start--> Armed --placed on a side--> Running
 *   Running <--back on a side-- Paused <--laid flat-- Running
 *   Running --progress reaches 1--> Done --flip / Enter--> Running (new cycle)
 *   Running --rewound to 0--> Finish --flip / Enter--> Running (new cycle)
 *   any state --Del (Esc)--> Menu
 *
 * The side the sand runs to is fixed when a cycle starts; standing on the other
 * side makes progress fall again (the sand and the clock run backwards), which
 * is what a flipped hourglass is supposed to look like. Once a cycle has begun
 * the app stays in the portrait hourglass world: both ends of the countdown end
 * in a finished hourglass - DONE after the interval, FINISH after the sand ran
 * back up - and both wait for a flip. ARMED is only the landscape hint before
 * the very first placement.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "orientation.h"

// Intervals offered in the menu, in minutes. Hotkeys 1..7 select these.
constexpr uint8_t  kIntervalCount           = 7;
constexpr uint32_t kIntervalMinutes[kIntervalCount] = {1, 2, 5, 10, 20, 25, 30};

enum class AppState : uint8_t
{
    Menu,
    Settings,
    Armed,   // waiting for the device to be put on its side
    Running, // sand falling, progress moving towards 1
    Paused,  // laid flat: sand and clock hold still
    Done,    // progress is 1, waiting for a flip or for Enter
    Finish,  // progress is 0 again after a rewind, waiting for a flip or Enter
};

const char* appStateName(AppState state);

namespace app {

struct Session
{
    AppState    state       = AppState::Menu;
    uint8_t     intervalIdx = 0;
    float       progress    = 0.0f;  // 0: all sand on top, 1: all sand at the bottom
    int8_t      flow        = 0;     // -1 rewinding, 0 holding still, +1 running
    Orientation forward     = Orientation::SideA; // side that makes progress grow
    Orientation shownSide   = Orientation::SideA; // side last seen; picks the frame
    Orientation doneSide    = Orientation::SideA; // side the finished cycle ended on
};

uint32_t durationMs(const Session& session);

// Whole seconds left, rounded up so the display starts at the full interval.
uint32_t remainingSeconds(const Session& session);

// "10 min" for the given interval index.
void formatInterval(uint8_t intervalIdx, char* buffer, std::size_t size);

// "mm:ss" for a number of seconds.
void formatClock(uint32_t seconds, char* buffer, std::size_t size);

void setInterval(Session& session, uint8_t intervalIdx);

void enter(Session& session, AppState next, const char* reason);

// Starts a countdown from an empty upper bulb, running towards `side`.
void startCycle(Session& session, Orientation side, const char* reason);

// Back to the menu, throwing the current cycle away.
void toMenu(Session& session, const char* reason);

// Applies an orientation change. Returns true when the state changed.
bool onOrientation(Session& session, Orientation orientation);

// Share of the drawn lower bulb that holds sand right now. `progress` counts
// towards the forward side, but the picture is drawn in the frame of the side the
// device is standing on: standing on the opposite side turns the picture over,
// so the bulb the sand has run into is the upper one of the frame. The sand
// simulation is governed against this share, and the drawing of it agrees by
// construction.
float frameLowerShare(const Session& session);

// Integrates the countdown. Returns true when the state changed.
bool update(Session& session, uint32_t dtMs);

// Debug path for working without an IMU: moves progress by `delta` and resolves
// the resulting state. Marked debug on purpose, it is not part of the product.
void nudge(Session& session, float delta);

}  // namespace app
