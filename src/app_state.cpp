#include "app_state.h"

#include <cstdio>

#include "log.h"

namespace {

// Resolves progress reaching either end of the range. Used both by the timed
// update and by the debug nudge so both paths end the cycle the same way.
void resolveEdge(app::Session& session)
{
    if (session.progress >= 1.0f) {
        session.progress = 1.0f;
        session.flow     = 0;
        session.doneSide = session.shownSide;
        app::enter(session, AppState::Done, "interval elapsed");
    } else if (session.progress <= 0.0f) {
        session.progress = 0.0f;
        session.flow     = 0;
        // The sand ran all the way back: the countdown is over, but the app stays
        // on the hourglass screen. Flipping the device starts the next cycle, so
        // remember the side it ended on.
        session.doneSide = session.shownSide;
        app::enter(session, AppState::Finish, "sand ran back up");
    }
}

}  // namespace

const char* appStateName(AppState state)
{
    switch (state) {
        case AppState::Menu:     return "Menu";
        case AppState::Settings: return "Settings";
        case AppState::Armed:    return "Armed";
        case AppState::Running:  return "Running";
        case AppState::Paused:   return "Paused";
        case AppState::Done:     return "Done";
        case AppState::Finish:   return "Finish";
    }
    return "?";
}

uint32_t app::durationMs(const Session& session)
{
    const uint8_t idx = session.intervalIdx < kIntervalCount ? session.intervalIdx : 0;
    return kIntervalMinutes[idx] * 60u * 1000u;
}

uint32_t app::remainingSeconds(const Session& session)
{
    const float remaining = (1.0f - session.progress) * static_cast<float>(durationMs(session));
    if (remaining <= 0.0f) {
        return 0;
    }

    // Round up: the display starts at the full interval and only shows 0:00 when
    // the cycle is actually over.
    const uint32_t seconds = static_cast<uint32_t>(remaining / 1000.0f);
    return (remaining - static_cast<float>(seconds * 1000u) > 0.0f) ? seconds + 1 : seconds;
}

void app::formatInterval(uint8_t intervalIdx, char* buffer, std::size_t size)
{
    const uint8_t idx = intervalIdx < kIntervalCount ? intervalIdx : 0;
    snprintf(buffer, size, "%lu min", static_cast<unsigned long>(kIntervalMinutes[idx]));
}

void app::formatClock(uint32_t seconds, char* buffer, std::size_t size)
{
    snprintf(buffer, size, "%02lu:%02lu", static_cast<unsigned long>(seconds / 60u),
             static_cast<unsigned long>(seconds % 60u));
}

float app::frameLowerShare(const Session& session)
{
    return session.shownSide == session.forward ? session.progress : 1.0f - session.progress;
}

void app::setInterval(Session& session, uint8_t intervalIdx)
{
    if (intervalIdx >= kIntervalCount || intervalIdx == session.intervalIdx) {
        return;
    }

    session.intervalIdx = intervalIdx;
    hgLog("timer: interval %lu min", static_cast<unsigned long>(kIntervalMinutes[intervalIdx]));
}

void app::enter(Session& session, AppState next, const char* reason)
{
    if (session.state == next) {
        return;
    }

    hgLog("state: %s -> %s (%s)", appStateName(session.state), appStateName(next), reason);
    session.state = next;
}

void app::startCycle(Session& session, Orientation side, const char* reason)
{
    if (!isSide(side)) {
        return;
    }

    session.progress  = 0.0f;
    session.flow      = 1;
    session.forward   = side;
    session.shownSide = side;

    enter(session, AppState::Running, reason);
}

void app::toMenu(Session& session, const char* reason)
{
    session.progress = 0.0f;
    session.flow     = 0;
    enter(session, AppState::Menu, reason);
}

bool app::onOrientation(Session& session, Orientation orientation)
{
    const AppState before = session.state;

    switch (session.state) {
        case AppState::Armed:
            // Both short edges are valid starting poses; whichever one the
            // device is put down on becomes the direction of the cycle.
            if (isSide(orientation)) {
                startCycle(session, orientation, "placed on side");
            }
            break;

        case AppState::Running:
        case AppState::Paused:
            if (!isSide(orientation)) {
                // Lying flat or being turned: the sand stops where it is.
                session.flow = 0;
                enter(session, AppState::Paused, "laid flat");
            } else {
                session.shownSide = orientation;
                session.flow = (orientation == session.forward) ? 1 : -1;
                if (session.state == AppState::Paused) {
                    enter(session, AppState::Running, "back on a side");
                }
            }
            break;

        case AppState::Done:
        case AppState::Finish:
            // A finished hourglass is restarted by flipping it over: the full
            // bulb becomes the upper one of a new cycle.
            if (isSide(orientation) && orientation != session.doneSide) {
                startCycle(session, orientation,
                           session.state == AppState::Done ? "flipped after done"
                                                           : "flipped after finish");
            }
            break;

        case AppState::Menu:
        case AppState::Settings:
            break;
    }

    return session.state != before;
}

bool app::update(Session& session, uint32_t dtMs)
{
    if (session.state != AppState::Running || session.flow == 0 || dtMs == 0) {
        return false;
    }

    const AppState before = session.state;

    // Progress per millisecond, so the countdown does not depend on the frame
    // rate or on how often this is called.
    const float delta = static_cast<float>(dtMs) / static_cast<float>(durationMs(session));
    session.progress += (session.flow > 0 ? delta : -delta);
    resolveEdge(session);

    return session.state != before;
}

void app::nudge(Session& session, float delta)
{
    if (session.state != AppState::Running && session.state != AppState::Paused) {
        return;
    }

    session.progress += delta;
    resolveEdge(session);
    hgLog("timer: debug nudge %+.2f -> progress %.2f", delta, session.progress);
}
