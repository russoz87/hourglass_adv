/*
 * The shape of the drawn hourglass, shared by the drawing code and by the
 * falling-sand grid.
 *
 * The grid of the simulation has to cover exactly the inside of the vessel that
 * is drawn, so these four numbers live in one place and both modules read them.
 * Coordinates are those of the portrait frame (135 x 240), the frame the big
 * hourglass is drawn in.
 *
 * The shape is a pair of trapezoid bulbs - two straight walls either side of an
 * 8 px neck. The simulation only needs the mask
 * the shape cuts out of the grid, so a rounder vessel would be a change here.
 */
#pragma once

#include <cstdint>

struct Vessel
{
    int16_t centerX;
    int16_t halfWidth;      // half width at the plates
    int16_t top;            // y of the top plate
    int16_t neck;           // y of the neck
    int16_t bottom;         // y of the bottom plate
    int16_t neckHalfWidth;
};

// The big drawing of the portrait scenes: two 80 px bulbs either side of the
// neck, leaving room for the clock above and the status line below.
constexpr Vessel kPortraitVessel = {67, 50, 46, 124, 204, 4};

// Half width of the inside of the vessel at a given y, in pixels. Row y is
// drawn as the run [centerX - halfWidthAt(y), centerX + halfWidthAt(y)), so
// the glass that is drawn and the grid the sand lives in have the same edge.
inline int16_t halfWidthAt(const Vessel& vessel, int16_t y)
{
    if (y <= vessel.neck) {
        // Tapering from the top plate towards the neck.
        const float t = static_cast<float>(y - vessel.top) /
                        static_cast<float>(vessel.neck - vessel.top);
        return static_cast<int16_t>(vessel.halfWidth +
                                    (vessel.neckHalfWidth - vessel.halfWidth) * t);
    }

    // Widening from the neck towards the bottom plate.
    const float t = static_cast<float>(y - vessel.neck) /
                    static_cast<float>(vessel.bottom - vessel.neck);
    return static_cast<int16_t>(vessel.neckHalfWidth +
                                (vessel.halfWidth - vessel.neckHalfWidth) * t);
}
