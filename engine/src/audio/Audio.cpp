// engine/src/audio/Audio.cpp — the ONLY TU that includes miniaudio.
#include "forge/audio/Audio.hpp"
#include "forge/core/Log.hpp"

// miniaudio's implementation compiles inside this TU; shield it from our
// warning wall (its warnings are not our problem — ADR-016 precedent).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4100 4456 4457 4701 4702)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING // we play sounds, we don't write files
#include <miniaudio.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace forge {

struct Audio::Impl {
    ma_engine engine{};
    bool ok = false;
};

Audio::Audio() : m_impl(std::make_unique<Impl>()) {
    const ma_result result = ma_engine_init(nullptr, &m_impl->engine);
    if (result == MA_SUCCESS) {
        m_impl->ok = true;
        FORGE_INFO("audio: miniaudio engine up ({} Hz)",
                   ma_engine_get_sample_rate(&m_impl->engine));
    } else {
        // Headless/CI or exotic hardware: sound off, engine alive.
        FORGE_WARN("audio: no usable device (miniaudio result {}), sound disabled",
                   static_cast<int>(result));
    }
}

Audio::~Audio() {
    if (m_impl->ok) {
        ma_engine_uninit(&m_impl->engine);
    }
}

bool Audio::available() const { return m_impl->ok; }

void Audio::play(const std::string& path, float volume) {
    if (!m_impl->ok) {
        return;
    }
    // Engine-level volume is the lean-P8 knob; VAULT's audio pass brings a
    // real mixer with per-voice control (ADR-020).
    ma_engine_set_volume(&m_impl->engine, volume);
    // Fire-and-forget: miniaudio manages the voice; overlapping plays mix.
    const ma_result result = ma_engine_play_sound(&m_impl->engine, path.c_str(), nullptr);
    if (result != MA_SUCCESS) {
        FORGE_WARN("audio: failed to play {} (result {})", path, static_cast<int>(result));
    }
}

} // namespace forge
