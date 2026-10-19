/*
 * Host tests for the falling-sand simulation.
 *
 * The simulation is platform independent, so it can be driven by g++ on the
 * machine that builds the firmware - the device itself is installed from SD
 * only and is not flashed during development.
 *
 *   g++ -std=c++17 -O2 -I src tests/sand_test.cpp src/sand.cpp -o /tmp/sand_test \
 *       && /tmp/sand_test
 *
 * The scenarios follow the way main.cpp drives the module: one step per 50 ms
 * frame, `lowerShare` from app::frameLowerShare, which for a cycle that is not
 * flipped is just the timer progress.
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "sand.h"
#include "vessel.h"

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

int countGrains(const sand::Simulation& sim)
{
    int total = 0;
    for (int r = 0; r < sand::kGridHeight; ++r) {
        for (int c = 0; c < sand::kGridWidth; ++c) {
            total += sim.cell(c, r) ? 1 : 0;
        }
    }
    return total;
}

int countAboveNeck(const sand::Simulation& sim)
{
    int total = 0;
    for (int r = 0; r < sand::kNeckRow; ++r) {
        for (int c = 0; c < sand::kGridWidth; ++c) {
            total += sim.cell(c, r) ? 1 : 0;
        }
    }
    return total;
}

// No grain outside the glass: a grain there would be stranded for good, because
// the automaton only moves grains into cells it considers part of the vessel.
bool allInsideMask(const sand::Simulation& sim)
{
    for (int r = 0; r < sand::kGridHeight; ++r) {
        const int half  = sand::halfWidthOfRow(r);
        const int first = sand::kGridWidth / 2 - half;
        const int last  = first + 2 * half - 1;
        for (int c = 0; c < sand::kGridWidth; ++c) {
            if (sim.cell(c, r) && (c < first || c > last)) {
                std::printf("      grain outside the mask at row %d col %d\n", r, c);
                return false;
            }
        }
    }
    return true;
}

// Same arithmetic as the valve: grains per millisecond.
float ratePerMs(uint32_t durationMs)
{
    return static_cast<float>(sand::kGrains) / static_cast<float>(durationMs);
}

// Walks the timer like main.cpp does: `dtMs` per frame, one frame per call.
struct Clock
{
    uint32_t durationMs = 60000;
    float    progress   = 0.0f;
    bool     forward    = true;
    bool     running    = true;

    float  lowerShare() const { return forward ? progress : 1.0f - progress; }
    bool   flowing() const { return running; }
    void   advance(uint32_t dtMs)
    {
        if (!running) {
            return;
        }
        progress += (forward ? 1.0f : -1.0f) * static_cast<float>(dtMs) /
                    static_cast<float>(durationMs);
        if (progress < 0.0f) {
            progress = 0.0f;
            running  = false;
        }
        if (progress > 1.0f) {
            progress = 1.0f;
            running  = false;
        }
    }
};

constexpr uint32_t kFrameMs = 50;

void testGeometry()
{
    std::printf("\n-- geometry --\n");

    bool symmetric = true;
    int  upperArea = 0;
    for (int r = 0; r < sand::kGridHeight; ++r) {
        const int half  = sand::halfWidthOfRow(r);
        const int first = sand::kGridWidth / 2 - half;
        const int last  = first + 2 * half - 1;
        if (first + last != sand::kGridWidth - 1) {
            symmetric = false;
        }
        if (r < sand::kNeckRow) {
            upperArea += 2 * half;
        }
    }
    check(symmetric, "rows are centred in the grid");
    check(upperArea >= sand::kGrains, "the upper bulb holds every grain");
    std::printf("      upper bulb %d cells, %d grains\n", upperArea, sand::kGrains);

    // The flip is a point reflection, so every grain has to land inside the mask
    // of the row it lands in. halfWidthOfRow narrows the grid to the part of the
    // vessel that survives the rotation, and this is the check that it worked.
    bool fits = true;
    for (int r = 0; r < sand::kGridHeight; ++r) {
        if (sand::halfWidthOfRow(sand::kGridHeight - 1 - r) < sand::halfWidthOfRow(r)) {
            fits = false;
            std::printf("      row %d does not fit its mirror row\n", r);
        }
    }
    check(fits, "a flipped row fits into the row it lands in");

    // And the grid still follows the glass: nowhere may it be wider than the
    // vessel that is drawn.
    bool inside = true;
    for (int r = 0; r < sand::kGridHeight; ++r) {
        const int glass = halfWidthAt(kPortraitVessel, static_cast<int16_t>(sand::kOriginY + r));
        if (sand::halfWidthOfRow(r) > glass) {
            inside = false;
        }
    }
    check(inside, "the grid stays inside the drawn vessel");
}

void testStartPosition()
{
    std::printf("\n-- start position --\n");

    sand::Simulation sim;
    sim.reset(60000);

    check(countGrains(sim) == sand::kGrains, "all grains are in the upper bulb", "");
    check(sim.grainsBelow() == 0, "nothing below the throat");
    check(countAboveNeck(sim) == sand::kGrains, "nothing in or below the neck row");

    // Settled already: the fill puts the grains on the bottom of the bulb.
    sim.step(kFrameMs, 0.0f, false);
    check(sim.grainsBelow() == 0, "a step without flow crosses nothing");
    check(countGrains(sim) == sand::kGrains, "grains are conserved");
}

void testFullInterval()
{
    std::printf("\n-- 1 min interval, 20 fps --\n");

    sand::Simulation sim;
    Clock           clock;
    clock.durationMs = 60000;
    sim.reset(clock.durationMs);

    int  maxAhead = 0;
    bool monotonic = true;
    int  lastBelow = 0;
    int  frames    = 0;

    while (clock.running && frames < 5000) {
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        clock.advance(kFrameMs);
        ++frames;

        const int below  = sim.grainsBelow();
        const int target = static_cast<int>(clock.lowerShare() * sand::kGrains + 0.5f);
        if (below > target + 2) {
            maxAhead = std::max(maxAhead, below - target);
        }
        if (below < lastBelow) {
            monotonic = false;
        }
        lastBelow = below;
        if (countGrains(sim) != sand::kGrains) {
            check(false, "grains are conserved", "during the run");
            return;
        }
    }

    std::printf("      %d frames, below throat %d of %d, max lead %d\n", frames, lastBelow,
                sand::kGrains, maxAhead);
    check(frames <= 1201, "the run took the interval in frames");
    check(lastBelow >= sand::kGrains - 2, "all grains ran through the neck");
    check(monotonic, "the level only grows while the timer runs");
    check(maxAhead <= 3, "the sand never runs ahead of the clock");
    check(sim.grainsBelow() == sand::kGrains - countAboveNeck(sim), "count matches the grid");
}

void testValveRate()
{
    std::printf("\n-- valve: the interval, not gravity, sets the rate --\n");

    // Five seconds of wall time out of a cycle. Gravity alone empties a bulb of
    // this size in a couple of seconds, so a 30 minute interval has to be about
    // 30 times slower than a 1 minute one.
    auto crossingsIn = [](uint32_t durationMs, int frames) {
        sand::Simulation sim;
        sim.reset(durationMs);
        for (int i = 0; i < frames; ++i) {
            const float progress = static_cast<float>(i * kFrameMs) / static_cast<float>(durationMs);
            sim.step(kFrameMs, progress, true);
        }
        return sim.grainsBelow();
    };

    const int fast = crossingsIn(60000, 100);      // 5 s
    const int slow = crossingsIn(1800000, 100);    // 5 s of a 30 min cycle
    const int expected = static_cast<int>(ratePerMs(60000) * 5000.0f + 0.5f);

    char detail[96];
    std::snprintf(detail, sizeof(detail), "(%d expected)", expected);
    check(std::abs(fast - expected) <= 6, "5 s of a 1 min cycle passes ~250 grains", detail);
    check(slow >= 4 && slow <= 14, "5 s of a 30 min cycle passes ~8 grains");
    std::printf("      1 min: %d grains, 30 min: %d grains\n", fast, slow);
}

void testStoppedTimer()
{
    std::printf("\n-- stopped timer --\n");

    sand::Simulation sim;
    sim.reset(60000);

    // Half way through, then the device is laid flat for a long while.
    for (int i = 0; i < 600; ++i) {
        sim.step(kFrameMs, static_cast<float>(i * kFrameMs) / 60000.0f, true);
    }
    const int frozen = sim.grainsBelow();
    for (int i = 0; i < 2000; ++i) {
        sim.step(kFrameMs, static_cast<float>(600 * kFrameMs) / 60000.0f, false);
    }
    const int held = sim.grainsBelow();

    char detail[96];
    std::snprintf(detail, sizeof(detail), "(%d -> %d)", frozen, held);
    // The valve is off without flow. What is left is the governor's correction
    // of a grain or two, which is what makes the arrow keys work in PAUSED.
    check(std::abs(held - frozen) <= 2, "the sand holds still while the timer does", detail);
    check(held <= 1500, "the sand never runs ahead of the frozen clock");
    check(countGrains(sim) == sand::kGrains, "grains are conserved");
    check(sim.settled(), "the picture comes to rest");
}

void testFlip()
{
    std::printf("\n-- flip --\n");

    sand::Simulation sim;
    Clock           clock;
    sim.reset(clock.durationMs);

    for (int i = 0; i < 600; ++i) {  // 30 s, half of a 1 min cycle
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        clock.advance(kFrameMs);
    }

    const int belowBefore = sim.grainsBelow();
    check(std::abs(belowBefore - sand::kGrains / 2) <= 8, "half the sand ran through in half the interval");

    // Turning the device over: the grid rotates, the frame rotates, the sand
    // stays where it is in the hand.
    const int aboveBefore = countAboveNeck(sim);
    sim.flip();

    char detail[96];
    std::snprintf(detail, sizeof(detail), "(%d -> %d)", belowBefore, sim.grainsBelow());
    check(sim.grainsBelow() == sand::kGrains - belowBefore, "the count turns around", detail);
    check(countGrains(sim) == sand::kGrains, "no grain is lost or doubled");
    std::printf("      above the throat before: %d, below after: %d\n", aboveBefore, sim.grainsBelow());

    // Flipping twice restores the picture exactly: the rotation is a faithful
    // reflection, not a rebuild.
    uint8_t snapshot[sand::kGridWidth * sand::kGridHeight];
    for (int r = 0; r < sand::kGridHeight; ++r) {
        for (int c = 0; c < sand::kGridWidth; ++c) {
            snapshot[r * sand::kGridWidth + c] = sim.cell(c, r) ? 1 : 0;
        }
    }
    const int belowTwice = sim.grainsBelow();
    sim.flip();
    sim.flip();
    bool same = true;
    for (int r = 0; r < sand::kGridHeight; ++r) {
        for (int c = 0; c < sand::kGridWidth; ++c) {
            if ((sim.cell(c, r) ? 1 : 0) != snapshot[r * sand::kGridWidth + c]) {
                same = false;
            }
        }
    }
    check(same && sim.grainsBelow() == belowTwice, "two flips are the identity");

    // The widest part of the lower bulb is the part that does not survive the
    // rotation unchanged, so flip a full lower bulb: the case with the most
    // grains sitting against the walls where the flip could push one out.
    sand::Simulation full;
    Clock           walk;
    full.reset(walk.durationMs);
    for (int i = 0; i < 1300; ++i) {
        full.step(kFrameMs, walk.lowerShare(), walk.flowing());
        walk.advance(kFrameMs);
    }
    full.flip();
    check(countGrains(full) == sand::kGrains && allInsideMask(full),
          "a flip of a full bulb keeps every grain inside the glass");
}

void testRewind()
{
    std::printf("\n-- rewind after a flip --\n");

    sand::Simulation sim;
    Clock           clock;
    sim.reset(clock.durationMs);

    for (int i = 0; i < 600; ++i) {
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        clock.advance(kFrameMs);
    }

    // Turned over: the timer now runs the other way and the frame is flipped.
    sim.flip();
    clock.forward = false;

    bool ok        = true;
    int  maxAhead  = 0;
    int  frames    = 1;
    while (clock.progress > 0.0f && frames < 5000) {
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        clock.advance(kFrameMs);
        ++frames;

        const int target = static_cast<int>(clock.lowerShare() * sand::kGrains + 0.5f);
        if (sim.grainsBelow() > target + 2) {
            maxAhead = std::max(maxAhead, sim.grainsBelow() - target);
        }
        if (countGrains(sim) != sand::kGrains) {
            ok = false;
        }
    }

    char detail[128];
    std::snprintf(detail, sizeof(detail), "(%d frames, below %d of %d)", frames, sim.grainsBelow(),
                  sand::kGrains);
    check(ok, "grains are conserved through the rewind");
    check(clock.progress == 0.0f, "the timer reached the start of the cycle");
    check(sim.grainsBelow() >= sand::kGrains - 2, "the frame's lower bulb holds everything", detail);
    check(maxAhead <= 3, "the sand never runs ahead of the clock");
}

void testDebugJump()
{
    std::printf("\n-- debug jump (arrow keys, no flip) --\n");

    sand::Simulation sim;
    Clock           clock;
    sim.reset(clock.durationMs);

    for (int i = 0; i < 300; ++i) {
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        clock.advance(kFrameMs);
    }
    const int before = sim.grainsBelow();

    // +10 % without the device being flipped: the timer jumps, the sand has to
    // follow, and the only way is the debug path of the governor.
    clock.progress += 0.1f;
    const int jumped = static_cast<int>(clock.lowerShare() * sand::kGrains + 0.5f);

    int frames  = 0;
    int maxLead = 0;
    while (frames < 200) {
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        ++frames;
        if (sim.grainsBelow() > jumped + 2) {
            maxLead = std::max(maxLead, sim.grainsBelow() - jumped);
        }
    }

    char detail[128];
    std::snprintf(detail, sizeof(detail), "(%d -> %d, target %d after %d frames)", before,
                  sim.grainsBelow(), jumped, frames);
    check(std::abs(sim.grainsBelow() - jumped) <= 2, "the level follows a jump of the timer", detail);
    check(maxLead <= 2, "and does not overshoot");

    // A step back down is the cheat: grains are lifted back into the upper bulb.
    clock.progress -= 0.2f;
    const int back = static_cast<int>(clock.lowerShare() * sand::kGrains + 0.5f);
    for (int i = 0; i < 200; ++i) {
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
    }
    std::snprintf(detail, sizeof(detail), "(%d -> %d, target %d)", jumped, sim.grainsBelow(), back);
    check(std::abs(sim.grainsBelow() - back) <= 4, "the level follows a jump backwards", detail);
    check(countGrains(sim) == sand::kGrains, "grains are conserved");
}

void testPileShape()
{
    std::printf("\n-- pile shape --\n");

    sand::Simulation sim;
    sim.reset(60000);

    // Let the initial mound settle without any flow.
    for (int i = 0; i < 100; ++i) {
        sim.step(kFrameMs, 0.0f, false);
    }

    double sum = 0.0;
    int    count = 0;
    for (int r = 0; r < sand::kNeckRow; ++r) {
        for (int c = 0; c < sand::kGridWidth; ++c) {
            if (sim.cell(c, r)) {
                sum += c;
                ++count;
            }
        }
    }
    const double center = static_cast<double>(sand::kGridWidth - 1) / 2.0;
    const double mean   = sum / static_cast<double>(count);
    char detail[96];
    std::snprintf(detail, sizeof(detail), "(mean column %.1f of %.1f)", mean, center);
    check(std::fabs(mean - center) < 1.5, "the pile has no sideways drift", detail);
}

void benchmark()
{
    std::printf("\n-- cost of one step --\n");

    sand::Simulation sim;
    Clock           clock;
    sim.reset(clock.durationMs);

    constexpr int kSteps = 1200;
    for (int i = 0; i < 200; ++i) {  // warm up, and get away from the first pile
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        clock.advance(kFrameMs);
    }

    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < kSteps; ++i) {
        sim.step(kFrameMs, clock.lowerShare(), clock.flowing());
        clock.advance(kFrameMs);
    }
    const auto end = std::chrono::steady_clock::now();

    const double totalMs =
        std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - begin).count();
    std::printf("      %.1f us per step on the host (one 50 ms frame, %d automaton passes)\n",
                totalMs * 1000.0 / kSteps, 4);
}

}  // namespace

int main()
{
    testGeometry();
    testStartPosition();
    testFullInterval();
    testValveRate();
    testStoppedTimer();
    testFlip();
    testRewind();
    testDebugJump();
    testPileShape();
    benchmark();

    std::printf("\n%s: %d failure(s)\n", gFailures == 0 ? "PASS" : "FAIL", gFailures);
    return gFailures == 0 ? 0 : 1;
}
