#pragma once

#include "core/Decoder.h"

#include <string>

namespace mp {

// MIDI e i suoi parenti dei giochi DOS su un SoundFont, con FluidSynth: per
// chi non ha le ROM dei moduli Roland, un General MIDI a campioni invece
// dell'FM di adlmidi. Il SoundFont è GeneralUser GS, che viaggia con le app,
// o quello che sceglie l'utente; senza SoundFont il backend non riconosce
// niente e i MIDI vanno ad adlmidi. Il SoundFont si carica al primo audio e
// resta caricato per il brano dopo.
class FluidSynthDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "fluidsynth"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

namespace fluidsynth {

// Vedi setSoundFont() in core/DecoderRegistry.h.
void setSoundFont(const std::string& path);
std::string soundFont();

} // namespace fluidsynth

} // namespace mp
