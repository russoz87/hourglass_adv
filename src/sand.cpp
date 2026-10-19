/*
 * Falling-sand simulation of the hourglass. See sand.h for the model.
 */
#include "sand.h"

namespace {

// The valve: how many grains may bank up in the token bucket. A frame that is
// slow (or a re-flip right after the first) should not lose throughput, but the
// bucket is not a reservoir either - the level must follow the clock closely.
constexpr float kBucketGrains = 4.0f;

// Automaton passes per step(). A grain falls one cell per pass, so with four
// passes in a 50 ms frame it crosses a bulb in about a second, which reads as
// falling sand; with one pass it looks like a slow crawl.
constexpr int kSubsteps = 4;

// Governor, in grains.
// A gap of one or two grains is what rounding gives and is left alone, so the
// automaton does the drip itself. The surplus slack is just as tight: the valve
// never lets the sand overtake the clock, so anything bigger than rounding came
// from outside physics - the arrow keys - and is repaired.
constexpr int kDeficitBand     = 2;
constexpr int kSurplusSlack    = 4;
constexpr int kTransferPerStep = 16;  // debug jumps need a few of these

// Fixed seed: the same run produces the same picture, which makes the host
// tests repeatable and a screenshot comparable.
constexpr uint32_t kSeed = 0x2545F491u;

}  // namespace

int sand::halfWidthOfRow(int row)
{
    const int16_t y   = static_cast<int16_t>(kOriginY + row);
    const int16_t own = halfWidthAt(kPortraitVessel, y);
    const int16_t mirrored =
        halfWidthAt(kPortraitVessel, static_cast<int16_t>(kOriginY + (kGridHeight - 1 - row)));
    return own < mirrored ? own : mirrored;
}

sand::Simulation::Simulation()
{
    // The grid is the inside of the vessel: row `r` spans the cells between the
    // two glass walls at y = kOriginY + r, narrowed to what survives a flip.
    for (int r = 0; r < kGridHeight; ++r) {
        const int half  = halfWidthOfRow(r);
        const int first = kGridWidth / 2 - half;
        _rowFirst[r]    = static_cast<uint8_t>(first);
        _rowLast[r]     = static_cast<uint8_t>(first + 2 * half - 1);
        _rowCount[r]    = 0;
    }
}

uint32_t sand::Simulation::random()
{
    // xorshift32: one multiply-free step, good enough to pick a sweep direction.
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return _rng;
}

void sand::Simulation::reset(uint32_t durationMs)
{
    for (int i = 0; i < kGridWidth * kGridHeight; ++i) {
        _cells[i] = 0;
    }
    for (int r = 0; r < kGridHeight; ++r) {
        _rowCount[r] = 0;
    }

    _below   = 0;
    _tokens  = 0.0f;
    _settled = false;
    _rng     = kSeed;
    _lastGravity = Gravity::Down;

    // The interval turns into a rate here: the whole upper bulb has to run
    // through the neck in `durationMs`, whatever gravity alone would do.
    _ratePerMs = durationMs == 0 ? 0.0f : static_cast<float>(kGrains) / static_cast<float>(durationMs);

    fillUpperBulb();
}

void sand::Simulation::fillUpperBulb()
{
    // From the throat upwards, so the grains rest on the bottom of the bulb and
    // the surface is what is missing at the top plate. Rows are filled from
    // their middle outwards, which leaves the top row as a mound instead of a
    // flat plate.
    int left = kGrains;
    for (int r = kNeckRow - 1; r >= 0 && left > 0; --r) {
        const int first  = rowFirst(r);
        const int last   = rowLast(r);
        const int center = (first + last) / 2;

        for (int d = 0; d <= last - first && left > 0; ++d) {
            for (int side = 0; side < 2 && left > 0; ++side) {
                const int c = side == 0 ? center + d : center - d;
                if (c < first || c > last || (side == 1 && d == 0)) {
                    continue;
                }
                putGrain(r, c);
                --left;
            }
        }
    }
}

void sand::Simulation::putGrain(int row, int col)
{
    _cells[row * kGridWidth + col] = 1;
    ++_rowCount[row];
}

void sand::Simulation::takeGrain(int row, int col)
{
    _cells[row * kGridWidth + col] = 0;
    --_rowCount[row];
}

void sand::Simulation::flip()
{
    // Point reflection about the middle of the grid: (col, row) ->
    // (kGridWidth-1-col, kGridHeight-1-row). The device is turned over in the
    // hand, so every grain keeps its place in the hand while the frame is drawn
    // the other way up; nothing jumps and nothing is lost.
    for (int r = 0; r < kGridHeight / 2; ++r) {
        uint8_t* upper = &_cells[r * kGridWidth];
        uint8_t* lower = &_cells[(kGridHeight - 1 - r) * kGridWidth];
        for (int c = 0; c < kGridWidth; ++c) {
            const uint8_t tmp = upper[c];
            upper[c]          = lower[kGridWidth - 1 - c];
            lower[kGridWidth - 1 - c] = tmp;
        }

        const uint16_t counts = _rowCount[r];
        _rowCount[r]                   = _rowCount[kGridHeight - 1 - r];
        _rowCount[kGridHeight - 1 - r] = counts;
    }

    // An odd grid keeps its middle row, mirrored on itself.
    uint8_t* middle = &_cells[(kGridHeight / 2) * kGridWidth];
    for (int c = 0; c < kGridWidth / 2; ++c) {
        const uint8_t tmp            = middle[c];
        middle[c]                    = middle[kGridWidth - 1 - c];
        middle[kGridWidth - 1 - c]   = tmp;
    }

    // The grains that had run through the neck are the ones that are above it
    // in the new frame, so the share simply turns around. Counting the grid
    // again would shift a few grains that sit right at the throat (the two
    // bulbs are 78 and 80 rows tall), and correcting that would look like sand
    // running upwards - the one thing this simulation must never show.
    _below   = kGrains - _below;
    _settled = false;
    _lastGravity = Gravity::Down;
}

void sand::Simulation::step(uint32_t dtMs, float lowerShare, bool flowing, Gravity gravity)
{
    if (dtMs == 0) {
        return;
    }

    // A gravity change re-arms the settle: the pile that rested under the old
    // direction still owes its slide under the new one. This is what makes the
    // flat pose start moving a pile that had already gone still during the
    // pause, under the standing physics.
    if (_lastGravity != gravity) {
        _lastGravity = gravity;
        _settled     = false;
    }

    // A flat pose holds the clock: the share, the valve target and the valve
    // itself stand still. Only the standing hourglass (Gravity::Down) runs the
    // valve and lets the governor act.
    const bool standing = gravity == Gravity::Down;

    if (standing) {
        // The timer is the truth: round(lowerShare * kGrains) grains belong
        // below the throat right now.
        int target = static_cast<int>(lowerShare * static_cast<float>(kGrains) + 0.5f);
        if (target < 0) {
            target = 0;
        } else if (target > kGrains) {
            target = kGrains;
        }
        _target = target;

        if (!flowing) {
            // A paused hourglass must not drain the valve's leftover bucket:
            // with the timer stopped no token is added and nothing crosses -
            // exactly what the model says.
            _tokens = 0.0f;
        }
    }

    // Per substep, so the grains that cross the neck spread over the frame
    // instead of leaving in one bunch at its start.
    const float tokensPerSubstep = standing && flowing
        ? _ratePerMs * static_cast<float>(dtMs) / static_cast<float>(kSubsteps)
        : 0.0f;

    // Into the direction the grains fall: a lateral grain lands in a column
    // that was swept already, so it moves at most one cell per pass - the same
    // rule the vertical sweep runs on rows.
    const int dir = gravity == Gravity::Left ? -1 : (gravity == Gravity::Right ? 1 : 0);

    bool moved = false;
    for (int i = 0; i < kSubsteps; ++i) {
        if (standing && flowing) {
            _tokens += tokensPerSubstep;
            if (_tokens > kBucketGrains) {
                _tokens = kBucketGrains;
            }
        }
        if (dir == 0 ? settleOneStep() : settleLateralStep(dir)) {
            moved = true;
        }
    }

    if (standing && govern()) {
        moved = true;
    }

    _settled = !moved;
}

bool sand::Simulation::settleOneStep()
{
    bool moved = false;

    // Bottom up: a grain that falls into a row that has already been swept is
    // not swept again, so no grain moves more than one cell per pass.
    for (int r = kGridHeight - 1; r >= 0; --r) {
        if (_rowCount[r] == 0) {
            continue;
        }

        const int first = rowFirst(r);
        const int last  = rowLast(r);
        const int span  = last - first + 1;

        // A random direction per row: a fixed one would comb every pile towards
        // the same side.
        const bool forward = (random() & 1u) != 0;
        for (int i = 0; i < span; ++i) {
            const int c = forward ? first + i : last - i;
            if (!grain(r, c)) {
                continue;
            }
            if (tryFall(r, c)) {
                moved = true;
            }
        }
    }

    return moved;
}

bool sand::Simulation::settleLateralStep(int dir)
{
    bool moved = false;

    // Columns swept from the destination wall outwards - the mirror of the
    // vertical sweep's bottom-up - so a moved grain is not swept twice; rows
    // in a random order per column.
    const int startCol = dir < 0 ? 0 : kGridWidth - 1;
    const int stepCol  = dir < 0 ? 1 : -1;
    for (int c = startCol; c >= 0 && c < kGridWidth; c += stepCol) {
        const bool downward = (random() & 1u) != 0;
        for (int i = 0; i < kGridHeight; ++i) {
            const int r = downward ? i : kGridHeight - 1 - i;
            if (_rowCount[r] == 0 || !grain(r, c)) {
                continue;
            }
            if (tryFallLateral(r, c, dir)) {
                moved = true;
            }
        }
    }

    return moved;
}

bool sand::Simulation::tryFallLateral(int row, int col, int dir)
{
    const int first = rowFirst(row);
    const int last  = rowLast(row);
    if (col < first || col > last) {
        return false;
    }

    // Straight, toward the wall - then the two spill diagonals, in a random
    // order. A lateral step never leaves the row: the row is the boundary
    // between the bulbs, so nothing can cross the neck sideways, and the
    // grains of the neck row slide inside the neck.
    const int next = col + dir;
    if (next >= first && next <= last && !grain(row, next)) {
        takeGrain(row, col);
        putGrain(row, next);
        return true;
    }

    const bool upFirst = (random() & 1u) != 0;
    for (int i = 0; i < 2; ++i) {
        const int r = row + (((i == 0) == upFirst) ? -1 : 1);
        if (r < 0 || r >= kGridHeight) {
            continue;
        }
        if (next < rowFirst(r) || next > rowLast(r) || grain(r, next)) {
            continue;
        }

        takeGrain(row, col);
        putGrain(r, next);
        return true;
    }

    return false;
}

bool sand::Simulation::tryFall(int row, int col)
{
    const int next = row + 1;
    if (next >= kGridHeight) {
        return false;
    }

    // The neck row is the valve: a grain may only enter it by spending a token,
    // the tokens are the interval's rate, and the timer has to say that the
    // grain belongs there. The last condition is what keeps the sand from
    // overtaking the clock when the two are computed from different deltas.
    // Everything below the neck falls freely - those grains have been paid for.
    const bool crossing = (next == kNeckRow);
    if (crossing && (_tokens < 1.0f || _below >= _target)) {
        return false;
    }

    const int first = rowFirst(next);
    const int last  = rowLast(next);

    if (col >= first && col <= last && !grain(next, col)) {
        takeGrain(row, col);
        putGrain(next, col);
        if (crossing) {
            _tokens -= 1.0f;
            ++_below;
        }
        return true;
    }

    // Blocked straight down: try the two downward diagonals, in a random order.
    const bool leftFirst = (random() & 1u) != 0;
    for (int i = 0; i < 2; ++i) {
        const bool left = (i == 0) == leftFirst;
        const int  side = col + (left ? -1 : 1);
        if (side < first || side > last || grain(next, side)) {
            continue;
        }

        takeGrain(row, col);
        putGrain(next, side);
        if (crossing) {
            _tokens -= 1.0f;
            ++_below;
        }
        return true;
    }

    return false;
}

bool sand::Simulation::govern()
{
    const int deficit = _target - _below;
    bool      moved   = false;

    if (deficit > kDeficitBand) {
        // The sand is behind the clock: it has to fall through the bulb before
        // it can cross, and a jump of the timer jumps further than one frame of
        // falling. Release grains from the face of the upper pile into the neck,
        // where they join the stream. The gap closes from above, so the level of
        // the upper bulb follows the clock as well.
        int limit = deficit - kDeficitBand;
        if (limit > kTransferPerStep) {
            limit = kTransferPerStep;
        }
        while (limit-- > 0 && releaseToNeck()) {
            moved = true;
        }
    } else if (-deficit > kSurplusSlack) {
        // Debug path, not physics: the valve never lets the sand overtake the
        // clock, so sand below the throat that the timer has not sent there can
        // only come from the arrow keys. Lifting it back up is a cheat, kept
        // because walking the timeline without the IMU has to show the right
        // picture.
        int limit = -deficit - kSurplusSlack;
        if (limit > kTransferPerStep) {
            limit = kTransferPerStep;
        }
        while (limit-- > 0 && liftToUpperBulb()) {
            moved = true;
        }
    }

    return moved;
}

bool sand::Simulation::releaseToNeck()
{
    // Free cell right at the throat, so the grain appears where a stream would
    // be and not in the middle of a pile.
    int toRow = -1;
    int toCol = -1;
    for (int r = kNeckRow; r <= kNeckRow + 2 && toRow < 0; ++r) {
        for (int c = rowFirst(r); c <= rowLast(r); ++c) {
            if (!grain(r, c)) {
                toRow = r;
                toCol = c;
                break;
            }
        }
    }
    if (toRow < 0) {
        return false;
    }

    // A grain off the top of the upper pile: the lowest row that still holds
    // sand is its surface.
    int fromRow = -1;
    int fromCol = -1;
    for (int r = 0; r < kNeckRow && fromRow < 0; ++r) {
        for (int c = rowFirst(r); c <= rowLast(r); ++c) {
            if (grain(r, c)) {
                fromRow = r;
                fromCol = c;
                break;
            }
        }
    }
    if (fromRow < 0) {
        return false;
    }

    takeGrain(fromRow, fromCol);
    putGrain(toRow, toCol);
    ++_below;
    return true;
}

bool sand::Simulation::liftToUpperBulb()
{
    // The top grain of the lower pile.
    int fromRow = -1;
    int fromCol = -1;
    for (int r = kNeckRow + 1; r < kGridHeight && fromRow < 0; ++r) {
        for (int c = rowFirst(r); c <= rowLast(r); ++c) {
            if (grain(r, c)) {
                fromRow = r;
                fromCol = c;
                break;
            }
        }
    }
    if (fromRow < 0) {
        return false;
    }

    // Any free cell of the upper bulb will do: the grain falls to the pile by
    // itself on the next steps. Start at the top plate, so an upper bulb that is
    // already full is not disturbed more than necessary.
    int toRow = -1;
    int toCol = -1;
    for (int r = 0; r < kNeckRow && toRow < 0; ++r) {
        for (int c = rowFirst(r); c <= rowLast(r); ++c) {
            if (!grain(r, c)) {
                toRow = r;
                toCol = c;
                break;
            }
        }
    }
    if (toRow < 0) {
        return false;
    }

    takeGrain(fromRow, fromCol);
    putGrain(toRow, toCol);
    --_below;
    return true;
}
