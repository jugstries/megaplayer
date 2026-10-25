#pragma once

#include "RolandDevices.h"
#include "core/Decoder.h"

#include <string>
#include <vector>

namespace mp {

// MIDI e i suoi parenti dei giochi DOS sui moduli Roland emulati da 88emu
// (gearmulator, The Usual Suspects), che fa girare il firmware originale di
// Sound Canvas e MT-32 su CPU e chip sonori emulati. Le ROM le fornisce
// l'utente: senza, il backend non riconosce niente e i MIDI vanno ad adlmidi.
// Il modulo si avvia al primo audio e resta acceso per il brano dopo.
class Emu88DecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "88emu"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

namespace emu88 {

// Vedi setRolandRomDirectory() e compagni in core/DecoderRegistry.h.
void addRomDirectory(const std::string& path);
void setPreferredDevice(const std::string& name);
std::vector<RolandDevice> devices();

} // namespace emu88

} // namespace mp
