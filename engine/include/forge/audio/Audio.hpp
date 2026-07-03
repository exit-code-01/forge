// engine/include/forge/audio/Audio.hpp
//
// Fire-and-forget sound playback (ADR-020). miniaudio lives behind the
// pimpl — the standard seam. No audio device is NOT an error: CI runners
// and headless boxes get a disabled-but-valid Audio; play() is a no-op.
// Mixing/buses/3D spatialization arrive with VAULT's audio pass (week 5),
// on top of this same engine object.

#pragma once

#include <memory>
#include <string>

namespace forge {

class Audio {
public:
    Audio(); // never throws: no device => disabled, logged once
    ~Audio();

    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    Audio(Audio&&) = delete;
    Audio& operator=(Audio&&) = delete;

    [[nodiscard]] bool available() const;

    // Decodes (wav/mp3/flac) and plays, overlapping with itself if retriggered.
    void play(const std::string& path, float volume = 1.0f);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
