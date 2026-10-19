#include "log.h"

#include <Arduino.h>
#include <cstdarg>

void hgLog(const char* format, ...)
{
    Serial.printf("[hg %7lu] ", static_cast<unsigned long>(millis()));

    // HWCDC (the USB CDC Serial of arduino-esp32 2.x) has no vprintf, so
    // format into a buffer and print that; every log line is far under 256
    // characters.
    char    message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    Serial.print(message);
    Serial.print('\n');
}
