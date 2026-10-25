#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace mp {

// I tag da scrivere nel file, dove il formato li prevede (l'MP3).
struct FileTags {
    std::string title;
    std::string artist;
    std::string album;
};

// Scrive frame stereo float in un file audio.
class AudioFileWriter {
public:
    virtual ~AudioFileWriter() = default;

    // Lancia std::runtime_error.
    virtual void write(const float* frames, size_t count) = 0;
    // Completa il file (l'MP3 scrive le ultime frame e la durata). Lancia
    // std::runtime_error; senza, il file resta incompleto.
    virtual void finish() = 0;
};

// Se la build sa scrivere MP3 (con LAME).
bool canWriteMp3();

// Il formato dall'estensione: .wav (PCM a 16 bit) o .mp3 (VBR di alta
// qualità). Lancia std::runtime_error.
std::unique_ptr<AudioFileWriter> createAudioFileWriter(const std::filesystem::path& path, int sampleRate,
                                                       const FileTags& tags = {});

} // namespace mp
