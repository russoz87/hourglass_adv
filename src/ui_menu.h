/*
 * Menu and settings screens, drawn in the landscape frame.
 *
 * Both screens are static: they are redrawn only when something changes, so the
 * caller keeps the cursor state and passes it in.
 *
 * The main menu lists the intervals and the way into the settings. The settings
 * screen shows the palette as a grid of swatches (with a preview of the chosen
 * colour) and the sound switch; a change is applied and stored immediately, so
 * there is no save step.
 */
#pragma once

#include <cstdint>

#include "app_state.h"
#include "settings.h"

namespace ui_menu {

// Rows and columns of the palette grid on the settings screen.
constexpr uint8_t kPaletteColumns = 4;
constexpr uint8_t kSoundRow       = 2;  // the sound switch sits below the grid

// Rows of the main menu: one per interval, then the settings entry.
constexpr uint8_t kMenuItemCount    = kIntervalCount + 1;
constexpr uint8_t kMenuSettingsItem = kIntervalCount;

struct SettingsCursor
{
    uint8_t row = 0;  // 0..1 palette rows, kSoundRow: the sound switch
    uint8_t col = 0;  // 0..kPaletteColumns-1, unused on the sound row

    bool    onSound() const { return row >= kSoundRow; }
    uint8_t paletteIdx() const { return static_cast<uint8_t>(row * kPaletteColumns + col); }
};

// Cursor position for the given palette entry.
SettingsCursor cursorForColor(uint8_t colorIdx);

// Cursor after an arrow key. Rows and columns clamp at the edges instead of
// wrapping: on a 4x3 grid wrapping is more confusing than helpful.
SettingsCursor moveCursor(SettingsCursor cursor, int dx, int dy);

// `bootStatus` is the "last boot: ..." line under the rows; an empty string
// draws nothing.
void drawMenu(uint8_t cursor, const settings::Store& store, const char* bootStatus);

void drawSettings(const SettingsCursor& cursor, const settings::Store& store);

}  // namespace ui_menu
