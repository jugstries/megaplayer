#include "OpenMptDecoder.h"

#include "core/Platform.h"

#include <libopenmpt/libopenmpt.hpp>

#include <algorithm>
#include <ostream>
#include <string_view>

namespace mp {

namespace {

// Preferito a UADE per i formati che conoscono entrambi: è più veloce,
// conosce la durata dei brani e permette il seek immediato.
constexpr int kProbeScore = 60;

// La piattaforma dal tipo di modulo: i formati nati sull'Amiga, i due
// dell'Atari (TCB Tracker sull'ST, Digital Tracker sul Falcon); gli altri
// tracker sono del PC.
const char* platformOf(std::string_view type)
{
    static constexpr std::string_view amiga[] = {
        "mod", "m15", "stk", "st26", "nst", "pt36", "stp", "ice", "med", "okt", "dbm", "digi", "sfx",
        "sfx2", "mms", "symmod", "ftm", "gmc", "puma", "unic", "kris",
    };
    if (type == "tcb" || type == "dtm")
        return platform::kAtariSt;
    for (std::string_view candidate : amiga) {
        if (candidate == type)
            return platform::kAmiga;
    }
    return platform::kPc;
}

class OpenMptDecoder final : public Decoder {
public:
    OpenMptDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
        , module_(load(file, log_))
    {
        info_.backend = "openmpt";
        info_.format = module_.get_metadata("type_long");
        info_.platform = platformOf(module_.get_metadata("type"));
        info_.player = module_.get_metadata("tracker");
        info_.title = module_.get_metadata("title");
        info_.author = module_.get_metadata("artist");
        info_.subsongCount = std::max(1, module_.get_num_subsongs());
        selectSubsong(0);
    }

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        module_.select_subsong(index);
        info_.currentSubsong = index;
        info_.duration = module_.get_duration_seconds();
    }

    size_t render(float* out, size_t frames) override
    {
        return module_.read_interleaved_stereo(sampleRate_, frames, out);
    }

    bool canSeek() const override { return true; }
    void seek(double seconds) override { module_.set_position_seconds(seconds); }
    double position() const override { return module_.get_position_seconds(); }

private:
    static openmpt::module load(const SourceFile& file, std::ostream& log)
    {
        try {
            return openmpt::module(file.data, log);
        } catch (const openmpt::exception& e) {
            throw DecoderError(e.what());
        }
    }

    // I messaggi di caricamento di libopenmpt finirebbero su stderr in mezzo
    // all'output della CLI: uno stream senza buffer li scarta.
    std::ostream log_{nullptr};
    int sampleRate_;
    openmpt::module module_;
    TrackInfo info_;
};

} // namespace

std::string_view OpenMptDecoderFactory::description() const
{
    return "libopenmpt: moduli tracker (MOD, XM, S3M, IT, MED, OKT, ...)";
}

int OpenMptDecoderFactory::probe(const SourceFile& file)
{
    const int result = openmpt::probe_file_header(openmpt::probe_file_header_flags_default2,
                                                  file.data.data(), file.data.size(), file.data.size());
    return result == openmpt::probe_file_header_result_success ? kProbeScore : 0;
}

std::unique_ptr<Decoder> OpenMptDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<OpenMptDecoder>(file, spec);
}

} // namespace mp
