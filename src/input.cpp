#include "input.h"

#include <M5Cardputer.h>

namespace {

// Size of the key matrix of the Cardputer keyboard, see Keyboard_def.h upstream.
constexpr int kRows    = 4;
constexpr int kColumns = 14;

int bitOf(const Point2D_t& pos)
{
    return pos.y * kColumns + pos.x;
}

}  // namespace

KeyPress KeyScanner::poll()
{
    auto& keyboard = M5Cardputer.Keyboard;

    uint64_t held = 0;
    for (const Point2D_t& pos : keyboard.keyList()) {
        if (pos.x < 0 || pos.y < 0 || pos.x >= kColumns || pos.y >= kRows) {
            continue;
        }
        held |= 1ull << bitOf(pos);
    }

    const uint64_t fresh = held & ~_previous;
    _previous            = held;
    if (fresh == 0) {
        return KeyPress{};
    }

    // One press per poll: the lowest bit, so the event order is stable.
    const int index = __builtin_ctzll(fresh);
    Point2D_t pos;
    pos.x = index % kColumns;
    pos.y = index / kColumns;

    KeyPress key;

    // Base layer only: modifiers must not change what a key means here.
    const uint8_t code = static_cast<uint8_t>(keyboard.getKeyValue(pos).value_first);

    const auto& state = keyboard.keysState();
    key.fn            = state.fn;
    key.shift         = state.shift;
    key.ctrl          = state.ctrl;
    key.opt           = state.opt;
    key.alt           = state.alt;

    switch (code) {
        case KEY_ENTER:
            key.enter = true;
            break;
        case KEY_BACKSPACE:
            key.back = true;
            break;
        case KEY_OPT:
            key.ok = true;
            break;
        case ';':
            key.up = true;
            break;
        case '.':
            key.down = true;
            break;
        case ',':
            key.left = true;
            break;
        case '/':
            key.right = true;
            break;
        case KEY_TAB:
        case KEY_FN:
        case KEY_LEFT_CTRL:
        case KEY_LEFT_SHIFT:
        case KEY_LEFT_ALT:
            // Modifier on its own: reported for the log, does nothing.
            break;
        default:
            if (code >= 0x20 && code < 0x7F) {
                key.ch = static_cast<char>(code);
            }
            break;
    }

    key.any = true;
    return key;
}

const char* keyPressName(const KeyPress& key)
{
    static char buffer[2] = {0, 0};

    if (key.enter) {
        return "Enter";
    }
    if (key.back) {
        return "Del";
    }
    if (key.ok) {
        return "Opt";
    }
    if (key.up) {
        return "Up";
    }
    if (key.down) {
        return "Down";
    }
    if (key.left) {
        return "Left";
    }
    if (key.right) {
        return "Right";
    }
    if (key.ch != 0) {
        buffer[0] = key.ch;
        return buffer;
    }
    if (key.fn) {
        return "Fn";
    }
    if (key.shift) {
        return "Shift";
    }
    if (key.ctrl) {
        return "Ctrl";
    }
    if (key.alt) {
        return "Alt";
    }
    return "?";
}
