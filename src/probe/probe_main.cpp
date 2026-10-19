/*
 * Audio probe firmware for the Cardputer-ADV.
 *
 * A throwaway diagnostic: the app panics when it tries to play anything, the
 * serial port has not been available to catch a backtrace, and the two
 * audio configurations in play (the defaults M5Cardputer.begin() leaves, and
 * the end/config/begin setup that shipped ADV apps use - see Enso) behave
 * differently on this hardware. This firmware answers which combination is at
 * fault without a serial cable and without touching the device:
 *
 *   - it runs one audio experiment per boot and then reboots itself;
 *   - the outcome of each experiment is the reset reason of the NEXT boot,
 *     stored in NVS - a panic in the experiment leaves its slot marked as
 *     running and the next boot records what the reset reason was;
 *   - after the last experiment it shows a table of outcomes and stays there.
 *
 * Install it as a Launcher app, let it run for about a minute, read the table.
 * `Enter` on the report screen clears the results and runs the series again.
 *
 * Build: pio run -e cardputer-adv-probe
 */
#include <M5Cardputer.h>
#include <Preferences.h>
#include <cmath>
#include <cstdio>
#include <esp_system.h>

namespace {

// Result codes live above every esp_reset_reason value (those are 0..14), so a
// stored reset reason can never be confused with one of them.
constexpr uint8_t kUnknown = 0xFF;
constexpr uint8_t kRunning = 0xFE;
constexpr uint8_t kOk      = 0xFD;

constexpr uint8_t kExperimentCount = 6;

const char* kNames[kExperimentCount] = {
    "control (no audio)",
    "default speaker + tone",
    "enso speaker + tone",
    "default speaker + playRaw 8b 16k",
    "enso speaker + playRaw 8b 16k",
    "enso speaker + playRaw 16b 22k",
};

Preferences gPrefs;
uint8_t     gSlot    = 0;
uint8_t     gOutcome[kExperimentCount];

uint8_t gRaw8[7680];   // the chime buffer that panicked first
int16_t gRaw16[512];   // one Enso-sized playback block

void slotKey(uint8_t slot, char* out)
{
    snprintf(out, 4, "r%u", static_cast<unsigned>(slot));
}

void fillBuffers()
{
    constexpr float kTwoPi = 6.2831853f;
    for (int i = 0; i < 7680; ++i) {
        const float s = std::sin(kTwoPi * 440.0f * i / 16000.0f);
        gRaw8[i]      = static_cast<uint8_t>(128.0f + s * 57.0f);
    }
    for (int i = 0; i < 512; ++i) {
        const float s = std::sin(kTwoPi * 440.0f * i / 22050.0f);
        gRaw16[i]     = static_cast<int16_t>(s * 14000.0f);
    }
}

// The speaker setup shipped ADV audio apps use: stop what M5Cardputer.begin()
// left behind, reconfigure, start again (see Enso).
void configureSpeakerEnso()
{
    M5.Speaker.end();
    auto cfg            = M5.Speaker.config();
    cfg.dma_buf_len     = 512;
    cfg.dma_buf_count   = 8;
    cfg.task_pinned_core = APP_CPU_NUM;
    M5.Speaker.config(cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(128);
}

void runExperiment(uint8_t slot)
{
    switch (slot) {
        case 1:
            M5.Speaker.tone(880.0f, 150, 0, true);
            break;
        case 2:
            configureSpeakerEnso();
            M5.Speaker.tone(880.0f, 150, 0, true);
            break;
        case 3:
            M5.Speaker.playRaw(gRaw8, sizeof(gRaw8), 16000, false, 1, 0);
            break;
        case 4:
            configureSpeakerEnso();
            M5.Speaker.playRaw(gRaw8, sizeof(gRaw8), 16000, false, 1, 0);
            break;
        case 5:
            configureSpeakerEnso();
            M5.Speaker.playRaw(gRaw16, 512, 22050, false, 1, 0);
            break;
        default:
            break;  // 0: the control run does nothing
    }
}

const char* outcomeText(uint8_t value)
{
    switch (value) {
        case kUnknown:          return "not run";
        case kRunning:          return "crashed here?";
        case kOk:               return "ok";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_SW:        return "sw reset";
        case ESP_RST_INT_WDT:   return "int watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_POWERON:   return "power-on";
        default:                return "other reset";
    }
}

lgfx::LovyanGFX& screen()
{
    M5Cardputer.Display.setRotation(1);
    return M5Cardputer.Display;
}

void drawStatus(const char* line)
{
    auto& d = screen();
    d.fillScreen(TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_CYAN);
    d.setCursor(6, 6);
    d.print("Audio probe");
    d.setTextSize(1);
    d.setTextColor(TFT_WHITE);
    d.setCursor(6, 40);
    d.print(line);
}

void drawReport()
{
    auto& d = screen();
    d.fillScreen(TFT_BLACK);
    d.setTextSize(2);
    d.setTextColor(TFT_CYAN);
    d.setCursor(6, 4);
    d.print("Results");
    d.setTextSize(1);

    for (uint8_t i = 0; i < kExperimentCount; ++i) {
        const int y = 30 + i * 14;
        d.setTextColor(TFT_LIGHTGREY);
        d.setCursor(6, y);
        d.printf("%u %s", static_cast<unsigned>(i), kNames[i]);
        d.setTextColor(gOutcome[i] == kOk ? TFT_GREEN : TFT_RED);
        d.setCursor(150, y);
        d.print(outcomeText(gOutcome[i]));
    }

    d.setTextColor(TFT_DARKGREY);
    d.setCursor(6, 120);
    d.print("Enter: run the series again");
}

void storeOutcome(uint8_t slot, uint8_t value)
{
    char key[4];
    slotKey(slot, key);
    gPrefs.putUChar(key, value);
    gOutcome[slot] = value;
}

}  // namespace

void setup()
{
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setBrightness(128);

    gPrefs.begin("hgprobe", false);
    gSlot = gPrefs.getUChar("slot", 0);
    for (uint8_t i = 0; i < kExperimentCount; ++i) {
        char key[4];
        slotKey(i, key);
        gOutcome[i] = gPrefs.getUChar(key, kUnknown);
    }

    const esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("\n[probe] boot, slot %u, reset %d\n", static_cast<unsigned>(gSlot),
                  static_cast<int>(reason));

    if (gSlot < kExperimentCount && gOutcome[gSlot] == kRunning) {
        // The previous run of this slot never reported: this boot's reset
        // reason is its outcome (panic, watchdog, or something gentler).
        storeOutcome(gSlot, static_cast<uint8_t>(reason));
        Serial.printf("[probe] slot %u recorded %s\n", static_cast<unsigned>(gSlot),
                      outcomeText(gOutcome[gSlot]));
        ++gSlot;
        gPrefs.putUChar("slot", gSlot);
    }

    if (gSlot >= kExperimentCount) {
        drawReport();
        return;
    }

    char line[64];
    snprintf(line, sizeof(line), "run %u/%u: %s", static_cast<unsigned>(gSlot + 1),
             static_cast<unsigned>(kExperimentCount), kNames[gSlot]);
    drawStatus(line);
    Serial.printf("[probe] %s\n", line);

    storeOutcome(gSlot, kRunning);
    delay(500);
    fillBuffers();

    runExperiment(gSlot);

    // Still alive: the experiment is fine, move on to the next one.
    delay(3000);
    storeOutcome(gSlot, kOk);
    ++gSlot;
    gPrefs.putUChar("slot", gSlot);
    Serial.printf("[probe] slot ok, rebooting\n");
    esp_restart();
}

void loop()
{
    M5Cardputer.update();

    if (gSlot >= kExperimentCount && M5Cardputer.Keyboard.isChange() &&
        M5Cardputer.Keyboard.isPressed()) {
        const auto& state = M5Cardputer.Keyboard.keysState();
        for (char raw : state.word) {
            if (raw == KEY_ENTER) {
                gPrefs.clear();
                gPrefs.putUChar("slot", 0);
                esp_restart();
            }
        }
    }

    delay(20);
}