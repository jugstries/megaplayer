#include "AudioFileWriter.h"

#include "WavWriter.h"

#ifdef MP_WITH_MP3
#include "Mp3Writer.h"
#endif

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mp {

bool canWriteMp3()
{
#ifdef MP_WITH_MP3
    return true;
#else
    return false;
#endif
}

std::unique_ptr<AudioFileWriter> createAudioFileWriter(const std::filesystem::path& path, int sampleRate,
                                                       const FileTags& tags)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".wav")
        return std::make_unique<WavWriter>(path, sampleRate);
#ifdef MP_WITH_MP3
    if (extension == ".mp3")
        return std::make_unique<Mp3Writer>(path, sampleRate, tags);
#else
    (void)tags;
#endif
    throw std::runtime_error("formato di uscita non supportato: " + path.filename().string()
                             + (canWriteMp3() ? " (WAV o MP3)" : " (WAV)"));
}

} // namespace mp
