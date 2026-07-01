// engine/include/forge/core/Log.hpp
//
// P0 logging STUB. Deliberately tiny: enough to debug the build system and
// give sandbox something real to call. P1 replaces the backend (file sink,
// categories, timestamps) WITHOUT changing this call-site API:
//
//     FORGE_INFO("swapchain created: {}x{}", w, h);
//
// That's the contract: macros + std::format syntax stay stable forever;
// what happens behind them is allowed to evolve.

#pragma once

#include <format>
#include <string_view>

namespace forge::log {

enum class Level { Trace, Info, Warn, Error };

// The single choke point. All macros funnel here; P1 swaps this body out.
void write(Level level, std::string_view message);

template <typename... Args>
void message(Level level, std::format_string<Args...> fmt, Args&&... args) {
    write(level, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace forge::log

// Macros so we can compile logging out of shipping builds later (P10)
// and auto-capture file/line when the P1 logger wants it.
#define FORGE_TRACE(...) ::forge::log::message(::forge::log::Level::Trace, __VA_ARGS__)
#define FORGE_INFO(...) ::forge::log::message(::forge::log::Level::Info, __VA_ARGS__)
#define FORGE_WARN(...) ::forge::log::message(::forge::log::Level::Warn, __VA_ARGS__)
#define FORGE_ERROR(...) ::forge::log::message(::forge::log::Level::Error, __VA_ARGS__)
