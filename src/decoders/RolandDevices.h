#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

// Un modulo Roland di 88emu, per scegliere con quale suonare un file.
struct RolandDevice {
    int id = 0;          // emu88_device_id
    std::string name;    // come lo chiama 88emu, es. "SC-55mkII"
    bool la = false;     // famiglia MT-32 (sintesi LA); altrimenti Sound Canvas (GS)
    bool available = false; // ROM trovate
};

// Se il file manda SysEx all'MT-32 o ai suoi parenti (Roland, modello 16h):
// allora è scritto per l'LA, non per il General MIDI o il GS.
bool talksToLa(const std::vector<uint8_t>& data);

// Il modulo con cui suonare un file: [preferred] se è disponibile e della
// famiglia giusta, altrimenti il primo disponibile di quella famiglia, e se
// non ce n'è il primo disponibile e basta (un Sound Canvas suona anche i
// brani per MT-32, meglio di un FM). [devices] è in ordine di preferenza.
std::optional<RolandDevice> chooseRolandDevice(bool la, std::string_view preferred,
                                               const std::vector<RolandDevice>& devices);

} // namespace mp
