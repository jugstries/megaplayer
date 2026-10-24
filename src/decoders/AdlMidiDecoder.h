#pragma once

#include "core/Decoder.h"

namespace mp {

// MIDI e i suoi parenti dei giochi DOS (XMI, MUS di Doom, HMI/HMP) con
// libADLMIDI: un sequencer MIDI che suona su chip FM OPL3 emulati, come una
// Sound Blaster.
class AdlMidiDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "adlmidi"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
