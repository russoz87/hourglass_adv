#include "canvas.h"

#include "log.h"

namespace {

LGFX_Sprite gLandscapeSprite(&M5Cardputer.Display);
LGFX_Sprite gPortraitSprite(&M5Cardputer.Display);

bool gLandscapeReady = false;
bool gPortraitReady  = false;

Frame   gFrame    = Frame::Landscape;
int16_t gRotation = -1;  // panel rotation currently set, -1: not set yet

lgfx::LovyanGFX& panel()
{
    return M5Cardputer.Display;
}

}  // namespace

void canvas::begin()
{
    gLandscapeSprite.setColorDepth(16);
    gLandscapeReady = gLandscapeSprite.createSprite(kLandscapeWidth, kLandscapeHeight) != nullptr;
    if (!gLandscapeReady) {
        hgLog("ui: landscape sprite allocation failed, drawing on the panel");
    }

    gPortraitSprite.setColorDepth(16);
    gPortraitReady = gPortraitSprite.createSprite(kPortraitWidth, kPortraitHeight) != nullptr;
    if (!gPortraitReady) {
        hgLog("ui: portrait sprite allocation failed, drawing on the panel");
    }
}

lgfx::LovyanGFX& canvas::beginFrame(Frame frame, uint8_t rotation)
{
    if (gRotation != static_cast<int16_t>(rotation)) {
        panel().setRotation(rotation);
        gRotation = static_cast<int16_t>(rotation);
    }

    gFrame = frame;

    if (frame == Frame::Portrait && gPortraitReady) {
        return gPortraitSprite;
    }
    if (frame == Frame::Landscape && gLandscapeReady) {
        return gLandscapeSprite;
    }
    return panel();
}

void canvas::endFrame()
{
    if (gFrame == Frame::Portrait) {
        if (gPortraitReady) {
            gPortraitSprite.pushSprite(0, 0);
        }
        return;
    }

    if (gLandscapeReady) {
        gLandscapeSprite.pushSprite(0, 0);
    }
}

bool canvas::usingSprites()
{
    return gLandscapeReady && gPortraitReady;
}
