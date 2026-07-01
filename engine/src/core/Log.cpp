// engine/src/core/Log.cpp
#include "forge/core/Log.hpp"

#include <cstdio>

namespace forge::log {

namespace {
constexpr const char* label(Level level) {
    switch (level) {
    case Level::Trace:
        return "TRACE";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    }
    return "?????";
}
} // namespace

void write(Level level, std::string_view message) {
    // stderr for Warn/Error so they survive stdout redirection.
    std::FILE* out = (level >= Level::Warn) ? stderr : stdout;
    std::fprintf(out, "[FORGE][%s] %.*s\n", label(level), static_cast<int>(message.size()),
                 message.data());
}

} // namespace forge::log
