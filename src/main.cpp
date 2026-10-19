/*
 * Hourglass - stage 3 firmware for the M5Stack Cardputer-ADV.
 *
 * The app is complete: menu and settings, NVS, the timer state machine, IMU
 * orientation, the completion chime, and the falling-sand animation that draws the
 * hourglass. main.cpp wires the modules together: it samples the keyboard and the
 * IMU, feeds the state machine and the simulation, and decides when a screen has
 * to be redrawn.
 *
 *   app_state.*       states, transitions, sand progress (milliseconds, not frames)
 *   sand.*            falling-sand cellular automaton (platform independent)
 *   orientation.*     accelerometer -> side / flat / unstable, debounced
 *   settings.*        NVS namespace "hourglass"
 *   ui_menu.*         menu and settings screens (landscape)
 *   hourglass_view.*  ARMED / RUNNING / PAUSED / DONE (portrait rotated frame)
 *   canvas.*          the off-screen sprites for the two frames
 *   vessel.h          the shape of the glass, shared by the drawing and the grid
 *   palette.h         the eight preset colours
 *   input.*           keyboard press events
 *
 * The timer and the sand are two clocks over one truth: `session.progress` is
 * what the countdown says, and the simulation is steered to show exactly that
 * much sand below the throat. This file owns the simulation - when a cycle starts
 * it is reset, when the device is turned over it is flipped, and while a screen
 * shows the hourglass it is stepped with the elapsed milliseconds.
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
#include <cstdio>
#include <esp_system.h>

#include "app_state.h"
#include "canvas.h"
#include "hourglass_view.h"
#include "input.h"
#include "log.h"
#include "orientation.h"
#include "palette.h"
#include "sand.h"
#include "settings.h"
#include "ui_menu.h"

namespace {

constexpr uint32_t kSerialBaud      = 115200;
constexpr uint32_t kLoopDelayMs     = 5;
constexpr uint32_t kRunningRenderMs = 50;   // 20 frames per second while running
constexpr uint8_t  kBrightness      = 128;  // the app is watched for minutes

// Master volume. The completion signal is the only sound the app makes, and it
// is a full-scale square wave from Speaker::tone: the speaker path sums the two
// channels for its mono output, so 128 puts the sum exactly on the rail - the
// loudest setting that does not clip, and clipping is what made louder
// attempts sound harsher rather than louder.
constexpr uint8_t  kSpeakerVolume   = 128;

// The sand advances at the frame rate, not every pass of the loop: the automaton
// is a whole-grid sweep and has nothing to show between two frames.
constexpr uint32_t kSandStepMs = kRunningRenderMs;

// A stall (menu, a long key press) must not make the sand lurch when the picture
// comes back: the governor repairs whatever this drops.
constexpr uint32_t kMaxSandStepMs = 200;

// How often the cost of a sand step is reported on the serial log.
constexpr uint16_t kSandLogSteps = 200;

// Completion signal: two short tones, a fourth apart.
//
// Speaker::tone() is the only audio path this device has run without a reset -
// every raw PCM request through playRaw() panicked it, at 16 kHz and at the
// codec's native 48 kHz alike (the investigation and what is still open are
// recorded in the README backlog). A square wave has no envelope to soften, so
// the signal is a short two-note figure instead of the two identical beeps the
// stage 2 build played: the first note sings, the second one, longer and lower,
// closes it.
constexpr float    kDoneNoteHz[]  = {880.0f, 1174.66f};  // A5, then D6
constexpr uint32_t kDoneNoteMs[]  = {120, 200};
constexpr uint8_t  kDoneNoteCount = 2;
constexpr uint32_t kDoneNoteGapMs = 60;

// Debug step per arrow press, see the header of this file.
constexpr float kDebugStep = 0.1f;

settings::Store         gSettings;
app::Session            gSession;
sand::Simulation        gSim;
OrientationTracker      gOrientation;
KeyScanner              gKeys;
uint8_t                 gMenuCursor = 0;
ui_menu::SettingsCursor gSettingsCursor;

Orientation gLastOrientation = Orientation::Unstable;

// Shown at the bottom of the menu: why the device started this time. After a
// reset that is the first thing the screen says - the reason itself is gone by
// the time the app runs.
char gBootStatus[40] = "";

// Static screens are redrawn when they change, the running countdown on a fixed
// interval: an idle screen must not cost anything.
bool     gDirty        = true;
uint32_t gLastRenderMs = 0;
uint32_t gLastTickMs   = 0;
uint32_t gLastSandMs   = 0;

// Cost of a sand step, reported every kSandLogSteps steps so the budget can be
// checked from the serial log on the device.
uint32_t gSandSteps   = 0;
uint32_t gSandUsSum   = 0;
uint32_t gSandUsMax   = 0;

// The completion signal is played one note at a time from the loop: tone()
// replaces whatever is sounding, so the gap between the calls separates the
// notes, and nothing blocks while they play.
uint8_t  gNotesLeft  = 0;
uint32_t gNextNoteMs = 0;

// Why the device started this time. A reset that happens while the chime plays
// shows up here on the next boot: `brownout` is the supply sagging under the
// amplifier, `panic` an exception (with a Guru Meditation line above it).
const char* resetReasonName(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "external pin";
        case ESP_RST_SW:        return "software";
        case ESP_RST_PANIC:     return "panic (crash)";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_BROWNOUT:  return "brownout (supply)";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

const char* boardName(m5::board_t board)
{
    switch (board) {
        case m5::board_t::board_M5CardputerADV: return "M5Cardputer-ADV";
        case m5::board_t::board_M5Cardputer:    return "M5Cardputer";
        default:                                return "unknown";
    }
}

// Starts the two-note completion signal. The notes are played one at a time
// from the loop: tone() replaces whatever is sounding, so the gap between the
// calls is what separates the notes, and nothing blocks while they play.
void startDoneChime(uint32_t nowMs)
{
    if (!gSettings.soundOn) {
        return;
    }
    gNotesLeft  = kDoneNoteCount;
    gNextNoteMs = nowMs;
}

void serviceChime(uint32_t nowMs)
{
    if (gNotesLeft == 0 || static_cast<int32_t>(nowMs - gNextNoteMs) < 0) {
        return;
    }

    const uint8_t idx = static_cast<uint8_t>(kDoneNoteCount - gNotesLeft);
    // Explicit channel 0, like the shipped ADV audio apps: the automatic
    // channel pick is one more moving part that the working setup avoids.
    M5.Speaker.tone(kDoneNoteHz[idx], kDoneNoteMs[idx], /*channel=*/0,
                    /*stop_current_sound=*/true);
    --gNotesLeft;
    gNextNoteMs = nowMs + kDoneNoteMs[idx] + kDoneNoteGapMs;
}

// Turns the switch around. Switching sound on plays the signal at once: that is
// what the switch enables, and it also makes the audio path checkable on the
// device without waiting out a whole interval.
void toggleSound()
{
    gSettings.setSound(!gSettings.soundOn);
    gDirty = true;
    if (gSettings.soundOn) {
        startDoneChime(millis());
    }
}

void applyInterval(uint8_t idx)
{
    gSettings.setInterval(idx);
    app::setInterval(gSession, gSettings.intervalIdx);
    gMenuCursor = gSettings.intervalIdx;
}

// A cycle starts with the whole upper bulb full, whatever the sand did before:
// that is what progress = 0 means for the state machine.
void startCycle(Orientation side, const char* reason)
{
    app::startCycle(gSession, side, reason);
    if (gSession.state == AppState::Running) {
        gSim.reset(app::durationMs(gSession));
        gLastSandMs = millis();
        hgLog("sand: cycle reset, %d grains above the neck", sand::kGrains);
    }
}

// The device was turned over: rotate the sand instead of rebuilding the scene.
void flipSand()
{
    gSim.flip();
    hgLog("sand: flipped, %d grains below the neck", gSim.grainsBelow());
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
                    toggleSound();
                } else {
                    const int dx    = (key.right ? 1 : 0) - (key.left ? 1 : 0);
                    gSettingsCursor = ui_menu::moveCursor(gSettingsCursor, dx, 0);
                    gSettings.setColor(gSettingsCursor.paletteIdx());
                }
                gDirty = true;
            }

            if ((key.enter || key.ok || key.ch == ' ') && onSoundRow) {
                toggleSound();
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
                startCycle(gSession.shownSide, "forced start");
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
                startCycle(gSession.doneSide, "restart");
            }
            if (key.back) {
                app::toMenu(gSession, "back");
            }
            break;

        case AppState::Finish:
            // Same as DONE: a fresh cycle on Enter, the menu on Del.
            if (key.enter || key.ok) {
                startCycle(gSession.doneSide, "restart");
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
        case AppState::Menu:     ui_menu::drawMenu(gMenuCursor, gSettings, gBootStatus); break;
        case AppState::Settings: ui_menu::drawSettings(gSettingsCursor, gSettings); break;
        case AppState::Armed:    hourglass_view::drawArmed(gSession, color); break;
        case AppState::Running:  hourglass_view::drawRunning(gSession, gSim, color, false); break;
        case AppState::Paused:   hourglass_view::drawRunning(gSession, gSim, color, true); break;
        case AppState::Done:     hourglass_view::drawDone(gSession, gSim, color); break;
        case AppState::Finish:   hourglass_view::drawFinish(gSession, gSim, color); break;
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

    // Audio, the way the shipped Cardputer-ADV apps set it up (Enso does the
    // same end/config/begin dance): the speaker M5Cardputer.begin() leaves
    // behind has a small DMA buffer and an unpinned task, and that is the one
    // configuration on this board that has ever panicked when it was asked to
    // play - the defaults here are the ones an ADV audio app runs with.
    M5.Speaker.end();
    {
        auto speakerCfg        = M5.Speaker.config();
        speakerCfg.dma_buf_len = 512;
        speakerCfg.dma_buf_count = 8;
        speakerCfg.task_pinned_core = APP_CPU_NUM;
        M5.Speaker.config(speakerCfg);
    }
    M5.Speaker.begin();
    M5.Speaker.setVolume(kSpeakerVolume);

    canvas::begin();

    gSettings.load();
    app::setInterval(gSession, gSettings.intervalIdx);
    gMenuCursor = gSettings.intervalIdx;

    // An empty cycle, so the grid is ready before the first screen asks for it.
    gSim.reset(app::durationMs(gSession));

    gOrientation.begin(millis());
    gLastOrientation = gOrientation.update(millis());

    Serial.printf("\nHourglass stage 3\n");
    const esp_reset_reason_t bootReason = esp_reset_reason();
    hgLog("reset    : %s (%d)", resetReasonName(bootReason), static_cast<int>(bootReason));
    snprintf(gBootStatus, sizeof(gBootStatus), "last boot: %s (%d)", resetReasonName(bootReason),
             static_cast<int>(bootReason));
    Serial.printf("board    : %s\n", boardName(M5.getBoard()));
    Serial.printf("display  : %dx%d landscape, %dx%d portrait\n", kLandscapeWidth,
                  kLandscapeHeight, kPortraitWidth, kPortraitHeight);
    Serial.printf("keyboard : %s\n",
                  M5.getBoard() == m5::board_t::board_M5CardputerADV ? "TCA8418 (ADV)"
                                                                     : "io matrix");
    hgLog("imu      : %s", M5.Imu.isEnabled() ? "enabled" : "not detected");
    hgLog("sprites  : %s", canvas::usingSprites() ? "landscape + portrait" : "not allocated");
    hgLog("sand     : %d grains on a %dx%d grid, %d bytes in .bss", sand::kGrains,
          sand::kGridWidth, sand::kGridHeight, static_cast<int>(sizeof(sand::Simulation)));
    hgLog("heap     : %lu bytes free", static_cast<unsigned long>(ESP.getFreeHeap()));
    hgLog("settings : interval %u min, color %u (%s), sound %s",
          static_cast<unsigned>(kIntervalMinutes[gSettings.intervalIdx]),
          static_cast<unsigned>(gSettings.colorIdx), paletteName(gSettings.colorIdx),
          gSettings.soundOn ? "on" : "off");

    gLastTickMs   = millis();
    gLastRenderMs = gLastTickMs;
    gLastSandMs   = gLastTickMs;
    gDirty        = true;
}

void loop()
{
    M5Cardputer.update();

    const uint32_t now    = millis();
    const uint32_t dtMs   = now - gLastTickMs;
    const AppState before = gSession.state;
    gLastTickMs           = now;

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

        const AppState    stateBefore = gSession.state;
        const Orientation sideBefore  = gSession.shownSide;
        app::onOrientation(gSession, orientation);

        if (gSession.state == AppState::Running &&
            (stateBefore == AppState::Armed || stateBefore == AppState::Done)) {
            // The placement that starts a cycle: fresh sand in the upper bulb.
            gSim.reset(app::durationMs(gSession));
            gLastSandMs = now;
            hgLog("sand: cycle reset, %d grains above the neck", sand::kGrains);
        } else if (gSession.shownSide != sideBefore) {
            flipSand();
        }

        gDirty = true;
    }

    app::update(gSession, dtMs);

    if (gSession.state != before) {
        gDirty = true;
        if (gSession.state == AppState::Done) {
            startDoneChime(now);
        }
    }

    // The sand steps with the frame rate. Once the pile has come to rest - in
    // PAUSED or DONE - stepping stops, so a still hourglass costs nothing.
    const bool sandShown = gSession.state == AppState::Running ||
                           gSession.state == AppState::Paused ||
                           gSession.state == AppState::Done ||
                           gSession.state == AppState::Finish;
    if (sandShown && (gSession.state == AppState::Running || !gSim.settled()) &&
        now - gLastSandMs >= kSandStepMs) {
        uint32_t stepMs = now - gLastSandMs;
        if (stepMs > kMaxSandStepMs) {
            stepMs = kMaxSandStepMs;
        }
        gLastSandMs = now;

        const bool     flowing = gSession.state == AppState::Running && gSession.flow != 0;
        const uint32_t begin   = micros();
        gSim.step(stepMs, app::frameLowerShare(gSession), flowing);
        const uint32_t spent = micros() - begin;

        gSandUsSum += spent;
        if (spent > gSandUsMax) {
            gSandUsMax = spent;
        }
        if (++gSandSteps >= kSandLogSteps) {
            hgLog("sand     : %lu steps, %lu us average, %lu us worst, %d below the neck",
                  static_cast<unsigned long>(gSandSteps),
                  static_cast<unsigned long>(gSandUsSum / gSandSteps),
                  static_cast<unsigned long>(gSandUsMax), gSim.grainsBelow());
            gSandSteps = 0;
            gSandUsSum = 0;
            gSandUsMax = 0;
        }

        gDirty = true;
    } else if (!sandShown) {
        gLastSandMs = now;
    }

    serviceChime(now);

    const bool running = gSession.state == AppState::Running;
    if (gDirty || (running && now - gLastRenderMs >= kRunningRenderMs)) {
        gDirty        = false;
        gLastRenderMs = now;
        render();
    }

    delay(kLoopDelayMs);
}
