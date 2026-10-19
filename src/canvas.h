/*
 * Off-screen canvases.
 *
 * The panel is a 135x240 portrait panel that the app uses as a 240x135
 * landscape screen, so there are two frames: landscape for the menu, the
 * settings and the ARMED hint, and portrait (the rotated frame) for the
 * hourglass itself.
 *
 * Each frame has its own sprite. A scene draws into the sprite and the finished
 * frame is pushed to the panel in one transfer, which is what keeps the display
 * free of flicker. If a sprite cannot be allocated (~65 kB each) drawing falls
 * back to the panel itself: both are LovyanGFX surfaces, so the drawing code
 * does not care.
 *
 * The panel rotation is chosen per frame: rotation 1 is the landscape frame, and
 * the portrait frame uses rotation 0 or 2 depending on which short edge the
 * device stands on.
 */
#pragma once

#include <M5Cardputer.h>
#include <cstdint>

enum class Frame : uint8_t
{
    Landscape,
    Portrait,
};

constexpr int kLandscapeWidth  = 240;
constexpr int kLandscapeHeight = 135;
constexpr int kPortraitWidth   = 135;
constexpr int kPortraitHeight  = 240;

// LovyanGFX rotation ids.
constexpr uint8_t kRotationLandscape = 1;
constexpr uint8_t kRotationPortraitA = 0;
constexpr uint8_t kRotationPortraitB = 2;

namespace canvas {

void begin();

// Selects the panel rotation for the frame and returns the surface to draw on.
lgfx::LovyanGFX& beginFrame(Frame frame, uint8_t rotation);

// Blits the frame drawn since beginFrame() to the panel.
void endFrame();

bool usingSprites();

}  // namespace canvas
