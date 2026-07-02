// engine/src/core/Log.cpp
#include "forge/core/Log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

#if defined(_WIN32)
#include <io.h>
#define FORGE_ISATTY(f) (_isatty(_fileno(f)) != 0)
#else
#include <unistd.h>
#define FORGE_ISATTY(f) (isatty(fileno(f)) != 0)
#endif

namespace forge::log {

namespace {

#ifdef NDEBUG
std::atomic<Level> s_minLevel{Level::Info};
#else
std::atomic<Level> s_minLevel{Level::Trace};
#endif

// One mutex around the actual write: interleaved half-lines from two threads
// are worse than the tiny cost of serializing output.
std::mutex s_writeMutex;

struct LevelStyle {
    const char* label;
    const char* color; // ANSI; empty string when colors are off
};

constexpr LevelStyle kStyles[] = {
    {"TRACE", "\x1b[90m"}, // bright black (dim)
    {"INFO ", "\x1b[32m"}, // green
    {"WARN ", "\x1b[33m"}, // yellow
    {"ERROR", "\x1b[31m"}, // red
};

bool colorsEnabled(std::FILE* stream) {
    // Cached per-stream on first use; VT sequences work on any modern
    // terminal incl. Windows Terminal. Redirected output gets plain text.
    static const bool stdoutTty = FORGE_ISATTY(stdout);
    static const bool stderrTty = FORGE_ISATTY(stderr);
    return (stream == stderr) ? stderrTty : stdoutTty;
}

} // namespace

void setMinLevel(Level level) { s_minLevel.store(level, std::memory_order_relaxed); }
Level minLevel() { return s_minLevel.load(std::memory_order_relaxed); }

void write(Level level, std::string_view message) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto timeOfDay = now - floor<days>(now);
    const auto h = duration_cast<hours>(timeOfDay);
    const auto m = duration_cast<minutes>(timeOfDay - h);
    const auto s = duration_cast<seconds>(timeOfDay - h - m);
    const auto ms = duration_cast<milliseconds>(timeOfDay - h - m - s);

    std::FILE* out = (level >= Level::Warn) ? stderr : stdout;
    const auto& style = kStyles[static_cast<size_t>(level)];
    const bool color = colorsEnabled(out);

    const std::lock_guard<std::mutex> lock(s_writeMutex);
    std::fprintf(out, "%s[%02d:%02d:%02d.%03d][FORGE][%s]%s %.*s\n", color ? style.color : "",
                 static_cast<int>(h.count() % 24), static_cast<int>(m.count()),
                 static_cast<int>(s.count()), static_cast<int>(ms.count()), style.label,
                 color ? "\x1b[0m" : "", static_cast<int>(message.size()), message.data());
}

} // namespace forge::log
