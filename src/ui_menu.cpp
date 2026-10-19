#include "ui_menu.h"

#include <cstdio>

#include "canvas.h"
#include "hourglass_view.h"  // drawPreview
#include "palette.h"

namespace {

// Main menu geometry, in the 240x135 landscape frame.
constexpr int kMenuRowTop    = 24;
constexpr int kMenuRowHeight = 11;
constexpr int kMenuTextLeft  = 18;
constexpr int kHintTop       = 122;

// Settings geometry.
constexpr int kSwatchLeft     = 8;
constexpr int kSwatchTop      = 26;
constexpr int kSwatchWidth    = 30;
constexpr int kSwatchHeight   = 24;
constexpr int kSwatchStepX    = 36;
constexpr int kSwatchStepY    = 30;
constexpr int kColorNameTop   = 86;
constexpr int kSoundRowTop    = 98;
constexpr int kSoundRowHeight = 14;
constexpr int kSoundRowWidth  = 116;

// Preview of the chosen colour, on the right of the settings grid.
constexpr int kPreviewCenterX    = 195;
constexpr int kPreviewCenterY    = 62;
constexpr int kPreviewHalfWidth  = 24;
constexpr int kPreviewHalfHeight = 30;
constexpr float kPreviewProgress = 0.55f;

void drawText(lgfx::LovyanGFX& g, const char* text, int x, int y, uint8_t size, uint16_t color)
{
    g.setTextSize(size);
    g.setTextColor(color);
    g.setCursor(x, y);
    g.print(text);
}

void beginScene(lgfx::LovyanGFX& g, const char* title)
{
    g.fillScreen(theme::kBackground);
    g.setTextWrap(false);
    g.setTextDatum(textdatum_t::top_left);
    drawText(g, title, 6, 2, 2, theme::kAccent);
    g.drawFastHLine(6, 21, kLandscapeWidth - 12, theme::kRule);
}

}  // namespace

ui_menu::SettingsCursor ui_menu::cursorForColor(uint8_t colorIdx)
{
    SettingsCursor cursor;
    cursor.row = static_cast<uint8_t>(clampPaletteIndex(colorIdx) / kPaletteColumns);
    cursor.col = static_cast<uint8_t>(clampPaletteIndex(colorIdx) % kPaletteColumns);
    return cursor;
}

ui_menu::SettingsCursor ui_menu::moveCursor(SettingsCursor cursor, int dx, int dy)
{
    if (dy < 0 && cursor.row > 0) {
        cursor.row = static_cast<uint8_t>(cursor.row - 1);
    } else if (dy > 0 && cursor.row < kSoundRow) {
        cursor.row = static_cast<uint8_t>(cursor.row + 1);
    }

    if (dx < 0 && !cursor.onSound() && cursor.col > 0) {
        cursor.col = static_cast<uint8_t>(cursor.col - 1);
    } else if (dx > 0 && !cursor.onSound() && cursor.col + 1 < kPaletteColumns) {
        cursor.col = static_cast<uint8_t>(cursor.col + 1);
    }

    return cursor;
}

void ui_menu::drawMenu(uint8_t cursor, const settings::Store& store)
{
    lgfx::LovyanGFX& g = canvas::beginFrame(Frame::Landscape, kRotationLandscape);

    beginScene(g, "Hourglass");

    // Chip in the current hourglass colour, so the menu shows what is set.
    g.fillRect(160, 5, 12, 12, paletteColor(store.colorIdx));
    g.drawRect(160, 5, 12, 12, theme::kRule);

    for (uint8_t i = 0; i < kMenuItemCount; ++i) {
        const int  y          = kMenuRowTop + i * kMenuRowHeight;
        const bool isCursor   = (i == cursor);
        const bool isSelected = (i != kMenuSettingsItem) && (i == store.intervalIdx);

        if (isCursor) {
            g.fillRect(4, y - 1, kLandscapeWidth - 8, kMenuRowHeight, theme::kCursorFill);
            drawText(g, ">", 6, y, 1, theme::kAccent);
        }

        char        buffer[12];
        const char* label = buffer;
        if (i == kMenuSettingsItem) {
            label = "Settings";
        } else {
            app::formatInterval(i, buffer, sizeof(buffer));
        }

        drawText(g, label, kMenuTextLeft, y, 1, isSelected ? theme::kAccent : theme::kText);

        if (isSelected) {
            g.fillRect(11, y + 2, 3, 3, theme::kAccent);
        }
    }

    drawText(g, "1-7 interval  Enter  S settings", 6, kHintTop, 1, theme::kTextDim);

    canvas::endFrame();
}

void ui_menu::drawSettings(const SettingsCursor& cursor, const settings::Store& store)
{
    lgfx::LovyanGFX& g = canvas::beginFrame(Frame::Landscape, kRotationLandscape);

    beginScene(g, "Settings");

    for (uint8_t idx = 0; idx < kPaletteCount; ++idx) {
        const int x = kSwatchLeft + (idx % kPaletteColumns) * kSwatchStepX;
        const int y = kSwatchTop + (idx / kPaletteColumns) * kSwatchStepY;

        g.fillRect(x, y, kSwatchWidth, kSwatchHeight, paletteColor(idx));

        if (store.colorIdx == idx) {
            // The colour the hourglass uses: light frame just inside the swatch.
            g.drawRect(x + 1, y + 1, kSwatchWidth - 2, kSwatchHeight - 2, theme::kText);
        }
        if (!cursor.onSound() && cursor.paletteIdx() == idx) {
            // Where the cursor is: accent frame around the swatch.
            g.drawRect(x - 2, y - 2, kSwatchWidth + 4, kSwatchHeight + 4, theme::kAccent);
        }
    }

    drawText(g, paletteName(store.colorIdx), 10, kColorNameTop, 1, theme::kTextDim);

    const bool soundCursor = cursor.onSound();
    if (soundCursor) {
        g.fillRect(4, kSoundRowTop - 3, kSoundRowWidth, kSoundRowHeight, theme::kCursorFill);
    }
    drawText(g, "Sound", 10, kSoundRowTop, 1, soundCursor ? theme::kText : theme::kTextDim);
    drawText(g, store.soundOn ? "On" : "Off", 52, kSoundRowTop, 1,
             store.soundOn ? theme::kAccent : theme::kTextDim);

    hourglass_view::drawPreview(g, kPreviewCenterX, kPreviewCenterY, kPreviewHalfWidth,
                                kPreviewHalfHeight, kPreviewProgress,
                                paletteColor(store.colorIdx));

    drawText(g, "Arrows move  Enter toggle  Del back", 6, kHintTop, 1, theme::kTextDim);

    canvas::endFrame();
}
