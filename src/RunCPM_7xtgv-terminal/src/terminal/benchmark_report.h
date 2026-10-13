#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace terminal_benchmark
{
    // Full uint64 range needs 27 bytes: 20 digits, six commas and the NUL.
    inline void grouped(char* output, uint64_t value)
    {
        char reverse[26];
        size_t length = 0;
        unsigned digits = 0;
        do
        {
            if (digits && digits % 3 == 0)
            {
                reverse[length++] = ',';
            }
            reverse[length++] = char('0' + value % 10);
            value /= 10;
            ++digits;
        } while (value);
        while (length)
        {
            *output++ = reverse[--length];
        }
        *output = 0;
    }

    inline void report(char* output, size_t capacity, uint32_t chars, uint32_t bytes, uint32_t elapsedUs)
    {
        // Decimal k/M units, truncated to three places without floating point.
        const uint64_t charRate = elapsedUs ? uint64_t(chars) * 1000000 / elapsedUs : 0;
        const uint64_t byteRate = elapsedUs ? uint64_t(bytes) * 1000000 / elapsedUs : 0;
        const uint64_t countUnit = chars >= 1000000 ? 1000000 : 1000;
        const uint64_t charUnit = charRate >= 1000000 ? 1000000 : 1000;
        const uint64_t byteUnit = byteRate >= 1000000 ? 1000000 : 1000;
        // Counts and scaled whole rates need at most 13 grouped characters.
        char count[14], charWhole[14], byteWhole[14];
        grouped(count, chars / countUnit);
        grouped(charWhole, charRate / charUnit);
        grouped(byteWhole, byteRate / byteUnit);
        snprintf(output, capacity,
                 "VTTERM native: %s.%03u %cChars in %lu ms\r\n  = %s.%03u %cChars/s (%s.%03u %cB/s)\r\n", count,
                 unsigned(chars % countUnit * 1000 / countUnit), countUnit == 1000000 ? 'M' : 'k',
                 (unsigned long)(elapsedUs / 1000), charWhole, unsigned(charRate % charUnit * 1000 / charUnit),
                 charUnit == 1000000 ? 'M' : 'k', byteWhole, unsigned(byteRate % byteUnit * 1000 / byteUnit),
                 byteUnit == 1000000 ? 'M' : 'k');
    }
}
