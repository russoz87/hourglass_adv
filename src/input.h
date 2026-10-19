/*
 * Keyboard input for the app.
 *
 * M5Cardputer.Keyboard reports the set of keys held down; this module turns
 * changes of that set into single press events, one per poll, after
 * M5Cardputer.update() has refreshed the keyboard.
 *
 * Three properties of the Cardputer keyboard shape the mapping:
 *   - there are no arrow keys. The arrows are printed on the Fn layer of
 *     ; . , / but the character of a key is the same with and without Fn, so
 *     those four keys are the arrows in this app and navigation never depends
 *     on Fn or on any other modifier being reported;
 *   - there is no Esc key. `del` sits where Esc is on a full keyboard and is
 *     reported as a backspace, so it is the back/cancel key;
 *   - the characters are read from the base layer of the key map, so a stale
 *     shift/ctrl flag cannot turn '1' into '!' or 's' into 'S'.
 *
 * The modifier flags are kept for the serial log only; no action depends on
 * them.
 *
 * TODO verify on device: that ; . , / are physically the keys with the arrow
 * glyphs, and that `del` is comfortable as back. The key log line prints what
 * was pressed, so this is a one-minute check.
 */
#pragma once

#include <cstdint>

struct KeyPress
{
    bool any       = false;
    char ch        = 0;      // printable character, base layer; 0 for the keys below
    bool enter     = false;  // OK
    bool back      = false;  // `del` key: back / cancel (the Esc role)
    bool ok        = false;  // Opt: the second OK key
    bool up        = false;  // ';'
    bool down      = false;  // '.'
    bool left      = false;  // ','
    bool right     = false;  // '/'

    // Reported by the library; logging only.
    bool fn    = false;
    bool shift = false;
    bool ctrl  = false;
    bool opt   = false;
    bool alt   = false;
};

class KeyScanner
{
public:
    // Call after M5Cardputer.update(). Returns at most one new press per call;
    // the main loop polls far faster than a person types, so nothing is lost.
    KeyPress poll();

private:
    // Bitmask of the key matrix (4 rows x 14 columns) held at the last poll.
    uint64_t _previous = 0;
};

// Name of the press for the serial log. Points at a static buffer, valid until
// the next call.
const char* keyPressName(const KeyPress& key);
