/*
 * The hourglass scenes.
 *
 * ARMED is still drawn in the landscape frame (the device is lying flat, that is
 * where the "place on side to start" hint is readable). RUNNING, PAUSED and DONE
 * are drawn in the portrait frame: the device stands on one of its short edges,
 * so the hourglass axis is the 240 px side and the picture is rotated to stay
 * upright for the viewer.
 *
 * The sand level is a placeholder for stage 3: the bulbs are geometric shapes
 * filled proportionally to app::Session::progress, and the falling-sand cellular
 * automaton will replace exactly the two fill calls in drawHourglass().
 *
 * Everything is drawn into the canvas for the frame, in the frame's own
 * coordinates: the portrait scenes never see the landscape layout.
 */
#pragma once

#include <M5Cardputer.h>
#include <cstdint>

#include "app_state.h"
#include "orientation.h"

namespace hourglass_view {

// Landscape: the selected interval and "place on side to start".
void drawArmed(const app::Session& session, uint16_t sand);

// Portrait: hourglass, mm:ss and, when paused, the paused label.
void drawRunning(const app::Session& session, uint16_t sand, bool paused);

// Portrait: the finished hourglass (lower bulb full) and the restart hint.
void drawDone(const app::Session& session, uint16_t sand);

// Panel rotation that keeps the picture upright when the device stands on the
// given side. The side also decides which end of the drawing is the top.
uint8_t rotationForSide(Orientation side);

// Small hourglass glyph with `progress` of the sand in the lower bulb. Used by
// the settings screen as a preview of the chosen colour.
void drawPreview(lgfx::LovyanGFX& g, int centerX, int centerY, int halfWidth, int halfHeight,
                 float progress, uint16_t color);

}  // namespace hourglass_view
