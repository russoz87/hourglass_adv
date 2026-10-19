#include "hourglass_view.h"

#include "canvas.h"
#include "palette.h"

namespace {

// Shape of the drawn hourglass, in the coordinates of the frame it is drawn in.
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

// Rotation of the portrait frame. Standing on one short edge or the other, one
// of the two portrait rotations shows the picture the right way up.
// TODO verify on device: if a side shows the picture upside down, swap these.
constexpr uint8_t kRotationSideA = kRotationPortraitA;
constexpr uint8_t kRotationSideB = kRotationPortraitB;

int16_t halfWidthAt(const Vessel& vessel, int16_t y)
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

void drawOutline(lgfx::LovyanGFX& g, const Vessel& vessel, uint16_t color)
{
    g.drawLine(vessel.centerX - vessel.halfWidth, vessel.top, vessel.centerX - vessel.neckHalfWidth,
               vessel.neck, color);
    g.drawLine(vessel.centerX + vessel.halfWidth, vessel.top, vessel.centerX + vessel.neckHalfWidth,
               vessel.neck, color);
    g.drawLine(vessel.centerX - vessel.neckHalfWidth, vessel.neck, vessel.centerX - vessel.halfWidth,
               vessel.bottom, color);
    g.drawLine(vessel.centerX + vessel.neckHalfWidth, vessel.neck, vessel.centerX + vessel.halfWidth,
               vessel.bottom, color);

    // The plates the bulbs are sealed with.
    g.fillRect(vessel.centerX - vessel.halfWidth - 2, vessel.top - 3, 2 * vessel.halfWidth + 4, 3,
               color);
    g.fillRect(vessel.centerX - vessel.halfWidth - 2, vessel.bottom, 2 * vessel.halfWidth + 4, 3,
               color);
}

// Fills the share of a bulb that holds sand. In the upper bulb the sand
// rests on its bottom, above the orifice: as the bulb drains the surface
// drops from the top plate towards the neck, so the empty space grows from
// the top. In the lower bulb the sand piles up from the bottom plate.
void fillUpperBulb(lgfx::LovyanGFX& g, const Vessel& vessel, float share, uint16_t color)
{
    if (share <= 0.0f) {
        return;
    }
    if (share > 1.0f) {
        share = 1.0f;
    }

    const int16_t level = static_cast<int16_t>(
        vessel.neck - share * static_cast<float>(vessel.neck - vessel.top));

    for (int16_t y = level; y <= vessel.neck; ++y) {
        const int16_t half = halfWidthAt(vessel, y);
        g.drawFastHLine(vessel.centerX - half, y, 2 * half, color);
    }
}

void fillLowerBulb(lgfx::LovyanGFX& g, const Vessel& vessel, float share, uint16_t color)
{
    if (share <= 0.0f) {
        return;
    }
    if (share > 1.0f) {
        share = 1.0f;
    }

    const int16_t level = static_cast<int16_t>(vessel.bottom -
                                               share * static_cast<float>(vessel.bottom - vessel.neck));

    for (int16_t y = level; y <= vessel.bottom; ++y) {
        const int16_t half = halfWidthAt(vessel, y);
        g.drawFastHLine(vessel.centerX - half, y, 2 * half, color);
    }
}

// The whole picture: glass outline plus sand. `upperShare` and `lowerShare`
// are the fractions of the two bulbs that hold sand, in the frame being drawn.
// Which bulb is "upper" is a property of the frame, not of the timer: standing
// the device on the opposite side rotates the picture 180 degrees, so the
// bulb the sand has run into becomes the upper one (see drawRunning).
void drawHourglass(lgfx::LovyanGFX& g, const Vessel& vessel, float upperShare,
                   float lowerShare, uint16_t sand, bool flowing)
{
    const uint16_t outline = dimmed(sand, 45);

    drawOutline(g, vessel, outline);
    fillUpperBulb(g, vessel, upperShare, sand);
    fillLowerBulb(g, vessel, lowerShare, sand);

    if (flowing) {
        // Placeholder for the falling stream: a short block in the neck. Stage 3
        // draws the real stream there, out of the cellular automaton.
        g.fillRect(vessel.centerX - 1, vessel.neck - 2, 2, 6, sand);
    }
}

void drawCentered(lgfx::LovyanGFX& g, const char* text, int centerX, int y, uint8_t size,
                  uint16_t color)
{
    g.setTextSize(size);
    g.setTextColor(color);
    g.setTextDatum(textdatum_t::top_center);
    g.drawString(text, centerX, y);
    g.setTextDatum(textdatum_t::top_left);
}

}  // namespace

uint8_t hourglass_view::rotationForSide(Orientation side)
{
    return side == Orientation::SideB ? kRotationSideB : kRotationSideA;
}

void hourglass_view::drawPreview(lgfx::LovyanGFX& g, int centerX, int centerY, int halfWidth,
                                 int halfHeight, float progress, uint16_t color)
{
    const Vessel vessel = {static_cast<int16_t>(centerX),
                           static_cast<int16_t>(halfWidth),
                           static_cast<int16_t>(centerY - halfHeight),
                           static_cast<int16_t>(centerY),
                           static_cast<int16_t>(centerY + halfHeight),
                           static_cast<int16_t>(halfWidth / 8 + 1)};

    drawHourglass(g, vessel, 1.0f - progress, progress, color, false);
}

void hourglass_view::drawArmed(const app::Session& session, uint16_t sand)
{
    lgfx::LovyanGFX& g = canvas::beginFrame(Frame::Landscape, kRotationLandscape);

    g.fillScreen(theme::kBackground);
    g.setTextWrap(false);
    g.setTextDatum(textdatum_t::top_left);

    char interval[12];
    app::formatInterval(session.intervalIdx, interval, sizeof(interval));

    drawCentered(g, interval, kLandscapeWidth / 2, 12, 3, sand);
    drawCentered(g, "Place on side to start", kLandscapeWidth / 2, 48, 1, theme::kText);
    drawCentered(g, "Enter: start without IMU", kLandscapeWidth / 2, 70, 1, theme::kTextDim);
    drawCentered(g, "Del: menu", kLandscapeWidth / 2, 82, 1, theme::kTextDim);

    canvas::endFrame();
}

void hourglass_view::drawRunning(const app::Session& session, uint16_t sand, bool paused)
{
    lgfx::LovyanGFX& g =
        canvas::beginFrame(Frame::Portrait, rotationForSide(session.shownSide));

    g.fillScreen(theme::kBackground);
    g.setTextWrap(false);
    g.setTextDatum(textdatum_t::top_left);

    char clock[8];
    app::formatClock(app::remainingSeconds(session), clock, sizeof(clock));
    drawCentered(g, clock, kPortraitWidth / 2, 6, 3, theme::kText);

    char interval[12];
    app::formatInterval(session.intervalIdx, interval, sizeof(interval));
    drawCentered(g, interval, kPortraitWidth / 2, 34, 1, theme::kTextDim);

    // Standing on the opposite side flips the picture 180 degrees: the bulb
    // the sand has run into (the forward bulb) is then the physically upper
    // one, so the two shares swap. The sand itself never jumps: only the
    // frame rotates around it.
    const bool flipped = session.shownSide != session.forward;
    const float upperShare = flipped ? session.progress : 1.0f - session.progress;
    drawHourglass(g, kPortraitVessel, upperShare, 1.0f - upperShare, sand, session.flow != 0);

    if (paused) {
        drawCentered(g, "Paused", kPortraitWidth / 2, 218, 2, theme::kAccent);
    }

    canvas::endFrame();
}

void hourglass_view::drawDone(const app::Session& session, uint16_t sand)
{
    lgfx::LovyanGFX& g =
        canvas::beginFrame(Frame::Portrait, rotationForSide(session.doneSide));

    g.fillScreen(theme::kBackground);
    g.setTextWrap(false);
    g.setTextDatum(textdatum_t::top_left);

    drawCentered(g, "Done", kPortraitWidth / 2, 6, 3, sand);
    drawHourglass(g, kPortraitVessel, 0.0f, 1.0f, sand, false);

    drawCentered(g, "Flip to restart", kPortraitWidth / 2, 212, 1, theme::kText);
    drawCentered(g, "Enter: new  Del: menu", kPortraitWidth / 2, 223, 1, theme::kTextDim);

    canvas::endFrame();
}
