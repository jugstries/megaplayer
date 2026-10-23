#include "OpenMptDecoder.h"

#include <libopenmpt/libopenmpt.hpp>

#include <algorithm>
#include <ostream>

namespace mp {

namespace {

// Preferito a UADE per i formati che conoscono entrambi: è più veloce,
// conosce la durata dei brani e permette il seek immediato.
constexpr int kProbeScore = 60;

class OpenMptDecoder final : public Decoder {
public:
    OpenMptDecoder(const SourceFile& file, const OutputSpec& spec)
        : sampleRate_(spec.sampleRate)
        , module_(load(file, log_))
    {
        info_.backend = "openmpt";
        info_.format = module_.get_metadata("type_long");
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
