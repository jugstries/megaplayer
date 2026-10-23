#pragma once

#include "core/Decoder.h"

#include <memory>

struct uade_state;

namespace mp {

struct UadeStateDeleter {
    void operator()(uade_state* state) const;
};

// Formati Amiga tramite UADE: emula 68000 e Paula ed esegue i replayer
// originali di Eagleplayer/DeliTracker. L'emulatore gira in un thread del
// player (src/uade/InProcess.cpp), un brano per volta.
class UadeDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "uade"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;

private:
    // Stato usato solo per il riconoscimento, creato al primo probe perché
    // carica configurazione e database dei formati. Non avvia l'emulatore.
    std::unique_ptr<uade_state, UadeStateDeleter> probeState_;
    bool probeStateFailed_ = false;
};

} // namespace mp
