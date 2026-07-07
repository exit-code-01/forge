// engine/src/audio/Audio.cpp — the ONLY TU that includes miniaudio.
#include "forge/audio/Audio.hpp"
#include "forge/core/Log.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

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
    // One-shots: owned until they finish, then reaped by update(). Owning by
    // unique_ptr keeps each ma_sound at a stable address (miniaudio stores
    // internal pointers into the voice, so it must not move).
    std::vector<std::unique_ptr<ma_sound>> oneShots;
    // Looping voices keyed by handle (ambient/music). Persist until stop().
    std::unordered_map<uint32_t, std::unique_ptr<ma_sound>> loops;
    uint32_t nextHandle = 1; // 0 stays reserved for the null handle
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
    if (!m_impl->ok) {
        return;
    }
    // Voices must die before the engine that owns their data path.
    for (auto& s : m_impl->oneShots) {
        ma_sound_uninit(s.get());
    }
    for (auto& [id, s] : m_impl->loops) {
        ma_sound_uninit(s.get());
    }
    ma_engine_uninit(&m_impl->engine);
}

bool Audio::available() const { return m_impl->ok; }

void Audio::setMasterVolume(float volume) {
    if (m_impl->ok) {
        ma_engine_set_volume(&m_impl->engine, volume);
    }
}

void Audio::play(const std::string& path, float volume) {
    if (!m_impl->ok) {
        return;
    }
    // A self-owned voice with its OWN volume — overlapping plays mix, and one
    // quiet SFX no longer drags the whole mix down (the old master-volume bug).
    auto snd = std::make_unique<ma_sound>();
    const ma_result r =
        ma_sound_init_from_file(&m_impl->engine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr,
                                nullptr, snd.get());
    if (r != MA_SUCCESS) {
        FORGE_WARN("audio: failed to load {} (result {})", path, static_cast<int>(r));
        return;
    }
    ma_sound_set_volume(snd.get(), volume);
    ma_sound_start(snd.get());
    m_impl->oneShots.push_back(std::move(snd));
}

SoundHandle Audio::playLoop(const std::string& path, float volume) {
    if (!m_impl->ok) {
        return {};
    }
    auto snd = std::make_unique<ma_sound>();
    // STREAM: ambient/music are long; don't decode minutes of audio into RAM.
    const ma_result r =
        ma_sound_init_from_file(&m_impl->engine, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr,
                                nullptr, snd.get());
    if (r != MA_SUCCESS) {
        FORGE_WARN("audio: failed to load loop {} (result {})", path, static_cast<int>(r));
        return {};
    }
    ma_sound_set_looping(snd.get(), MA_TRUE);
    ma_sound_set_volume(snd.get(), volume);
    ma_sound_start(snd.get());
    const uint32_t id = m_impl->nextHandle++;
    m_impl->loops.emplace(id, std::move(snd));
    return {id};
}

void Audio::setVolume(SoundHandle handle, float volume) {
    if (!m_impl->ok || !handle.valid()) {
        return;
    }
    const auto it = m_impl->loops.find(handle.value);
    if (it != m_impl->loops.end()) {
        ma_sound_set_volume(it->second.get(), volume);
    }
}

void Audio::stop(SoundHandle handle) {
    if (!m_impl->ok || !handle.valid()) {
        return;
    }
    const auto it = m_impl->loops.find(handle.value);
    if (it != m_impl->loops.end()) {
        ma_sound_uninit(it->second.get());
        m_impl->loops.erase(it);
    }
}

void Audio::update() {
    if (!m_impl->ok) {
        return;
    }
    // Reap finished one-shots. erase-remove: uninit the dead voices, then drop.
    auto& v = m_impl->oneShots;
    for (auto it = v.begin(); it != v.end();) {
        if (ma_sound_at_end(it->get())) {
            ma_sound_uninit(it->get());
            it = v.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace forge
