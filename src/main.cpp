/*
 * Hourglass - sand-timer firmware for the M5Stack Cardputer-ADV.
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
 * One debug path keeps the app usable while the IMU pose is off: in
 * RUNNING/PAUSED the up/down keys move progress by 10 % per press. Start the
 * cycle on an edge once, then a flat device pauses itself - that is what flat
 * means - and the arrow keys still move the sand, which is enough to walk
 * through all the screens without an IMU.
 *
 * Build:   pio run
 * Install: copy build/hourglass.bin to a microSD card and
 *          install it from the Launcher SD menu - never flash over USB, that
 *          would overwrite the installed Launcher.
 */

#include <M5Cardputer.h>
#include <cmath>
#include <cstring>
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
#include "version.h"

namespace {

constexpr uint32_t kSerialBaud      = 115200;
constexpr uint32_t kLoopDelayMs     = 5;
constexpr uint32_t kRunningRenderMs = 50;   // 20 frames per second while running
constexpr uint8_t  kBrightness      = 128;  // the app is watched for minutes

// Speaker rate: the speaker task plays what it was configured for, and
// playRaw() takes the rate explicitly - the two must agree, so both read one
// constant. AdvanceOS uses 96000.
constexpr uint32_t kSpeakerSampleRate = 96000;

// Master volume. The completion chime renders its own envelope and sits around
// a fifth of the channel range, so the master no longer has to hug the rail -
// 128 keeps the sum away from it, and the render controls the loudness.
constexpr uint8_t  kSpeakerVolume   = 128;

// The sand advances at the frame rate, not every pass of the loop: the automaton
// is a whole-grid sweep and has nothing to show between two frames.
constexpr uint32_t kSandStepMs = kRunningRenderMs;

// A stall (menu, a long key press) must not make the sand lurch when the picture
// comes back: the governor repairs whatever this drops.
constexpr uint32_t kMaxSandStepMs = 200;

// Completion chime: the same two notes the tone() figure played (A5, then a
// longer D6 - a fourth apart), rendered once at boot into soft PCM and played
// in one go through Speaker::playRaw(). The envelope a square wave cannot
// have is the whole point: every note is a sine with a fade in and out.
//
// Every playRaw() failure ever recorded on this device - silent at low
// levels, reset at high ones - happened under the espressif32@8.0.0 speaker
// stack, the one that also reset tone(). The pinned generation plays this
// chime audibly - confirmed on the device - and played tone() before it. If
// the chime ever goes silent or resets the board, the version pins are the
// first suspect; Speaker::tone() in the git history is the proven fallback.
constexpr float    kChimeNoteHz[]  = {880.0f, 1174.66f};  // A5, then a longer D6
constexpr uint32_t kChimeNoteMs[]  = {150, 300};
constexpr uint8_t  kChimeNoteCount = 2;
constexpr uint32_t kChimeGapMs     = 50;
constexpr float    kChimeAmplitude  = 48.0f;  // ~19% of the range: soft, clear of the rails
constexpr uint32_t kChimeFadeMs    = 15;     // per note edge: the softness itself
constexpr size_t   kChimeSamples =
    (kChimeNoteMs[0] + kChimeGapMs + kChimeNoteMs[1]) * kSpeakerSampleRate / 1000;

// Unsigned 8-bit, the WAV convention: silence is the 128 centre line. Filled
// once at setup; the speaker task streams it with nothing running in the loop.
alignas(4) uint8_t gChimeData[kChimeSamples];

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

// Static screens are redrawn when they change, the running countdown on a fixed
// interval: an idle screen must not cost anything.
bool     gDirty        = true;
uint32_t gLastRenderMs = 0;
uint32_t gLastTickMs   = 0;
uint32_t gLastSandMs   = 0;

// Why the device started this time. A reset that happens while the chime plays
// shows up here in the serial log on the next boot: `brownout` is the supply
// sagging under the amplifier, `panic` an exception (with a Guru Meditation
// line above it).
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

// Renders the chime once at boot: a sine per note with linear fades at both
// edges, silence in the gap. The whole chime is one buffer, so the speaker
// task streams it with nothing left to generate at play time.
void renderChime()
{
    std::memset(gChimeData, 128, sizeof(gChimeData));

    size_t offset = 0;
    for (uint8_t note = 0; note < kChimeNoteCount; ++note) {
        const uint32_t length = kChimeNoteMs[note] * kSpeakerSampleRate / 1000;
        const uint32_t fade   = kChimeFadeMs * kSpeakerSampleRate / 1000;
        for (uint32_t i = 0; i < length; ++i) {
            const float phase =
                2.0f * 3.14159265f * kChimeNoteHz[note] * static_cast<float>(i) /
                static_cast<float>(kSpeakerSampleRate);
            float envelope = 1.0f;
            if (i < fade) {
                envelope = static_cast<float>(i) / static_cast<float>(fade);
            } else if (i > length - fade) {
                envelope = static_cast<float>(length - i) / static_cast<float>(fade);
            }
            const float value = 128.0f + kChimeAmplitude * std::sin(phase) * envelope;
            gChimeData[offset + i] = static_cast<uint8_t>(value + 0.5f);
        }
        offset += length;
        if (note + 1 < kChimeNoteCount) {
            offset += kChimeGapMs * kSpeakerSampleRate / 1000;  // the gap stays silent
        }
    }
}

// Plays the chime on the speaker's virtual channel 0: one call, no scheduler
// left in the loop. A retrigger (two cycles ending close together) replaces
// whatever is sounding.
void playDoneChime()
{
    if (!gSettings.soundOn) {
        return;
    }
    M5.Speaker.playRaw(gChimeData, sizeof(gChimeData), kSpeakerSampleRate,
                       /*stereo=*/false, /*repeat=*/1, /*channel=*/0,
                       /*stop_current_sound=*/true);
}

// Turns the switch around. Switching sound on plays the signal at once: that is
// what the switch enables, and it also makes the audio path checkable on the
// device without waiting out a whole interval.
void toggleSound()
{
    gSettings.setSound(!gSettings.soundOn);
    gDirty = true;
    if (gSettings.soundOn) {
        playDoneChime();
    }
}

// Shows or hides the mm:ss readout during the countdown. No preview needed: the
// next countdown screen is the feedback.
void toggleClock()
{
    gSettings.setClock(!gSettings.clockOn);
    gDirty = true;
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
            const bool onClockRow = gSettingsCursor.onClock();

            if (key.up || key.down) {
                const int dy    = (key.down ? 1 : 0) - (key.up ? 1 : 0);
                gSettingsCursor = ui_menu::moveCursor(gSettingsCursor, 0, dy);
                if (gSettingsCursor.onPalette()) {
                    // Moving onto a swatch selects it: the preview is the
                    // confirmation and the choice goes to NVS right away.
                    gSettings.setColor(gSettingsCursor.paletteIdx());
                }
                gDirty = true;
            }

            if (key.left || key.right) {
                if (onSoundRow) {
                    toggleSound();
                } else if (onClockRow) {
                    toggleClock();
                } else {
                    const int dx    = (key.right ? 1 : 0) - (key.left ? 1 : 0);
                    gSettingsCursor = ui_menu::moveCursor(gSettingsCursor, dx, 0);
                    gSettings.setColor(gSettingsCursor.paletteIdx());
                }
                gDirty = true;
            }

            if ((key.enter || key.ok || key.ch == ' ') && (onSoundRow || onClockRow)) {
                if (onSoundRow) {
                    toggleSound();
                } else {
                    toggleClock();
                }
            }

            if (key.back) {
                app::enter(gSession, AppState::Menu, "settings closed");
                gMenuCursor = ui_menu::kMenuSettingsItem;
            }
            break;
        }

        case AppState::Armed:
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
        case AppState::Menu:     ui_menu::drawMenu(gMenuCursor, gSettings); break;
        case AppState::Settings: ui_menu::drawSettings(gSettingsCursor, gSettings); break;
        case AppState::Armed:    hourglass_view::drawArmed(gSession); break;
        case AppState::Running:  hourglass_view::drawRunning(gSession, gSim, color, false,
                                                             gSettings.clockOn); break;
        case AppState::Paused:   hourglass_view::drawRunning(gSession, gSim, color, true,
                                                             gSettings.clockOn); break;
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

    // Audio, the way AdvanceOS - the shipped Cardputer-ADV app whose sounds
    // are proven on this board - sets it up. It never calls Speaker.end()
    // before the first sound, it just asks for a higher sample rate and
    // starts the speaker once, at boot, so the ES8311 codec and the I2S
    // channel are up long before anything plays. (Its extra M5.begin() with
    // external_speaker flags is a no-op: M5.begin() refuses to run twice, and
    // the Cardputer-ADV setup ignores those flags - so it is not replicated
    // here.) The earlier end/config/begin dance with a bigger DMA buffer is
    // what the diagnostic probe tested; it did not help, and AdvanceOS does
    // not do it.
    {
        auto speakerCfg        = M5.Speaker.config();
        speakerCfg.sample_rate = kSpeakerSampleRate;
        M5.Speaker.config(speakerCfg);
    }
    M5.Speaker.begin();
    M5.Speaker.setVolume(kSpeakerVolume);
    renderChime();

    canvas::begin();

    gSettings.load();
    app::setInterval(gSession, gSettings.intervalIdx);
    gMenuCursor = gSettings.intervalIdx;

    // An empty cycle, so the grid is ready before the first screen asks for it.
    gSim.reset(app::durationMs(gSession));

    gOrientation.begin(millis());
    gLastOrientation = gOrientation.update(millis());

    Serial.printf("\nHourglass v%s\n", HG_VERSION);
    const esp_reset_reason_t bootReason = esp_reset_reason();
    hgLog("reset    : %s (%d)", resetReasonName(bootReason), static_cast<int>(bootReason));
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
    hgLog("settings : interval %u min, color %u (%s), sound %s, clock %s",
          static_cast<unsigned>(kIntervalMinutes[gSettings.intervalIdx]),
          static_cast<unsigned>(gSettings.colorIdx), paletteName(gSettings.colorIdx),
          gSettings.soundOn ? "on" : "off", gSettings.clockOn ? "on" : "off");

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
            playDoneChime();
        }
    }

    // The sand steps with the frame rate. Once the pile has come to rest - in
    // PAUSED or DONE - stepping stops, so a still hourglass costs nothing.
    // "At rest" is per gravity direction: laying the device flat re-arms the
    // settle even when the standing pile had already stopped, which is exactly
    // the pause that starts the lateral slide.
    const bool sandShown = gSession.state == AppState::Running ||
                           gSession.state == AppState::Paused ||
                           gSession.state == AppState::Done ||
                           gSession.state == AppState::Finish;

    // Lying flat is a paused hourglass whose picture keeps its frame: the sand
    // slides across the picture, toward the lower wall of its own bulb. Which
    // of the two side walls that is cannot be read from a flat pose (there the
    // gravity points straight into the screen), so the convention is flat -
    // screen up settles left, screen down settles right, and the tilted/in
    // motion poses keep the vertical physics.
    //
    // On a LONG EDGE the hourglass lies on its side for real: the gravity runs
    // across the picture along the device y axis, and the picture's horizontal
    // axis IS that y axis. The direction is device-verified: the pile must run
    // toward the wall the device rests on, and on the hardware it ran the
    // opposite of the right-hand cross guess from the x/z axis table (the
    // triad's y sign was never pinned down by a measurement), so the mapping
    // below carries the confirmed signs: in a SideA frame LongYUp slides
    // left, in a SideB frame right; LongYDown the other way round.
    sand::Gravity gravity = sand::Gravity::Down;
    if (gSession.state == AppState::Paused) {
        if (gLastOrientation == Orientation::FlatUp) {
            gravity = sand::Gravity::Left;
        } else if (gLastOrientation == Orientation::FlatDown) {
            gravity = sand::Gravity::Right;
        } else if (gLastOrientation == Orientation::LongYUp ||
                   gLastOrientation == Orientation::LongYDown) {
            const bool yUp = gLastOrientation == Orientation::LongYUp;
            if (gSession.shownSide == Orientation::SideB) {
                gravity = yUp ? sand::Gravity::Right : sand::Gravity::Left;
            } else {
                gravity = yUp ? sand::Gravity::Left : sand::Gravity::Right;
            }
        }
    }

    if (sandShown && (gSession.state == AppState::Running || !gSim.settled(gravity)) &&
        now - gLastSandMs >= kSandStepMs) {
        uint32_t stepMs = now - gLastSandMs;
        if (stepMs > kMaxSandStepMs) {
            stepMs = kMaxSandStepMs;
        }
        gLastSandMs = now;

        const bool     flowing = gSession.state == AppState::Running && gSession.flow != 0;

        gSim.step(stepMs, app::frameLowerShare(gSession), flowing, gravity);

        gDirty = true;
    } else if (!sandShown) {
        gLastSandMs = now;
    }

    const bool running = gSession.state == AppState::Running;
    if (gDirty || (running && now - gLastRenderMs >= kRunningRenderMs)) {
        gDirty        = false;
        gLastRenderMs = now;
        render();
    }

    delay(kLoopDelayMs);
}
