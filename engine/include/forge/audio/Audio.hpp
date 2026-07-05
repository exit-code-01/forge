// engine/include/forge/audio/Audio.hpp
//
// Sound playback (ADR-020). miniaudio lives behind the pimpl — the standard
// seam. No audio device is NOT an error: CI runners and headless boxes get a
// disabled-but-valid Audio; every call is a harmless no-op.
//
// Week 5 (VAULT's audio pass) grew this from a single fire-and-forget play()
// into a small mixer: one-shots now carry their OWN volume (the old code set
// engine master volume per play — the last SFX silently reset every other
// voice), and persistent looping voices (ambient room tone, one music bed)
// arrive as SoundHandles you can revolume or stop. 3D spatialization is still
// out of scope — VAULT is corridors and rooms, not open world.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace forge {

// Handle to a persistent (looping) voice. value == 0 is the null handle:
// playLoop() returns it when audio is disabled or the file failed to open,
// and stop()/setVolume() treat it as a no-op.
struct SoundHandle {
    uint32_t value = 0;
    [[nodiscard]] bool valid() const { return value != 0; }
};

class Audio {
public:
    Audio(); // never throws: no device => disabled, logged once
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) = delete;
    Audio& operator=(Audio&&) = delete;

    [[nodiscard]] bool available() const;

    // One-shot: decodes (wav/mp3/flac), plays once at its OWN volume, and
    // overlaps with itself if retriggered. The voice is reaped by update().
    void play(const std::string& path, float volume = 1.0f);

    // Persistent looping voice (ambient / music). Streamed, not decoded into
    // RAM. Returns a handle for later setVolume()/stop(); null handle on
    // failure. Caller owns the lifetime via that handle.
    [[nodiscard]] SoundHandle playLoop(const std::string& path, float volume = 1.0f);
    void setVolume(SoundHandle handle, float volume);
    void stop(SoundHandle handle);

    // Reaps finished one-shot voices. Call once per frame; cheap and safe
    // when disabled. (Loops persist until stop().)
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
