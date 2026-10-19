/*
 * One place for the serial log format.
 *
 * Every line starts with the uptime in milliseconds, so a session on the device
 * can be lined up with what happened (state changes, orientation changes, key
 * presses, NVS reads). 115200 baud, set in platformio.ini.
 */
#pragma once

// printf-style log line on the USB serial port. Adds the "[hg <uptime>]" prefix
// and the trailing newline; use Serial.printf directly for the boot banner.
void hgLog(const char* format, ...);
