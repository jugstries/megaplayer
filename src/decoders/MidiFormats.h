#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace mp {

// Il formato di un file MIDI o di un suo parente dei giochi DOS, dalla firma,
// o nullptr se non lo è: quelli che legge il sequencer di libADLMIDI.
inline const char* midiFormat(const std::vector<uint8_t>& data)
{
    auto at = [&](size_t offset, const char* signature) {
        const size_t size = std::strlen(signature);
        return data.size() >= offset + size && std::memcmp(data.data() + offset, signature, size) == 0;
    };
    if (at(0, "MThd"))
        return "MIDI";
    if (at(0, "RIFF") && at(8, "RMID"))
        return "RMI";
    if (at(0, "GMF\x01"))
        return "GMF";
    if (at(0, "FORM") && at(8, "XDIR"))
        return "XMI";
    if (at(0, "MUS\x1a"))
        return "MUS";
    if (at(0, "HMI-MIDISONG06"))
        return "HMI";
    if (at(0, "HMIMIDIP"))
        return "HMP";
    return nullptr;
}

// Un MIDI con dentro gli strumenti AdLib: gli MDI dell'AdLib MIDIPlay, a volte
// rinominati .mid, che li definiscono con eventi del sequencer per il
// costruttore 00 00 3F (FF 7F, lunghezza, 00 00 3F). Senza cambi di programma
// General MIDI: suonano giusti solo con AdPlug, che usa quegli strumenti.
inline bool hasAdlibInstruments(const std::vector<uint8_t>& data)
{
    if (midiFormat(data) == nullptr)
        return false;
    for (size_t i = 0; i + 5 < data.size(); ++i) {
        if (data[i] == 0xFF && data[i + 1] == 0x7F && data[i + 2] < 0x80 && data[i + 3] == 0x00
            && data[i + 4] == 0x00 && data[i + 5] == 0x3F)
            return true;
    }
    return false;
}

} // namespace mp
