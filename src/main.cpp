/*
 * Hourglass - stage 2 firmware for the M5Stack Cardputer-ADV.
 *
 * This stage is the frame of the app: module structure, menu and settings, NVS,
 * the timer state machine, orientation from the IMU and the completion beep. The
 * falling-sand animation is stage 3 and is deliberately not here yet: the
 * hourglass scene fills its bulbs proportionally to the timer progress.
 *
 * main.cpp only wires the modules together: it samples the keyboard and the IMU,
 * feeds the state machine and decides when a screen has to be redrawn.
 *
 *   app_state.*       states, transitions, sand progress (milliseconds, not frames)
 *   orientation.*     accelerometer -> side / flat / unstable, debounced
 *   settings.*        NVS namespace "hourglass"
 *   ui_menu.*         menu and settings screens (landscape)
 *   hourglass_view.*  ARMED / RUNNING / PAUSED / DONE (portrait rotated frame)
 *   canvas.*          the off-screen sprites for the two frames
 *   palette.h         the eight preset colours
 *   input.*           keyboard press events
 *
 * Two debug paths exist because the app has to be usable while the IMU pose is
 * off: Enter in ARMED skips the "place on side" step, and in RUNNING/PAUSED the
 * up/down keys move progress by 10 % per press. If the device is lying flat the
 * state machine pauses right after a forced start - that is what flat means -
 * and the arrow keys still move the sand, which is enough to walk through all
 * the screens without an IMU.
 *
 * Build:   pio run
 * Install: copy .pio/build/cardputer-adv/firmware.bin to a microSD card and
 *          install it from the Launcher SD menu - never flash over USB, that
 *          would overwrite the installed Launcher.
 */

#include <M5Cardputer.h>

#include "app_state.h"
#include "canvas.h"
#include "hourglass_view.h"
#include "input.h"
#include "log.h"
#include "orientation.h"
#include "palette.h"
#include "settings.h"
#include "ui_menu.h"

namespace {

constexpr uint32_t kSerialBaud      = 115200;
constexpr uint32_t kLoopDelayMs     = 5;
constexpr uint32_t kRunningRenderMs = 50;   // 20 frames per second while running
constexpr uint8_t  kBrightness      = 128;  // the app is watched for minutes
constexpr uint8_t  kSpeakerVolume   = 128;

// Completion signal: two short beeps at 880 Hz.
constexpr float    kBeepHz    = 880.0f;
constexpr uint32_t kBeepMs    = 110;
constexpr uint32_t kBeepGapMs = 150;
constexpr uint8_t  kDoneBeeps = 2;

// Debug step per arrow press, see the header of this file.
constexpr float kDebugStep = 0.1f;

settings::Store         gSettings;
app::Session            gSession;
OrientationTracker      gOrientation;
KeyScanner              gKeys;
uint8_t                 gMenuCursor = 0;
ui_menu::SettingsCursor gSettingsCursor;

Orientation gLastOrientation = Orientation::Unstable;

// Static screens are redrawn when they change, the running countdown on a fixed
// interval: an idle screen must not cost anything.
bool     gDirty        = true;
uint32_t gLastRenderMs = 0;
uint32_t gLastTickMs   = 0;

// The beeps are queued and played from the loop, so nothing blocks the timer.
uint8_t  gBeepsLeft  = 0;
uint32_t gNextBeepMs = 0;

const char* boardName(m5::board_t board)
{
    switch (board) {
        case m5::board_t::board_M5CardputerADV: return "M5Cardputer-ADV";
        case m5::board_t::board_M5Cardputer:    return "M5Cardputer";
        default:                                return "unknown";
    }
}

void startDoneBeeps(uint32_t nowMs)
{
    if (!gSettings.soundOn) {
        return;
    }
    gBeepsLeft  = kDoneBeeps;
    gNextBeepMs = nowMs;
}

void serviceBeeps(uint32_t nowMs)
{
    // Signed difference: safe across the millis() wrap after ~49 days.
    if (gBeepsLeft == 0 || static_cast<int32_t>(nowMs - gNextBeepMs) < 0) {
        return;
    }

    M5.Speaker.tone(kBeepHz, kBeepMs);
    --gBeepsLeft;
    gNextBeepMs = nowMs + kBeepMs + kBeepGapMs;
}

void applyInterval(uint8_t idx)
{
    gSettings.setInterval(idx);
    app::setInterval(gSession, gSettings.intervalIdx);
    gMenuCursor = gSettings.intervalIdx;
}

void openSettings()
{
    gSettingsCursor = ui_menu::cursorForColor(gSettings.colorIdx);
    app::enter(gSession, AppState::Settings, "settings opened");
}

// The only place that knows how a key maps to an action in each state.
void handleKey(const KeyPress& key)
{
    switch (gSession.state) {
        case AppState::Menu:
            if (key.up && gMenuCursor > 0) {
                --gMenuCursor;
                gDirty = true;
            }
            if (key.down && gMenuCursor + 1 < ui_menu::kMenuItemCount) {
                ++gMenuCursor;
                gDirty = true;
            }
            if (key.ch >= '1' && key.ch <= '7') {
                applyInterval(static_cast<uint8_t>(key.ch - '1'));
                gDirty = true;
            }
            if (key.ch == 's') {
                openSettings();
            }
            if (key.enter || key.ok) {
                if (gMenuCursor == ui_menu::kMenuSettingsItem) {
                    openSettings();
                } else {
                    applyInterval(gMenuCursor);
                    gSession.progress = 0.0f;
                    app::enter(gSession, AppState::Armed, "start pressed");
                }
            }
            if (key.back) {
                // There is no parent screen: the Launcher owns the boot choice,
                // so the app cannot exit to it by itself.
                hgLog("menu: no screen to go back to");
            }
            break;

        case AppState::Settings: {
            const bool onSoundRow = gSettingsCursor.onSound();

            if (key.up || key.down) {
                const int dy    = (key.down ? 1 : 0) - (key.up ? 1 : 0);
                gSettingsCursor = ui_menu::moveCursor(gSettingsCursor, 0, dy);
                if (!gSettingsCursor.onSound()) {
                    // Moving onto a swatch selects it: the preview is the
                    // confirmation and the choice goes to NVS right away.
                    gSettings.setColor(gSettingsCursor.paletteIdx());
                }
                gDirty = true;
            }

            if (key.left || key.right) {
                if (onSoundRow) {
                    gSettings.setSound(!gSettings.soundOn);
                } else {
                    const int dx    = (key.right ? 1 : 0) - (key.left ? 1 : 0);
                    gSettingsCursor = ui_menu::moveCursor(gSettingsCursor, dx, 0);
                    gSettings.setColor(gSettingsCursor.paletteIdx());
                }
                gDirty = true;
            }

            if ((key.enter || key.ok || key.ch == ' ') && onSoundRow) {
                gSettings.setSound(!gSettings.soundOn);
                gDirty = true;
            }

            if (key.back) {
                app::enter(gSession, AppState::Menu, "settings closed");
                gMenuCursor = ui_menu::kMenuSettingsItem;
            }
            break;
        }

        case AppState::Armed:
            if (key.enter || key.ok) {
                // Debug path: start without the IMU. The frame keeps the side it
                // has, so the picture is the same as after a real placement.
                hgLog("debug: forced start without IMU");
                app::startCycle(gSession, gSession.shownSide, "forced start");
            }
            if (key.back) {
                app::toMenu(gSession, "back");
            }
            break;

        case AppState::Running:
        case AppState::Paused:
            // Debug path: move progress without the IMU, 10 % per press. It works
            // in both states, so a demo does not depend on the device being held
            // exactly on a side.
            if (key.up) {
                app::nudge(gSession, kDebugStep);
                gDirty = true;
            }
            if (key.down) {
                app::nudge(gSession, -kDebugStep);
                gDirty = true;
            }
            if (key.back) {
                app::toMenu(gSession, "back");
            }
            break;

        case AppState::Done:
            if (key.enter || key.ok) {
                app::startCycle(gSession, gSession.doneSide, "restart");
            }
            if (key.back) {
                app::toMenu(gSession, "back");
            }
            break;
    }
}

void render()
{
    const uint16_t color = paletteColor(gSettings.colorIdx);

    switch (gSession.state) {
        case AppState::Menu:     ui_menu::drawMenu(gMenuCursor, gSettings); break;
        case AppState::Settings: ui_menu::drawSettings(gSettingsCursor, gSettings); break;
        case AppState::Armed:    hourglass_view::drawArmed(gSession, color); break;
        case AppState::Running:  hourglass_view::drawRunning(gSession, color, false); break;
        case AppState::Paused:   hourglass_view::drawRunning(gSession, color, true); break;
        case AppState::Done:     hourglass_view::drawDone(gSession, color); break;
    }
}

}  // namespace

void setup()
{
    Serial.begin(kSerialBaud);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);  // true: initialise the keyboard

    M5Cardputer.Display.setRotation(kRotationLandscape);
    M5Cardputer.Display.setBrightness(kBrightness);
    M5.Speaker.setVolume(kSpeakerVolume);

    canvas::begin();

    gSettings.load();
    app::setInterval(gSession, gSettings.intervalIdx);
    gMenuCursor = gSettings.intervalIdx;

    gOrientation.begin(millis());
    gLastOrientation = gOrientation.update(millis());

    Serial.printf("\nHourglass stage 2\n");
    Serial.printf("board    : %s\n", boardName(M5.getBoard()));
    Serial.printf("display  : %dx%d landscape, %dx%d portrait\n", kLandscapeWidth,
                  kLandscapeHeight, kPortraitWidth, kPortraitHeight);
    Serial.printf("keyboard : %s\n",
                  M5.getBoard() == m5::board_t::board_M5CardputerADV ? "TCA8418 (ADV)"
                                                                     : "io matrix");
    hgLog("imu      : %s", M5.Imu.isEnabled() ? "enabled" : "not detected");
    hgLog("sprites  : %s", canvas::usingSprites() ? "landscape + portrait" : "not allocated");
    hgLog("heap     : %lu bytes free", static_cast<unsigned long>(ESP.getFreeHeap()));
    hgLog("settings : interval %u min, color %u (%s), sound %s",
          static_cast<unsigned>(kIntervalMinutes[gSettings.intervalIdx]),
          static_cast<unsigned>(gSettings.colorIdx), paletteName(gSettings.colorIdx),
          gSettings.soundOn ? "on" : "off");

    gLastTickMs   = millis();
    gLastRenderMs = gLastTickMs;
    gDirty        = true;
}

void loop()
{
    M5Cardputer.update();

    const uint32_t now    = millis();
    const AppState before = gSession.state;

    const KeyPress key = gKeys.poll();
    if (key.any) {
        hgLog("key: %s (fn %u shift %u ctrl %u opt %u alt %u)", keyPressName(key),
              static_cast<unsigned>(key.fn), static_cast<unsigned>(key.shift),
              static_cast<unsigned>(key.ctrl), static_cast<unsigned>(key.opt),
              static_cast<unsigned>(key.alt));
        handleKey(key);
    }

    const Orientation orientation = gOrientation.update(now);
    if (orientation != gLastOrientation) {
        gLastOrientation = orientation;
        app::onOrientation(gSession, orientation);
        gDirty = true;
    }

    app::update(gSession, now - gLastTickMs);
    gLastTickMs = now;

    if (gSession.state != before) {
        gDirty = true;
        if (gSession.state == AppState::Done) {
            startDoneBeeps(now);
        } else {
            gBeepsLeft = 0;  // leaving DONE drops what is left of the signal
        }
    }

    serviceBeeps(now);

    const bool running = gSession.state == AppState::Running;
    if (gDirty || (running && now - gLastRenderMs >= kRunningRenderMs)) {
        gDirty        = false;
        gLastRenderMs = now;
        render();
    }

    delay(kLoopDelayMs);
}
