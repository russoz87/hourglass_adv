/*
 * Host tests for the timer state machine and the sand progress model.
 *
 * The module is platform independent, so it can be driven by g++ on the
 * machine that builds the firmware - the device itself is installed from SD
 * only and is not flashed during development.
 *
 *   g++ -std=c++17 -O2 -I src tests/app_state_test.cpp src/app_state.cpp \
 *       -o /tmp/app_state_test && /tmp/app_state_test
 *
 * What is linked and what is stubbed:
 *
 *   - orientation.cpp would pull in the device IMU (M5Cardputer.h), so it is
 *     not linked; the pose API is stubbed here with the same contract as the
 *     real one, whose single source of truth is orientation.cpp: a side is
 *     SideA or SideB, and oppositeSide swaps the sides or returns the input.
 *   - hgLog is the device serial log; the tests only need it silent.
 *
 * The scenarios follow the way main.cpp drives the module: onOrientation for
 * the placed poses, update() once per frame with the elapsed milliseconds,
 * nudge() for the debug path, startCycle/toMenu from the key handlers.
 */
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_state.h"
#include "log.h"
#include "orientation.h"

// The pose API of orientation.cpp carries its contract over here (the file
// header explains why it is stubbed). A drift between the two would be caught
// by the state tests falling apart, and the device build links the real module.
bool isSide(Orientation orientation)
{
    return orientation == Orientation::SideA || orientation == Orientation::SideB;
}

Orientation oppositeSide(Orientation orientation)
{
    if (orientation == Orientation::SideA) {
        return Orientation::SideB;
    }
    if (orientation == Orientation::SideB) {
        return Orientation::SideA;
    }
    return orientation;
}

// The device serial log; the tests only need it silent.
void hgLog(const char*, ...) {}

namespace {

int gFailures = 0;

void check(bool ok, const char* what, const char* detail = "")
{
    if (!ok) {
        ++gFailures;
        std::printf("FAIL  %s %s\n", what, detail);
    } else {
        std::printf("ok    %s %s\n", what, detail);
    }
}

bool near(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) < epsilon;
}

app::Session freshSession(uint8_t intervalIdx = 0)
{
    app::Session session;  // state Menu
    app::setInterval(session, intervalIdx);
    return session;
}

app::Session runningSession(uint8_t intervalIdx = 0)
{
    app::Session session = freshSession(intervalIdx);
    app::startCycle(session, Orientation::SideA, "placed on side");
    return session;
}

void testFormatHelpers()
{
    std::printf("-- format helpers --\n");

    char buffer[16];

    app::formatInterval(3, buffer, sizeof(buffer));
    check(std::strcmp(buffer, "10 min") == 0, "formatInterval formats the minutes", buffer);

    app::formatInterval(kIntervalCount + 4, buffer, sizeof(buffer));
    check(std::strcmp(buffer, "1 min") == 0, "formatInterval clamps a stale index", buffer);

    app::formatClock(0, buffer, sizeof(buffer));
    check(std::strcmp(buffer, "00:00") == 0, "formatClock formats zero", buffer);

    app::formatClock(599, buffer, sizeof(buffer));
    check(std::strcmp(buffer, "09:59") == 0, "formatClock pads the fields", buffer);

    app::Session session = freshSession();  // 1 min interval
    check(app::remainingSeconds(session) == 60, "remainingSeconds starts at the full interval");

    session.progress = 0.5f;
    check(app::remainingSeconds(session) == 30, "remainingSeconds takes half of the interval");

    session.progress = 0.995f;
    check(app::remainingSeconds(session) == 1,
          "remainingSeconds rounds up the tail (not 0:00 before the end)");

    session.progress = 1.0f;
    check(app::remainingSeconds(session) == 0, "remainingSeconds is 0 when the cycle is over");
}

void testArmedStartsOnASide()
{
    std::printf("-- armed: starts on a side --\n");

    app::Session session = freshSession();
    app::enter(session, AppState::Armed, "start pressed");

    check(app::onOrientation(session, Orientation::FlatUp) == false,
          "a flat pose does not start the countdown");
    check(session.state == AppState::Armed, "flat pose stays Armed");

    check(app::onOrientation(session, Orientation::SideB) == true,
          "standing on a side starts the countdown");
    check(session.state == AppState::Running, "the state is Running");
    check(session.forward == Orientation::SideB, "the start side is the direction of the cycle");
    check(session.shownSide == Orientation::SideB, "the frame follows the side");
    check(session.flow == 1, "the sand runs towards the forward side");
    check(session.progress == 0.0f, "the cycle starts empty");

    check(app::onOrientation(session, Orientation::FlatUp) == true, "flat pose stops the run");
    check(session.state == AppState::Paused, "paused on flat");
    check(app::update(session, 1000) == false, "no time is integrated while paused");
    check(near(session.progress, 0.0f), "the sand holds while the timer does");

    check(app::onOrientation(session, Orientation::SideB) == true, "the same side resumes");
    check(session.state == AppState::Running, "running again");
    check(near(session.progress, 0.0f), "the sand continues where it stopped");
}

void testCountdownEndsInOut()
{
    std::printf("-- countdown: ends in Done --\n");

    app::Session session = runningSession();  // 1 min = 60000 ms

    check(app::update(session, 60000) == true, "one whole interval ends the cycle");
    check(session.state == AppState::Done, "the state is Done");
    check(session.progress == 1.0f, "the lower bulb is full");
    check(session.flow == 0, "the sand stopped");
    check(session.doneSide == Orientation::SideA, "the end side is remembered for the restart");

    check(app::update(session, 1000) == false, "time in Done does nothing");
}

void testFrameRateIndependence()
{
    std::printf("-- frame-rate independence --\n");

    app::Session oneStep = runningSession();
    check(app::update(oneStep, 30000) == false, "half the interval keeps running");
    check(near(oneStep.progress, 0.5f), "one 30000 ms step is half of a 1 min cycle");

    app::Session twoSteps = runningSession();
    app::update(twoSteps, 15000);
    app::update(twoSteps, 15000);
    check(near(twoSteps.progress, oneStep.progress),
          "two halves integrated the same as one step",
          "(progress drift between call patterns)");

    app::Session thirds = runningSession();
    for (int i = 0; i < 3; ++i) {
        app::update(thirds, 10000);
    }
    check(near(thirds.progress, oneStep.progress), "three thirds integrated the same");

    // A flip mid-countdown reverses the integration: the sand and the clock run
    // backwards from where they stopped.
    app::Session flipped = runningSession();
    app::update(flipped, 15000);
    app::onOrientation(flipped, Orientation::SideB);
    app::update(flipped, 15000);
    check(near(flipped.progress, 0.0f), "a full flip rewinds to the start");

    app::Session bigStep = runningSession();
    app::update(bigStep, 12000);
    app::onOrientation(bigStep, Orientation::SideB);
    app::update(bigStep, 4500);
    check(near(bigStep.progress, 0.2f - 4500.0f / 60000.0f),
          "the rewind follows the same ms-per-progress model in reverse");
}

void testFrameLowerShare()
{
    std::printf("-- frame lower share --\n");

    app::Session session = runningSession();
    app::update(session, 9000);  // progress 0.15 of a 1 min interval

    check(near(app::frameLowerShare(session), session.progress),
          "on the forward side the drawn level is the progress");

    app::onOrientation(session, Orientation::SideB);
    check(near(app::frameLowerShare(session), 1.0f - session.progress),
          "on the opposite side the frame is turned over, so the share is the rest");
}

void testRewindEndsInFinish()
{
    std::printf("-- rewind: ends in Finish --\n");

    app::Session session = runningSession();
    app::update(session, 20000);  // progress 0.333.. of a 1 min interval
    app::onOrientation(session, Orientation::SideB);
    while (session.state == AppState::Running) {
        app::update(session, 1000);
    }

    check(session.state == AppState::Finish, "the sand ran all the way back: Finish");
    check(session.progress == 0.0f, "the upper bulb is full again");
    check(session.doneSide == Orientation::SideB, "the end side is remembered");
    check(app::update(session, 1000) == false, "time in Finish does nothing");

    // Flipping the finished hourglass restarts; the same side just waits.
    check(app::onOrientation(session, Orientation::SideB) == false,
          "the cycle does not restart on the side it ended on");
    check(session.state == AppState::Finish, "stays in Finish");

    check(app::onOrientation(session, Orientation::SideA) == true,
          "a real flip starts the next cycle");
    check(session.state == AppState::Running, "running again after the flip");
    check(session.progress == 0.0f, "the new cycle starts empty");
    check(session.forward == Orientation::SideA,
          "the flip side is the direction of the new cycle");

    // A back-to-menu throws the cycle away even from a finished screen.
    app::update(session, 1000);
    app::toMenu(session, "back");
    check(session.progress == 0.0f, "the menu resets the progress");
    check(app::update(session, 5000) == false, "no countdown in the menu");
}

void testDebugNudgeWalksTheEdges()
{
    std::printf("-- debug nudge: same edges as the clock --\n");

    app::Session session = runningSession();

    for (int i = 0; i < 9; ++i) {
        app::nudge(session, 0.1f);
        check(session.state == AppState::Running, "the nudge steps keep the cycle running");
    }
    app::nudge(session, 0.1f);
    check(session.state == AppState::Done, "ten 10% steps reach Done like the clock does");
    check(near(session.progress, 1.0f), "the progress is clamped at the edge");

    // Restart the way the Enter handler does it, then walk the progress around.
    // Every step that reaches an edge (progress 0 or 1) ends the cycle at once -
    // the same resolveEdge runs here as in the timed path.
    app::startCycle(session, session.doneSide, "restart");
    check(session.state == AppState::Running, "Enter in Done starts a fresh cycle");

    app::nudge(session, -0.1f);
    check(session.state == AppState::Finish,
          "a down step at the empty start ends in Finish at once");
    check(near(session.progress, 0.0f), "the progress is clamped at the start");

    app::startCycle(session, session.doneSide, "restart");
    check(session.state == AppState::Running, "Enter in Finish starts a fresh cycle too");

    app::nudge(session, 0.5f);
    for (int i = 0; i < 4; ++i) {
        app::nudge(session, -0.1f);
    }
    check(session.state == AppState::Running, "rewinding runs until the sand is back");
    check(near(session.progress, 0.1f), "the rewind is at 10 % of the cycle");

    app::nudge(session, -0.15f);
    check(session.state == AppState::Finish, "crossing the start edge ends in Finish");
    check(near(session.progress, 0.0f), "the progress is clamped at the start");

    // Nudges do nothing outside the countdown screens.
    app::toMenu(session, "back");
    app::nudge(session, 0.5f);
    check(session.progress == 0.0f, "a nudge in the menu is ignored");
}

}  // namespace

int main()
{
    testFormatHelpers();
    testArmedStartsOnASide();
    testCountdownEndsInOut();
    testFrameRateIndependence();
    testFrameLowerShare();
    testRewindEndsInFinish();
    testDebugNudgeWalksTheEdges();

    std::printf("\n%s: %d failure(s)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures == 0 ? 0 : 1;
}