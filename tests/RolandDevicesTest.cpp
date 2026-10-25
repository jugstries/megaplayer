// La scelta del modulo Roland per un file MIDI, senza ROM: chi parla
// all'MT-32 (SysEx Roland al modello 16h) va a un modulo LA, il resto al
// Sound Canvas scelto o al primo disponibile, e senza moduli adatti a
// nessuno (i MIDI restano ad adlmidi).

#include "decoders/RolandDevices.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what)
{
    if (!ok) {
        std::fprintf(stderr, "FALLITO: %s\n", what.c_str());
        ++failures;
    }
}

// Un MIDI di un brano con un SysEx dentro, come lo scrive un file: F0, la
// lunghezza, i dati senza F0.
std::vector<uint8_t> midiWithSysex(std::vector<uint8_t> sysex)
{
    std::vector<uint8_t> data = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, 0};
    data.push_back(0x00);
    data.push_back(0xF0);
    data.push_back(static_cast<uint8_t>(sysex.size()));
    data.insert(data.end(), sysex.begin(), sysex.end());
    return data;
}

std::string name(const std::optional<mp::RolandDevice>& device)
{
    return device ? device->name : "nessuno";
}

} // namespace

int main()
{
    // Reset dei parametri dell'MT-32 e GS Reset.
    const std::vector<uint8_t> mt32 = midiWithSysex({0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x01, 0x00, 0xF7});
    const std::vector<uint8_t> gs = midiWithSysex({0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7});
    check(mp::talksToLa(mt32), "un SysEx all'MT-32 non è riconosciuto");
    check(!mp::talksToLa(gs), "un GS Reset scambiato per MT-32");
    check(!mp::talksToLa({}), "un file vuoto scambiato per MT-32");

    const std::vector<mp::RolandDevice> all = {
        {4, "SC-55mkII", false, true},
        {2, "SC-88Pro", false, true},
        {3, "SC-8850", false, false},
        {21, "MT-32 1.x", true, true},
    };
    check(name(mp::chooseRolandDevice(false, "", all)) == "SC-55mkII", "GS senza preferenze: il primo disponibile");
    check(name(mp::chooseRolandDevice(false, "SC-88Pro", all)) == "SC-88Pro", "GS con il modulo scelto");
    check(name(mp::chooseRolandDevice(false, "SC-8850", all)) == "SC-55mkII", "il modulo scelto senza ROM");
    check(name(mp::chooseRolandDevice(true, "SC-88Pro", all)) == "MT-32 1.x", "un brano per MT-32 al Sound Canvas scelto");

    const std::vector<mp::RolandDevice> onlyGs = {{4, "SC-55mkII", false, true}, {21, "MT-32 1.x", true, false}};
    check(name(mp::chooseRolandDevice(true, "", onlyGs)) == "SC-55mkII", "senza MT-32 un Sound Canvas va bene");
    const std::vector<mp::RolandDevice> onlyLa = {{4, "SC-55mkII", false, false}, {21, "MT-32 1.x", true, true}};
    check(!mp::chooseRolandDevice(false, "", onlyLa), "un MIDI generico all'MT-32");
    check(!mp::chooseRolandDevice(false, "", {}), "un modulo senza nessuna ROM");

    std::printf(failures == 0 ? "ok\n" : "FALLITO\n");
    return failures == 0 ? 0 : 1;
}
