#pragma once

#include "core/Decoder.h"

namespace mp {

// I formati AdLib dei giochi e dei tracker DOS (RAD, HSC, D00, A2M, CMF, IMF,
// DRO, ROL, LDS e una cinquantina d'altri) con AdPlug, sul chip FM OPL3
// emulato da Nuked OPL3. Anche i VGM per i chip OPL, che gme non suona.
class AdplugDecoderFactory final : public DecoderFactory {
public:
    std::string_view name() const override { return "adplug"; }
    std::string_view description() const override;
    int probe(const SourceFile& file) override;
    std::unique_ptr<Decoder> open(const SourceFile& file, const OutputSpec& spec) override;
};

} // namespace mp
