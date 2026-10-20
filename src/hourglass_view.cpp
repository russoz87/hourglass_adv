#include "hourglass_view.h"

#include "canvas.h"
#include "palette.h"
#include "sand.h"
#include "vessel.h"

namespace {

// Rotation of the portrait frame. Standing on one short edge or the other, one
// of the two portrait rotations shows the picture the right way up; verified
// on the device with the sand of both edges (they run down in the drawing).
constexpr uint8_t kRotationSideA = kRotationPortraitA;
constexpr uint8_t kRotationSideB = kRotationPortraitB;

// The outline: the glass wall per row, following the profile - so the drawing
// stays on the shape whatever it is (straight trapezoids, the rounded bulbs,
// anything row-wise the profile says). One pixel per wall keeps the sand that
// leans on the glass visible under it, as the run-length drawing does.
void drawOutline(lgfx::LovyanGFX& g, const Vessel& vessel, uint16_t color)
{
    for (int16_t y = vessel.top; y <= vessel.bottom; ++y) {
        const int16_t half = halfWidthAt(vessel, y);
        g.drawPixel(vessel.centerX - half, y, color);
        g.drawPixel(vessel.centerX + half - 1, y, color);
    }

    // The plates the bulbs are sealed with.
    g.fillRect(vessel.centerX - vessel.halfWidth - 2, vessel.top - 3, 2 * vessel.halfWidth + 4, 3,
               color);
    g.fillRect(vessel.centerX - vessel.halfWidth - 2, vessel.bottom, 2 * vessel.halfWidth + 4, 3,
               color);
}

// Fills the share of a bulb that holds sand. In the upper bulb the sand rests
// on its bottom, above the orifice: as the bulb drains the surface drops from
// the top plate towards the neck, so the empty space grows from the top. In the
// lower bulb the sand piles up from the bottom plate.
//
// This is the simple proportional fill; the hourglass scenes draw the
// simulation instead, and the settings preview has no room for a cellular
// automaton.
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
                   float lowerShare, uint16_t sand)
{
    const uint16_t outline = dimmed(sand, 45);

    drawOutline(g, vessel, outline);
    fillUpperBulb(g, vessel, upperShare, sand);
    fillLowerBulb(g, vessel, lowerShare, sand);
}

// The sand out of the simulation: one cell per pixel, run-length per row so a
// row of sand is a single horizontal transfer instead of ninety draws. Drawn
// before the outline, so the glass stays on top of the pile.
void drawSand(lgfx::LovyanGFX& g, const sand::Simulation& sim, uint16_t color)
{
    for (int row = 0; row < sand::kGridHeight; ++row) {
        int col = 0;
        while (col < sand::kGridWidth) {
            if (!sim.cell(col, row)) {
                ++col;
                continue;
            }

            const int start = col;
            while (col < sand::kGridWidth && sim.cell(col, row)) {
                ++col;
            }
            g.drawFastHLine(sand::kOriginX + start, sand::kOriginY + row, col - start, color);
        }
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

// A finished hourglass: the whole load has run down, so the picture is the same
// whether the interval ran out (DONE) or the sand ran all the way back (FINISH).
// Only the words differ.
void drawFinished(const sand::Simulation& sim, uint16_t sand, Orientation side, const char* title,
                  const char* hint)
{
    lgfx::LovyanGFX& g =
        canvas::beginFrame(Frame::Portrait, hourglass_view::rotationForSide(side));

    g.fillScreen(theme::kBackground);
    g.setTextWrap(false);
    g.setTextDatum(textdatum_t::top_left);

    drawCentered(g, title, kPortraitWidth / 2, 6, 3, sand);

    drawSand(g, sim, sand);
    drawOutline(g, kPortraitVessel, dimmed(sand, 45));

    drawCentered(g, hint, kPortraitWidth / 2, 212, 1, theme::kText);
    drawCentered(g, "Enter: new  Del: menu", kPortraitWidth / 2, 223, 1, theme::kTextDim);

    canvas::endFrame();
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

    drawHourglass(g, vessel, 1.0f - progress, progress, color);
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

void hourglass_view::drawRunning(const app::Session& session, const sand::Simulation& sim,
                                 uint16_t sand, bool paused, bool showClock)
{
    lgfx::LovyanGFX& g =
        canvas::beginFrame(Frame::Portrait, rotationForSide(session.shownSide));

    g.fillScreen(theme::kBackground);
    g.setTextWrap(false);
    g.setTextDatum(textdatum_t::top_left);

    char interval[12];
    app::formatInterval(session.intervalIdx, interval, sizeof(interval));

    if (showClock) {
        char clock[8];
        app::formatClock(app::remainingSeconds(session), clock, sizeof(clock));
        drawCentered(g, clock, kPortraitWidth / 2, 6, 3, theme::kText);
        drawCentered(g, interval, kPortraitWidth / 2, 34, 1, theme::kTextDim);
    } else {
        // The clock is hidden by setting: the interval alone takes its place,
        // small and dim, so the top of the screen keeps its structure.
        drawCentered(g, interval, kPortraitWidth / 2, 10, 1, theme::kTextDim);
    }

    // The grains are drawn where the simulation has them, in the frame of the
    // side the device stands on. Turning the device over rotates the grid
    // inside the simulation, so the picture never jumps and the sand always
    // runs downwards in the frame that is drawn.
    drawSand(g, sim, sand);
    drawOutline(g, kPortraitVessel, dimmed(sand, 45));

    if (paused) {
        drawCentered(g, "Paused", kPortraitWidth / 2, 218, 2, theme::kAccent);
    }

    canvas::endFrame();
}

void hourglass_view::drawDone(const app::Session& session, const sand::Simulation& sim,
                              uint16_t sand)
{
    drawFinished(sim, sand, session.doneSide, "Done", "Flip to restart");
}

void hourglass_view::drawFinish(const app::Session& session, const sand::Simulation& sim,
                                uint16_t sand)
{
    drawFinished(sim, sand, session.doneSide, "Finish", "Flip to start");
}
