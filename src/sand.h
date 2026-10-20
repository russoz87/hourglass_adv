/*
 * Falling-sand simulation of the hourglass.
 *
 * Platform independent on purpose: no M5 or Arduino headers, no clock, no
 * drawing. Time comes in as a millisecond delta, so the same module runs in the
 * firmware and in the host tests (tests/sand_test.cpp).
 *
 * The grid covers the inside of the drawn portrait vessel, one cell per pixel
 * (100 x 159 cells). A cell holds at most one grain; `kGrains` grains are about
 * 80 % of the upper bulb's area, which fills it to ~85 % of its height.
 *
 * Three mechanisms:
 *
 *   gravity  - the classic automaton: a grain falls if it can, else tries one of
 *              the two downward diagonals, else it stays. Rows are swept from
 *              the bottom up with the sweep direction drawn at random per row,
 *              so a pile has no preferred side.
 *   repose   - a pile steeper than 45 degrees cannot hold: when straight-down
 *              and both down-diagonals are occupied, but a side column is two
 *              or more levels lower, the grain rolls off the slope and falls
 *              there. This is what heals a lateral pile back into the standing
 *              shape, and that is what makes every flat settle repeatable: the
 *              slide always starts from a re-centered pile. A slope of exactly
 *              one level per column is stable, so the mounds the stream builds
 *              keep their shape.
 *   valve    - a token bucket on the neck. `kGrains / durationMs` grains may
 *              cross it per millisecond, so a 30 minute hourglass drips its
 *              3000 grains over 30 minutes and a 1 minute one over a minute.
 *              Natural physics would empty a bulb in seconds; the valve is what
 *              makes the animation last as long as the interval. With the timer
 *              stopped no token is added and nothing crosses.
 *   governor - the timer is the truth, the grain count is not. The governor
 *              compares the grains below the throat with the share `lowerShare`
 *              says round(lowerShare * kGrains) and repairs what the automaton
 *              alone cannot: it releases grains from the face of the upper pile
 *              into the neck when the sand is behind. In the other direction it
 *              is a debug path only (see govern()).
 *   flat     - while the device lies flat the picture keeps its upright frame
 *              and the gravity a real lying hourglass sees runs ACROSS that
 *              picture: `step()` is called with Gravity::Left or ::Right and
 *              the grains slide toward the lower wall of their own bulb, with
 *              the spill diagonals of a lying pile. The valve and the governor
 *              stand still, and a lateral step never crosses the neck line
 *              (the straight slide does not leave its row; the diagonals are
 *              forbidden at the neck line), so nothing runs bulb to bulb.
 *
 * A flip is not a rebuild of the scene: the grid is rotated 180 degrees, so the
 * sand keeps its place in the hand while the frame turns with the device. The
 * bulb that was full at the bottom becomes the upper one and drains from then
 * on, which is the rewind. Sand never runs upwards on its own.
 */
#pragma once

#include <cstdint>

#include "vessel.h"

namespace sand {

// The gravity direction of one step, in the frame that is drawn. `Down` is the
// standing hourglass. `Left`/`Right` are the flat poses: which of the two side
// walls is physically lower cannot be read out of a flat pose (the gravity then
// points straight into the screen), so the convention is flat - screen up
// settles to the left wall, screen down to the right one.
enum class Gravity : uint8_t
{
    Down,
    Left,
    Right,
};

// One grain per pixel. kCellPx = 2 would halve the grid in each direction and
// cost a quarter of the work, at the price of visibly blocky sand.
constexpr int kCellPx = 1;

constexpr int kGridWidth  = 2 * kPortraitVessel.halfWidth;                      // 100
constexpr int kGridHeight = kPortraitVessel.bottom - kPortraitVessel.top + 1;   // 159

// Row of the throat. Grains in this row and below it have run through the neck:
// the throat line of the profile sits on the exact mirror row of the grid, so
// the neck pipe is symmetric and the crossing counts once, right at its top.
constexpr int kNeckRow = kPortraitVessel.neck - kPortraitVessel.top;            // 79

// Where the grid sits in the portrait frame: cell (col, row) is the pixel
// (kOriginX + col, kOriginY + row).
constexpr int kOriginX = kPortraitVessel.centerX - kPortraitVessel.halfWidth;   // 17
constexpr int kOriginY = kPortraitVessel.top;                                  // 46

// Number of grains. Upper bulb: 6242 cells, so 5000 fills it to ~85 % of its
// height (69 of the 79 bulb rows); the lower bulb is its exact twin - the
// throat line sits on the grid's mirror row - and holds the same.
constexpr int kGrains = 5000;

// Half width of the grid at a grid row. The grid is the part of the vessel that
// survives a flip: the narrower of a row and the row it is reflected onto. The
// profile is symmetric about the throat line, so the two agree per row and the
// narrowing is exact - a flip can never push a grain through the glass and
// leave it stuck against a wall. The function stays the guard it always was
// for a future asymmetric profile.
int halfWidthOfRow(int row);

class Simulation
{
public:
    Simulation();

    // A cycle starts: every grain settled in the upper bulb, nothing below the
    // throat. `durationMs` is the interval, which only sets the valve rate.
    void reset(uint32_t durationMs);

    // The device was turned over: rotate the grid 180 degrees. The grains keep
    // their physical place, the frame around them turns, and the share that had
    // run into the lower bulb is above the neck from now on.
    void flip();

    // Advances the sand by `dtMs`. `lowerShare` is the share of the drawn lower
    // bulb that should hold sand (app::frameLowerShare), `flowing` is false
    // while the timer holds still, and then nothing crosses the neck.
    // `gravity` is the direction the grains move in this step: `Down` runs the
    // standing hourglass (valve, governor), `Left`/`Right` the flat settle.
    void step(uint32_t dtMs, float lowerShare, bool flowing, Gravity gravity = Gravity::Down);

    // True when the last step moved nothing under the gravity of `gravity`:
    // the pile is at rest for that direction and the caller can stop animating
    // the picture. A gravity change re-arms the settle (the resting lateral
    // pile still owes its slide, and the other way around), so gate the
    // stepping with the direction you are about to apply.
    bool settled(Gravity gravity = Gravity::Down) const
    {
        return _settled && _lastGravity == gravity;
    }

    // Grains below the throat, in the frame that is drawn right now.
    int grainsBelow() const { return _below; }

    // Grid access for the renderer.
    static constexpr int width() { return kGridWidth; }
    static constexpr int height() { return kGridHeight; }

    bool cell(int col, int row) const
    {
        if (col < 0 || col >= kGridWidth || row < 0 || row >= kGridHeight) {
            return false;
        }
        return _cells[row * kGridWidth + col] != 0;
    }

private:
private:
    // Valid columns of a row: [first, last], inclusive. Rows outside the vessel
    // have first > last.
    int rowFirst(int row) const { return _rowFirst[row]; }
    int rowLast(int row) const { return _rowLast[row]; }

    bool  grain(int row, int col) const { return _cells[row * kGridWidth + col] != 0; }
    void  putGrain(int row, int col);
    void  takeGrain(int row, int col);

    void fillUpperBulb();
    bool settleOneStep();
    bool settleLateralStep(int dir);
    bool tryFall(int row, int col);
    bool tryFallLateral(int row, int col, int dir);
    bool govern();
    bool releaseToNeck();
    bool liftToUpperBulb();

    uint32_t random();

    uint8_t  _cells[kGridWidth * kGridHeight] = {};
    uint16_t _rowCount[kGridHeight]           = {};
    uint8_t  _rowFirst[kGridHeight]           = {};
    uint8_t  _rowLast[kGridHeight]            = {};

    float    _ratePerMs   = 0.0f;  // valve: grains per millisecond
    float    _tokens      = 0.0f;  // valve: grains that may still cross
    int      _target      = 0;     // grains the timer says belong below the throat
    int      _below       = 0;     // grains that have run through the neck
    bool     _settled     = true;
    Gravity  _lastGravity = Gravity::Down;  // the direction "settled" was measured under
    uint32_t _rng         = 1;
};

}  // namespace sand
