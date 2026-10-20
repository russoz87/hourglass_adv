/*
 * The shape of the drawn hourglass, shared by the drawing code and by the
 * falling-sand grid.
 *
 * The grid of the simulation has to cover exactly the inside of the vessel that
 * is drawn, so these numbers live in one place and both modules read them.
 * Coordinates are those of the portrait frame (135 x 240), the frame the big
 * hourglass is drawn in.
 *
 * The shape: two bulbs rounded the whole way - each opens from the neck pipe
 * on a quarter-ellipse and runs vertical into its plate - joined by a short
 * pipe that keeps the neck width. The throat line sits on the grid's exact
 * mirror row, so the two bulbs are equal twins; a flip mirrors the profile on
 * itself and has nothing to reconcile.
 */
#pragma once

#include <cmath>
#include <cstdint>

struct Vessel
{
    int16_t centerX;
    int16_t halfWidth;      // half width at the plates: the widest the glass gets
    int16_t top;            // y of the top plate
    int16_t neck;           // y of the throat line (the middle of the neck pipe)
    int16_t bottom;         // y of the bottom plate
    int16_t neckHalfWidth;
};

// The big drawing of the portrait scenes: two 77 px bulb zones either side of
// a 5 px neck pipe (rows 46..122, throat 123..127, 128..204), leaving room for
// the clock above and the status line below. The throat y = 125 lands on the
// grid's exact mirror row (159 rows: row 79 mirrors onto itself).
constexpr Vessel kPortraitVessel = {67, 50, 46, 125, 204, 4};

// Half length of the neck pipe, in rows: the rows right at the throat line
// that hold the neck width before the bulb starts opening.
constexpr int16_t kNeckPipeHalfRows = 2;

// Half width of the inside of the vessel at a given y, in pixels. Row y is
// drawn as the run [centerX - halfWidthAt(y), centerX + halfWidthAt(y)), so
// the glass that is drawn and the grid the sand lives in have the same edge.
inline int16_t halfWidthAt(const Vessel& vessel, int16_t y)
{
    int16_t d = y - vessel.neck;
    if (d < 0) {
        d = -d;
    }
    d -= kNeckPipeHalfRows;
    if (d <= 0) {
        // The pipe: the throat keeps its half width for a few rows, so the
        // waist reads as a short tube and not as a pinch to a point.
        return vessel.neckHalfWidth;
    }

    // The bulb opens on a quarter-ellipse from the pipe to the plate: u = 1 at
    // the pipe (zero opening) and 0 at the plate (the full half width with a
    // zero slope - the wall runs vertical into the plate, which is what keeps
    // the silhouette round instead of conical). Out of the throat the flare is
    // the fastest, like real glass curling away from a narrow neck.
    const float bulbRows = static_cast<float>(vessel.bottom - vessel.neck - kNeckPipeHalfRows);
    float u = 1.0f - static_cast<float>(d) / bulbRows;
    if (u < 0.0f) {
        u = 0.0f;
    }
    return static_cast<int16_t>(static_cast<float>(vessel.neckHalfWidth) +
        static_cast<float>(vessel.halfWidth - vessel.neckHalfWidth) * sqrtf(1.0f - u * u) +
        0.5f);
}