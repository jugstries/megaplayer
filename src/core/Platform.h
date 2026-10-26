#pragma once

#include <string_view>

namespace mp {

// Le piattaforme su cui la musica è nata, per filtrare la libreria. Ogni
// backend sa qual è la sua e la scrive in TrackInfo::platform; nei dati
// (database, API C) va l'identificativo, all'utente il nome.
namespace platform {

constexpr const char* kAmiga = "amiga";
constexpr const char* kPc = "pc";
constexpr const char* kC64 = "c64";
constexpr const char* kNes = "nes";
constexpr const char* kSnes = "snes";
constexpr const char* kGameBoy = "gameboy";
constexpr const char* kSega = "sega";
constexpr const char* kPcEngine = "pcengine";
constexpr const char* kMsx = "msx";
constexpr const char* kZxSpectrum = "zxspectrum";
constexpr const char* kAtari8 = "atari8";
constexpr const char* kAtariSt = "atarist";
constexpr const char* kPlayStation = "playstation";
constexpr const char* kPlayStation2 = "ps2";
constexpr const char* kSaturn = "saturn";
constexpr const char* kDreamcast = "dreamcast";
constexpr const char* kArcade = "arcade";
constexpr const char* kGameBoyAdvance = "gba";
constexpr const char* kNintendoDs = "nds";
constexpr const char* kNintendo64 = "n64";
constexpr const char* kAudio = "audio";

} // namespace platform

// "Amiga", "PC / DOS", ...; l'identificativo stesso se sconosciuto.
std::string_view platformName(std::string_view id);

} // namespace mp
