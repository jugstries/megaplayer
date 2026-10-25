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

} // namespace mp
