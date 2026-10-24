#include "Platform.h"

#include <utility>

namespace mp {

std::string_view platformName(std::string_view id)
{
    static constexpr std::pair<std::string_view, std::string_view> names[] = {
        {platform::kAmiga, "Amiga"},
        {platform::kPc, "PC / DOS"},
        {platform::kC64, "Commodore 64"},
        {platform::kNes, "NES"},
        {platform::kSnes, "SNES"},
        {platform::kGameBoy, "Game Boy"},
        {platform::kSega, "Sega"},
        {platform::kPcEngine, "PC Engine"},
        {platform::kMsx, "MSX"},
        {platform::kZxSpectrum, "ZX Spectrum"},
        {platform::kAtari8, "Atari 8 bit"},
        {platform::kAtariSt, "Atari ST"},
        {platform::kAudio, "File audio"},
    };
    for (const auto& [key, name] : names) {
        if (key == id)
            return name;
    }
    return id;
}

} // namespace mp
