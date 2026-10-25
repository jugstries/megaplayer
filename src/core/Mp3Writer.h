#pragma once

#include "AudioFileWriter.h"

#include <filesystem>
#include <fstream>
#include <vector>

struct lame_global_struct;

namespace mp {

// Scrive frame stereo float in un MP3 con LAME: VBR alla qualità più alta
// (V0, intorno ai 245 kbit/s), tag ID3v2 con titolo, autore e album, e il tag
// LAME con la durata esatta, che i lettori usano per la barra di
// avanzamento.
class Mp3Writer final : public AudioFileWriter {
public:
    // Lancia std::runtime_error.
    Mp3Writer(const std::filesystem::path& path, int sampleRate, const FileTags& tags);
    ~Mp3Writer() override;

    Mp3Writer(const Mp3Writer&) = delete;
    Mp3Writer& operator=(const Mp3Writer&) = delete;

    void write(const float* frames, size_t count) override;
    void finish() override;

private:
    void put(const unsigned char* data, size_t size);

    lame_global_struct* lame_ = nullptr;
    std::ofstream file_;
    std::streampos firstFrame_ = 0; // dove va il tag LAME, dopo l'ID3v2
    std::vector<unsigned char> buffer_;
    bool finished_ = false;
};

} // namespace mp
