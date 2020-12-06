#include "IBLogging.h"

#include "IBPlatform.h"

#include <stdio.h>
#include <stdint.h>

namespace
{
    char const* LogLevelString[] =
    {
        "Log",
        "Warn",
        "Error"
    };
    static_assert(sizeof(LogLevelString) / sizeof(LogLevelString[0]) == static_cast<uint32_t>(IB::LogLevel::Count), "Log level string table doesn't match enum size. Are we missing an array element?");
}

namespace IB
{
    void log(LogLevel level, char const* category, char const* message)
    {
        printf("[%s][%s] %s\n", LogLevelString[static_cast<uint32_t>(level)], category, message);
    }

    void assert(bool condition, char const* message)
    {
        if (!condition)
        {
            IB::log(LogLevel::Error, "Assert", message);
            IB::debugBreak();
        }
    }
}

