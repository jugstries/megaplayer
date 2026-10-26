#pragma once

#include "core/Decoder.h"

#include <string>

namespace mp {

// La musica del NEC PC-98 per i driver PMD (M, M2, MZ) e FMP (OPI, OVI,
// OZI), con 98fmplayer, che li ha ricostruiti per lo YM2608 (OPNA): sei voci
// FM, tre SSG, la batteria, l'ADPCM e gli otto canali PPZ8, con i campioni dei
// file PPC, PVI e PZI accanto al brano. La batteria vuole la ROM dello YM2608
// dell'utente (ym2608_adpcm_rom.bin, quella di MAME); senza, tace. I brani
// col loop fanno due giri e sfumano.
class FmplayerDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "fmplayer"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

namespace fmplayer {

// Una cartella in più dove cercare ym2608_adpcm_rom.bin (con le
// sottocartelle): quelle delle ROM dei moduli Roland.
void addRomDirectory(const std::string& path);

} // namespace fmplayer

} // namespace mp
