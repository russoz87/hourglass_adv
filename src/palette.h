/*
 * Colours of the Hourglass UI.
 *
 * The user chooses one colour for the whole hourglass picture (the presets
 * below); the outline of the glass is drawn in a dimmed variant of that colour
 * so the picture still reads as one colour. Everything else on screen - text,
 * rules, cursor - uses the fixed theme colours, which are not configurable.
 */
#pragma once

#include <cstddef>
#include <cstdint>

// Packs 8-bit RGB into the RGB565 format the panel and the sprites use.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

constexpr uint16_t kBackground = rgb565(0, 0, 0);
constexpr uint16_t kText       = rgb565(226, 232, 238);
constexpr uint16_t kTextDim    = rgb565(116, 124, 134);
constexpr uint16_t kAccent     = rgb565(255, 196, 84);
constexpr uint16_t kCursorFill = rgb565(42, 48, 56);
constexpr uint16_t kRule       = rgb565(64, 70, 78);
constexpr uint16_t kError      = rgb565(240, 80, 80);

}  // namespace theme

struct Palette
{
    const char* name;
    uint16_t    color;
};

// Eight presets; index 0 is the classic sand colour and the default.
constexpr std::size_t kPaletteCount = 8;

constexpr Palette kPalettes[kPaletteCount] = {
    {"Classic",  rgb565(232, 201, 125)},
    {"Ivory",    rgb565(242, 232, 205)},
    {"Amber",    rgb565(255, 160, 40)},
    {"Copper",   rgb565(214, 120, 62)},
    {"Emerald",  rgb565(60, 210, 140)},
    {"Sapphire", rgb565(70, 150, 255)},
    {"Ruby",     rgb565(240, 80, 90)},
    {"Frost",    rgb565(190, 214, 235)},
};

// Scales a colour towards black; percent 0 is black, 100 is the colour itself.
// Single return statement: the build standard is gnu++11, whose constexpr
// functions allow nothing else.
constexpr uint16_t dimmed(uint16_t color, uint8_t percent)
{
    return static_cast<uint16_t>((((((color >> 11) & 0x1F) * percent) / 100) << 11) |
                                 ((((color >> 5) & 0x3F) * percent) / 100) << 5 |
                                 (((color & 0x1F) * percent) / 100));
}

// Palette lookups clamp their index: a stale NVS value must not read past the
// table.
constexpr uint8_t clampPaletteIndex(uint8_t idx)
{
    return idx < kPaletteCount ? idx : 0;
}

constexpr uint16_t paletteColor(uint8_t idx)
{
    return kPalettes[clampPaletteIndex(idx)].color;
}

constexpr const char* paletteName(uint8_t idx)
{
    return kPalettes[clampPaletteIndex(idx)].name;
}
