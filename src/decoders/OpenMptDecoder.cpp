#include "OpenMptDecoder.h"

#include "core/Platform.h"

#include <libopenmpt/libopenmpt.hpp>
#include <libopenmpt/libopenmpt_ext.hpp>

#include <algorithm>
#include <ostream>
#include <string_view>
#include <vector>

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
        , interactive_(static_cast<openmpt::ext::interactive*>(module_.get_interface(openmpt::ext::interactive_id)))
        , channels_(module_.get_channel_names())
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

    // I canali del tracker, con il nome che gli ha dato l'autore (di solito
    // nessuno).
    std::vector<std::string> channels() const override
    {
        return interactive_ ? channels_ : std::vector<std::string>{};
    }

    void setChannelMuted(int channel, bool muted) override
    {
        if (interactive_)
            interactive_->set_channel_mute_status(channel, muted);
    }

private:
    static openmpt::module_ext load(const SourceFile& file, std::ostream& log)
    {
        try {
            return openmpt::module_ext(file.data, log);
        } catch (const openmpt::exception& e) {
            throw DecoderError(e.what());
        }
    }

    // I messaggi di caricamento di libopenmpt finirebbero su stderr in mezzo
    // all'output della CLI: uno stream senza buffer li scarta.
    std::ostream log_{nullptr};
    int sampleRate_;
    openmpt::module_ext module_;
    // Il mute dei canali; resta valido cambiando sottobrano.
    openmpt::ext::interactive* interactive_;
    std::vector<std::string> channels_;
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
