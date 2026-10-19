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
#include <cmath>
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

// Master volume. The chime is the only sound the app makes and its level is
// picked against this one (see the chime constants below); 200 of 255 is where
// the stage 2 build stood (it ran at 128) and leaves the chime a good way below
// the codec's rail instead of asking the amplifier for everything it has.
constexpr uint8_t  kSpeakerVolume   = 200;

// The sand advances at the frame rate, not every pass of the loop: the automaton
// is a whole-grid sweep and has nothing to show between two frames.
constexpr uint32_t kSandStepMs = kRunningRenderMs;

// A stall (menu, a long key press) must not make the sand lurch when the picture
// comes back: the governor repairs whatever this drops.
constexpr uint32_t kMaxSandStepMs = 200;

// How often the cost of a sand step is reported on the serial log.
constexpr uint16_t kSandLogSteps = 200;

// Completion signal: two soft sine notes.
//
// A square wave from Speaker::tone is harsh and thin on this speaker, so the
// chime is built as PCM instead: 16 kHz, 8 bit unsigned (silence = 128), mono,
// raised-cosine fades on both edges of every note so there is no click, and a
// gap of silence between them. The buffer is generated once in setup() from the
// constants below - no asset file - and played as a single raw request, which is
// asynchronous: the timer and the sand keep running while it sounds.
//
// 8 bit unsigned is the format Speaker::tone() plays through
// (Speaker_Class.hpp: playRaw(const uint8_t*) hands _play_raw flg_16bit = false),
// so it is the path this device has actually been heard through, and it halves
// the .bss the buffer costs. The library is not why a 16 bit request stayed
// quiet: its 16 bit path decodes and scales the data correctly
// (Speaker_Class.inl: int16_t samples for is_16bit, 8 bit data scaled by 2^8).
// What was wrong was the level this chime asked for - see below.
//
// Level: the speaker here is mono, and the audio task adds the two channels
// together for a mono output (`l += r`, Speaker_Class.inl), so the peak the
// codec receives is TWICE the peak of the buffer. The level used to be 0.70 at
// master volume 255, i.e. 1.4 of full scale: measured through the library's own
// conversion, 31 % of the samples came out clipped at full scale and the codec
// plus the amplifier had to hold that for the whole 480 ms blip. 0.45 keeps the
// mono sum under full scale even at the loudest master volume.
constexpr uint32_t kChimeRate     = 16000;
constexpr uint32_t kChimeNoteMs   = 170;
constexpr uint32_t kChimeGapMs    = 70;
constexpr uint32_t kChimeFadeMs   = 10;
constexpr float    kChimeNoteHz[] = {880.0f, 1174.0f};  // A5, then D6
constexpr float    kChimeLevel    = 0.45f;  // of full scale, before the mono sum
constexpr uint8_t  kChimeQuiet    = 128;    // 8 bit unsigned: mid scale is silence

// Two notes and two gaps (one gap after every note, including the last):
// buildChime() writes exactly this many samples. Sizing this for a single gap is
// what once made the generator write 1120 samples past the end of the buffer.
constexpr int kChimeSamples =
    static_cast<int>(2 * (kChimeNoteMs + kChimeGapMs) * kChimeRate / 1000);

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

// Cost of a sand step, reported every kSandLogSteps steps so the budget can be
// checked from the serial log on the device.
uint32_t gSandSteps   = 0;
uint32_t gSandUsSum   = 0;
uint32_t gSandUsMax   = 0;

// One buffer, filled in setup(): the speaker task reads from it while it plays,
// so it has to stay alive for as long as the chime can sound. 7680 bytes of .bss.
uint8_t gChime[kChimeSamples];

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

void buildChime()
{
    constexpr float kTwoPi = 6.283185307179586f;
    const int       fade   = static_cast<int>(kChimeFadeMs * kChimeRate / 1000);
    int             at     = 0;

    for (float hz : kChimeNoteHz) {
        const int   count = static_cast<int>(kChimeNoteMs * kChimeRate / 1000);
        const float step  = kTwoPi * hz / static_cast<float>(kChimeRate);
        float       phase = 0.0f;

        for (int i = 0; i < count; ++i) {
            // Raised cosine on both edges: the note starts and ends at zero, so
            // the speaker gets no step and the chime has no click.
            float envelope = 1.0f;
            if (i < fade) {
                envelope = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) /
                                                 static_cast<float>(2 * fade));
            } else if (i >= count - fade) {
                envelope = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(count - 1 - i) /
                                                 static_cast<float>(2 * fade));
            }

            // 8 bit unsigned around mid scale: the codec sees the same waveform
            // a 16 bit buffer would give it. The peak is kChimeLevel * 127, and
            // the level is below half of full scale, so the value stays inside
            // [1, 255] with room to spare.
            const float sample = std::sin(phase) * envelope * kChimeLevel * 127.0f;
            gChime[at++] = static_cast<uint8_t>(static_cast<float>(kChimeQuiet) + sample + 0.5f);
            phase += step;
            if (phase >= kTwoPi) {
                phase -= kTwoPi;
            }
        }

        for (int i = 0; i < static_cast<int>(kChimeGapMs * kChimeRate / 1000); ++i) {
            gChime[at++] = kChimeQuiet;
        }
    }
}

void playDoneChime()
{
    if (!gSettings.soundOn) {
        return;
    }
    // Non-blocking: the request is queued and the audio task plays it. 8 bit
    // unsigned, mono, once, on any free channel, replacing whatever plays now.
    const bool queued = M5.Speaker.playRaw(gChime, kChimeSamples, kChimeRate,
                                           /*stereo=*/false, /*repeat=*/1,
                                           /*channel=*/-1, /*stop_current_sound=*/true);

    // One line that answers the open questions of a device session: was the
    // request taken, and is the audio path alive with the chime on the wire? If
    // the device restarts here instead of printing it, the boot log of the next
    // run has to say why (see resetReasonName).
    hgLog("audio    : chime %s, playing %u, task %s, heap %lu",
          queued ? "queued" : "NOT queued",
          static_cast<unsigned>(M5.Speaker.isPlaying()),
          M5.Speaker.isRunning() ? "running" : "stopped",
          static_cast<unsigned long>(ESP.getFreeHeap()));
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
        case AppState::Menu:     ui_menu::drawMenu(gMenuCursor, gSettings); break;
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
    M5.Speaker.setVolume(kSpeakerVolume);

    // The chime buffer has to exist before the first DONE can play it; without
    // this call playRaw() plays the zero-initialised buffer: perfect silence.
    buildChime();

    canvas::begin();

    gSettings.load();
    app::setInterval(gSession, gSettings.intervalIdx);
    gMenuCursor = gSettings.intervalIdx;

    // An empty cycle, so the grid is ready before the first screen asks for it.
    gSim.reset(app::durationMs(gSession));

    gOrientation.begin(millis());
    gLastOrientation = gOrientation.update(millis());

    Serial.printf("\nHourglass stage 3\n");
    hgLog("reset    : %s (%d)", resetReasonName(esp_reset_reason()),
          static_cast<int>(esp_reset_reason()));
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
            playDoneChime();
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

    const bool running = gSession.state == AppState::Running;
    if (gDirty || (running && now - gLastRenderMs >= kRunningRenderMs)) {
        gDirty        = false;
        gLastRenderMs = now;
        render();
    }

    delay(kLoopDelayMs);
}
