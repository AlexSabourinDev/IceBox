#pragma once

#include "IBEngineAPI.h"

#define IB_ENABLE_LOGGING
#define IB_ENABLE_ASSERTS

#ifdef IB_ENABLE_LOGGING
#define IB_LOG(level, category, message) IB::log(level, category, message)
#else
#define IB_LOG(level, category, message)
#endif // IB_ENABLE_LOGGING

#ifdef IB_ENABLE_ASSERTS
#define IB_ASSERT(condition, message) IB::assert(condition, message)
#else
#define IB_ASSERT(condition, message)
#endif // IB_ENABLE_ASSERTS

namespace IB
{
    enum class LogLevel
    {
        Log = 0,
        Warn,
        Error,
        Count
    };

    IB_API void log(LogLevel level, char const* category, char const* message);
    IB_API void assert(bool condition, char const* message);
}
