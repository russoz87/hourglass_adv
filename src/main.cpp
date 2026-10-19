/*
 * Hourglass - stage 1 bring-up firmware for the M5Stack Cardputer-ADV.
 *
 * This stage only proves that the hardware and the toolchain work on the real
 * device: display, keyboard (TCA8418 over I2C on the ADV) and IMU (BMI270).
 * The timer, the falling-sand animation and the settings UI come in later
 * stages, so nothing here is written as if it were the final application.
 *
 * Screen (landscape, 240x135): the detected board, the last key event, the
 * raw accelerometer/gyroscope values, the IMU sample counter and the uptime.
 * The same values are printed on the USB serial port at 115200 baud.
 *
 * Build:   pio run
 * Install: copy .pio/build/cardputer-adv/firmware.bin to a microSD card and
 *          install it from the Launcher SD menu - never flash over USB, that
 *          would overwrite the installed Launcher.
 */

#include <M5Cardputer.h>

namespace {

// ------------------------------------------------------------------- timing

constexpr uint32_t kSerialBaud     = 115200;
constexpr uint32_t kRenderPeriodMs = 100;  // ~10 screen updates per second
constexpr uint32_t kImuLogPeriodMs = 500;  // serial IMU trace, twice per second

// ------------------------------------------------------------------ display

constexpr int kScreenWidth  = 240;  // landscape frame: Display.setRotation(1)
constexpr int kScreenHeight = 135;

constexpr uint16_t kColorBackground = TFT_BLACK;
constexpr uint16_t kColorTitle      = TFT_CYAN;
constexpr uint16_t kColorLabel      = TFT_LIGHTGREY;
constexpr uint16_t kColorValue      = TFT_WHITE;
constexpr uint16_t kColorHighlight  = TFT_GOLD;
constexpr uint16_t kColorRule       = TFT_DARKGREY;
constexpr uint16_t kColorError      = TFT_RED;

// Left edge of every row, and the column the values start at.
constexpr int kRowLeft   = 6;
constexpr int kValueLeft = 72;

// --------------------------------------------------------------------- state

// The screen is composed in an off-screen buffer to avoid flicker. If the
// sprite cannot be allocated (it needs ~64 kB) drawing falls back to the
// display itself; both are LovyanGFX surfaces, so the drawing code is shared.
LGFX_Sprite gCanvas(&M5Cardputer.Display);
lgfx::LovyanGFX* gTarget = &M5Cardputer.Display;

String   gLastKey    = "-";
String   gLastMods   = "-";
uint32_t gKeyEvents  = 0;

uint32_t gImuSamples = 0;

uint32_t gLastRenderMs = 0;
uint32_t gLastImuLogMs = 0;

// ------------------------------------------------------------------ helpers

// Human readable name for the board M5GFX detected. On the ADV this is the
// value that selects the TCA8418 keyboard reader, so it is worth showing.
const char* boardName(m5::board_t board)
{
    switch (board) {
        case m5::board_t::board_M5CardputerADV: return "M5Cardputer-ADV";
        case m5::board_t::board_M5Cardputer:    return "M5Cardputer";
        default:                                return "unknown";
    }
}

// Label for one character of Keyboard.keysState().word. That vector holds the
// raw key-map values, so control keys arrive as their KEY_* codes.
String keyLabel(char raw)
{
    const uint8_t code = static_cast<uint8_t>(raw);

    switch (code) {
        case KEY_BACKSPACE:  return "BACKSPACE";
        case KEY_TAB:        return "TAB";
        case KEY_ENTER:      return "ENTER";
        case KEY_LEFT_CTRL:  return "CTRL";
        case KEY_LEFT_SHIFT: return "SHIFT";
        case KEY_LEFT_ALT:   return "ALT";
        case KEY_OPT:        return "OPT";
        case KEY_FN:         return "FN";
        case ' ':            return "SPACE";
        default:             break;
    }

    if (code >= 0x20 && code < 0x7F) {
        return String(static_cast<char>(code));
    }

    char buffer[8];
    snprintf(buffer, sizeof(buffer), "0x%02X", code);
    return String(buffer);
}

void appendModifier(String& mods, const char* name)
{
    if (mods.length() > 0) {
        mods += '+';
    }
    mods += name;
}

// ------------------------------------------------------------------- polling

void pollKeyboard()
{
    auto& keyboard = M5Cardputer.Keyboard;

    // isChange() reports a change of the list of held keys; a release is a
    // change too, and then we keep the last press on screen.
    if (!keyboard.isChange() || !keyboard.isPressed()) {
        return;
    }

    const auto& state = keyboard.keysState();
    ++gKeyEvents;

    String keys;
    for (char raw : state.word) {
        if (keys.length() > 0) {
            keys += '+';
        }
        keys += keyLabel(raw);
    }
    gLastKey = keys.length() > 0 ? keys : "(no character)";

    String mods;
    if (state.fn)    appendModifier(mods, "FN");
    if (state.shift) appendModifier(mods, "SHIFT");
    if (state.ctrl)  appendModifier(mods, "CTRL");
    if (state.opt)   appendModifier(mods, "OPT");
    if (state.alt)   appendModifier(mods, "ALT");
    gLastMods = mods.length() > 0 ? mods : "-";

    Serial.printf("[key] %s   mods: %s   held: %u   events: %lu\n",
                  gLastKey.c_str(), gLastMods.c_str(),
                  static_cast<unsigned>(keyboard.isPressed()),
                  static_cast<unsigned long>(gKeyEvents));
}

void pollImu()
{
    // update() returns the mask of sensors that produced a fresh sample, so a
    // non-zero mask is exactly one new reading.
    if (M5.Imu.update() == m5::IMU_Class::sensor_mask_none) {
        return;
    }

    ++gImuSamples;

    if (millis() - gLastImuLogMs < kImuLogPeriodMs) {
        return;
    }
    gLastImuLogMs = millis();

    const auto& data = M5.Imu.getImuData();
    Serial.printf("[imu] accel [g] %+6.3f %+6.3f %+6.3f   gyro [dps] %+7.2f %+7.2f %+7.2f\n",
                  data.accel.x, data.accel.y, data.accel.z,
                  data.gyro.x, data.gyro.y, data.gyro.z);
}

// ------------------------------------------------------------------ drawing

void render()
{
    auto& g = *gTarget;
    const auto& imu = M5.Imu.getImuData();

    g.fillScreen(kColorBackground);
    g.setTextWrap(false);

    g.setTextSize(2);
    g.setTextColor(kColorTitle);
    g.setCursor(kRowLeft, 4);
    g.print("Hourglass bring-up");

    g.drawFastHLine(kRowLeft, 26, kScreenWidth - 2 * kRowLeft, kColorRule);

    g.setTextSize(1);

    // Board detection drives the keyboard reader and the display setup, so it
    // is the first thing to check when something looks wrong.
    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 32);
    g.print("Board  :");
    g.setTextColor(kColorValue);
    g.setCursor(kValueLeft, 32);
    g.print(boardName(M5.getBoard()));

    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 44);
    g.print("Key    :");
    g.setTextColor(kColorHighlight);
    g.setCursor(kValueLeft, 44);
    g.print(gLastKey);

    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 56);
    g.print("Mods   :");
    g.setTextColor(kColorValue);
    g.setCursor(kValueLeft, 56);
    g.print(gLastMods);

    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 68);
    g.print("Events :");
    g.setTextColor(kColorValue);
    g.setCursor(kValueLeft, 68);
    g.printf("%lu  (held %u)", static_cast<unsigned long>(gKeyEvents),
             static_cast<unsigned>(M5Cardputer.Keyboard.isPressed()));

    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 80);
    g.print("Accel g:");
    g.setTextColor(kColorValue);
    g.setCursor(kValueLeft, 80);
    g.printf("%+7.3f %+7.3f %+7.3f", imu.accel.x, imu.accel.y, imu.accel.z);

    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 92);
    g.print("Gyro dps:");
    g.setTextColor(kColorValue);
    g.setCursor(kValueLeft, 92);
    g.printf("%+7.2f %+7.2f %+7.2f", imu.gyro.x, imu.gyro.y, imu.gyro.z);

    const bool imuEnabled = M5.Imu.isEnabled();
    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 104);
    g.print("IMU    :");
    g.setTextColor(imuEnabled ? kColorValue : kColorError);
    g.setCursor(kValueLeft, 104);
    if (imuEnabled) {
        g.printf("enabled, %lu samples", static_cast<unsigned long>(gImuSamples));
    } else {
        g.print("not detected");
    }

    g.setTextColor(kColorLabel);
    g.setCursor(kRowLeft, 116);
    g.print("Uptime :");
    g.setTextColor(kColorValue);
    g.setCursor(kValueLeft, 116);
    g.printf("%lu.%01lu s", static_cast<unsigned long>(millis() / 1000),
             static_cast<unsigned long>((millis() / 100) % 10));

    if (gTarget == &gCanvas) {
        gCanvas.pushSprite(0, 0);
    }
}

}  // namespace

void setup()
{
    Serial.begin(kSerialBaud);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);  // true: initialise the keyboard

    M5Cardputer.Display.setRotation(1);  // landscape, 240x135
    M5Cardputer.Display.setBrightness(128);

    gCanvas.setColorDepth(16);
    if (gCanvas.createSprite(kScreenWidth, kScreenHeight) != nullptr) {
        gTarget = &gCanvas;
    } else {
        Serial.println("[ui] sprite allocation failed, drawing directly");
    }

    Serial.printf("\nHourglass bring-up\n");
    Serial.printf("board         : %s\n", boardName(M5.getBoard()));
    Serial.printf("display       : %dx%d, rotation %u\n",
                  static_cast<int>(M5Cardputer.Display.width()),
                  static_cast<int>(M5Cardputer.Display.height()),
                  static_cast<unsigned>(M5Cardputer.Display.getRotation()));
    Serial.printf("keyboard      : %s\n",
                  M5.getBoard() == m5::board_t::board_M5CardputerADV ? "TCA8418 (ADV)" : "io matrix");
    Serial.printf("IMU           : %s\n", M5.Imu.isEnabled() ? "enabled" : "not detected");
    Serial.printf("serial        : %lu baud\n\n", static_cast<unsigned long>(kSerialBaud));

    render();
}

void loop()
{
    M5Cardputer.update();  // refreshes buttons, keyboard and power state

    pollKeyboard();
    pollImu();

    const uint32_t now = millis();
    if (now - gLastRenderMs >= kRenderPeriodMs) {
        gLastRenderMs = now;
        render();
    }

    delay(10);
}
