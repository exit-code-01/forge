// engine/include/forge/core/Log.hpp
//
// Same call-site contract since P0: FORGE_INFO("x = {}", x). The backend has
// grown up (timestamps, colors, runtime filtering, thread safety) without a
// single call site changing — that's what the write() choke point bought us.

#pragma once

#include <format>
#include <string_view>

namespace forge::log {

enum class Level { Trace = 0, Info = 1, Warn = 2, Error = 3 };

// Runtime filter. Messages below this level cost one branch and nothing else
// (checked BEFORE std::format runs). Defaults: Trace in Debug, Info otherwise.
void setMinLevel(Level level);
[[nodiscard]] Level minLevel();

// The choke point. Thread-safe.
void write(Level level, std::string_view message);

template <typename... Args>
void message(Level level, std::format_string<Args...> fmt, Args&&... args) {
    if (level < minLevel()) {
        return;
    } // early-out: skip formatting entirely
    write(level, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace forge::log

#define FORGE_TRACE(...) ::forge::log::message(::forge::log::Level::Trace, __VA_ARGS__)
#define FORGE_INFO(...) ::forge::log::message(::forge::log::Level::Info, __VA_ARGS__)
#define FORGE_WARN(...) ::forge::log::message(::forge::log::Level::Warn, __VA_ARGS__)
#define FORGE_ERROR(...) ::forge::log::message(::forge::log::Level::Error, __VA_ARGS__)
