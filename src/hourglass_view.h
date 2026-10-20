/*
 * The hourglass scenes.
 *
 * ARMED is still drawn in the landscape frame (the device is lying flat, that is
 * where the "place on side to start" hint is readable). RUNNING, PAUSED and DONE
 * are drawn in the portrait frame: the device stands on one of its short edges,
 * so the hourglass axis is the 240 px side and the picture is rotated to stay
 * upright for the viewer.
 *
 // The sand of the portrait scenes is the falling-sand simulation, drawn
 * grain by grain out of the grid it lives in. The only proportional fill left is
 * the settings preview, which has no room for a cellular automaton.
 *
 * Everything is drawn into the canvas for the frame, in the frame's own
 * coordinates: the portrait scenes never see the landscape layout.
 */
#pragma once

#include <M5Cardputer.h>
#include <cstdint>

#include "app_state.h"
#include "orientation.h"
#include "sand.h"

namespace hourglass_view {

// Landscape: the selected interval and "place on side to start". The countdown
// begins from a real placement only.
void drawArmed(const app::Session& session);

// Portrait: hourglass, mm:ss (unless hidden by setting) and, when paused, the
// paused label. The sand comes from the simulation, which already holds the
// layout of the frame that is being drawn.
void drawRunning(const app::Session& session, const sand::Simulation& sim, uint16_t sand,
                 bool paused, bool showClock);

// Portrait: the finished hourglass (lower bulb full) and the restart hint.
// DONE after the interval ran out, FINISH after the sand ran all the way back.
void drawDone(const app::Session& session, const sand::Simulation& sim, uint16_t sand);
void drawFinish(const app::Session& session, const sand::Simulation& sim, uint16_t sand);

// Panel rotation that keeps the picture upright when the device stands on the
// given side. The side also decides which end of the drawing is the top.
uint8_t rotationForSide(Orientation side);

// Small hourglass glyph with `progress` of the sand in the lower bulb. Used by
// the settings screen as a preview of the chosen colour.
void drawPreview(lgfx::LovyanGFX& g, int centerX, int centerY, int halfWidth, int halfHeight,
                 float progress, uint16_t color);

}  // namespace hourglass_view
