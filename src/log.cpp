#include "log.h"

#include <Arduino.h>
#include <cstdarg>

void hgLog(const char* format, ...)
{
    Serial.printf("[hg %7lu] ", static_cast<unsigned long>(millis()));

    va_list args;
    va_start(args, format);
    Serial.vprintf(format, args);
    va_end(args);

    Serial.print('\n');
}
