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
        {platform::kAmstradCpc, "Amstrad CPC"},
        {platform::kAtari8, "Atari 8 bit"},
        {platform::kAtariSt, "Atari ST"},
        {platform::kX68000, "Sharp X68000"},
        {platform::kPc98, "NEC PC-98"},
        {platform::kPlayStation, "PlayStation"},
        {platform::kPlayStation2, "PlayStation 2"},
        {platform::kSaturn, "Saturn"},
        {platform::kDreamcast, "Dreamcast"},
        {platform::kArcade, "Arcade"},
        {platform::kGameBoyAdvance, "Game Boy Advance"},
        {platform::kNintendoDs, "Nintendo DS"},
        {platform::kNintendo64, "Nintendo 64"},
        {platform::kAudio, "File audio"},
    };
    for (const auto& [key, name] : names) {
        if (key == id)
            return name;
    }
    return id;
}

} // namespace mp
