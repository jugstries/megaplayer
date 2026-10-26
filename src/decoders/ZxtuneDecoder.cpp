#include "ZxtuneDecoder.h"

#include "core/Platform.h"

// ZXTune: compilato come nei suoi makefile, con char senza segno.
#include "binary/container_factories.h"
#include "core/core_parameters.h"
#include "core/plugins/players/plugins_list.h"
#include "core/service.h"
#include "error.h"
#include "module/attributes.h"
#include "module/holder.h"
#include "parameters/container.h"
#include "parameters/merged_accessor.h"
#include "sound/sound_parameters.h"

#include <algorithm>
#include <string>
#include <typeinfo>
#include <vector>

namespace ZXTune {

void RegisterAYMPlugins(PlayerPluginsRegistrator& players);

// Al posto di core/plugins/players/plugins_list.cpp: solo i formati dei chip
// AY, gli altri li suonano altri backend.
void RegisterPlayerPlugins(PlayerPluginsRegistrator& players, ArchivePluginsRegistrator&)
{
    RegisterAYMPlugins(players);
}

} // namespace ZXTune

namespace mp {

namespace {

// Dopo libopenmpt e prima di UADE, che riconosce molti formati solo dal
// nome.
constexpr int kProbeScore = 55;
// I moduli AY sono piccoli; i dump PSG un po' meno.
constexpr size_t kMaxSize = 8 * 1024 * 1024;
// Come per gli stream dei giochi: due giri, poi dieci secondi di dissolvenza.
constexpr int kLoops = 2;
constexpr double kFadeSeconds = 10;
constexpr int kChipMaskBits = 5; // Devices::AYM::VOICES

const ZXTune::Service& service()
{
    static const ZXTune::Service::Ptr instance = ZXTune::Service::Create(Parameters::Container::Create());
    return *instance;
}

Module::Holder::Ptr openModule(const std::vector<uint8_t>& data)
{
    if (data.empty() || data.size() > kMaxSize)
        return {};
    try {
        return service().OpenModule(Binary::CreateContainer(Binary::Dump(data.begin(), data.end())), "",
                                    Parameters::Container::Create());
    } catch (...) {
        return {};
    }
}

// Gli errori di ZXTune (Error, e le eccezioni della libreria standard) come
// DecoderError.
template <typename Function>
auto guarded(Function&& function)
{
    try {
        return function();
    } catch (const Error& error) {
        throw DecoderError("ZXTune: " + std::string(error.ToString()));
    } catch (const DecoderError&) {
        throw;
    } catch (const std::exception& error) {
        throw DecoderError(std::string("ZXTune: ") + typeid(error).name() + ": " + error.what());
    }
}

std::string property(const Parameters::Accessor& properties, std::string_view name)
{
    return std::string(properties.FindString(Parameters::Identifier(name)).value_or(Parameters::StringType()));
}

class ZxtuneDecoder final : public Decoder {
public:
    ZxtuneDecoder(Module::Holder::Ptr holder, const OutputSpec& spec)
        : holder_(std::move(holder))
        , sampleRate_(spec.sampleRate)
        , parameters_(Parameters::Container::Create())
    {
        const Module::Information information = holder_->GetModuleInformation();
        const double first = information.Duration.Get() / 1000.0;
        const double loop = information.LoopDuration.Get() / 1000.0;
        // ZXTune fa ripartire anche i dump (PSG), che non hanno un loop: lo
        // hanno solo i moduli, con le loro posizioni.
        looped_ = loop > 0 && information.Track;
        fadeStart_ = first + (looped_ ? loop * (kLoops - 1) : 0);
        info_.duration = looped_ ? fadeStart_ + kFadeSeconds : first;
        const int channels = information.Track ? static_cast<int>(information.Track->ChannelsCount) : 3;
        for (int i = 0; i < channels; ++i) {
            const char name = static_cast<char>('A' + i % 3);
            channels_.push_back(channels > 3 ? std::string(1, name) + std::to_string(i / 3 + 1) : std::string(1, name));
        }

        const Parameters::Accessor::Ptr properties = holder_->GetModuleProperties();
        info_.backend = "zxtune";
        info_.format = property(*properties, Module::ATTR_TYPE);
        info_.player = property(*properties, Module::ATTR_PROGRAM);
        info_.title = property(*properties, Module::ATTR_TITLE);
        info_.author = property(*properties, Module::ATTR_AUTHOR);
        info_.platform = property(*properties, Module::ATTR_PLATFORM) == "Amstrad CPC" ? platform::kAmstradCpc
                                                                                         : platform::kZxSpectrum;
        // I giri li conta megaplayer: ZXTune ricomincia sempre.
        parameters_->SetValue(Parameters::ZXTune::Sound::LOOPED, 1);
        // Prima i nostri, poi quelli del modulo (la tabella delle frequenze,
        // per esempio), come nelle app di ZXTune.
        renderer_ = guarded([this, &properties] {
            return holder_->CreateRenderer(sampleRate_, Parameters::CreateMergedAccessor(parameters_, properties));
        });
    }

    const TrackInfo& info() const override { return info_; }

    void selectSubsong(int index) override
    {
        if (index != 0)
            throw DecoderError("sottobrano " + std::to_string(index + 1) + " non valido");
        seek(0);
    }

    size_t render(float* out, size_t frames) override
    {
        const auto total = static_cast<size_t>(info_.duration.value_or(0) * sampleRate_);
        frames = std::min(frames, total > rendered_ ? total - rendered_ : 0);
        size_t done = 0;
        while (done < frames) {
            if (pendingStart_ == pending_.size()) {
                pending_ = guarded([this] { return renderer_->Render(); });
                pendingStart_ = 0;
                if (pending_.empty())
                    break;
            }
            const size_t count = std::min(frames - done, pending_.size() - pendingStart_);
            for (size_t i = 0; i < count; ++i) {
                const Sound::Sample& sample = pending_[pendingStart_ + i];
                float gain = 1.0f / 32768;
                if (looped_) {
                    const double time = static_cast<double>(rendered_ + done + i) / sampleRate_;
                    if (time > fadeStart_)
                        gain *= static_cast<float>(std::max(0.0, 1 - (time - fadeStart_) / kFadeSeconds));
                }
                out[(done + i) * kChannels] = sample.Left() * gain;
                out[(done + i) * kChannels + 1] = sample.Right() * gain;
            }
            pendingStart_ += count;
            done += count;
        }
        rendered_ += done;
        return done;
    }

    bool canSeek() const override { return true; }

    void seek(double seconds) override
    {
        const double target = std::clamp(seconds, 0.0, info_.duration.value_or(seconds));
        guarded([this, target] {
            renderer_->Reset();
            renderer_->SetPosition(Time::AtMillisecond(static_cast<uint_t>(target * 1000)));
        });
        pending_ = {};
        pendingStart_ = 0;
        rendered_ = static_cast<size_t>(target * sampleRate_);
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    std::vector<std::string> channels() const override { return channels_; }

    // ZXTune rilegge i parametri a ogni frame. Ogni chip ha cinque bit nella
    // maschera (A, B, C, rumore e inviluppo): il secondo chip del TurboSound
    // comincia dal sesto. Rumore e inviluppo restano: patches/zxtune.patch fa
    // tacere del tutto il canale zittito.
    void setChannelMuted(int channel, bool muted) override
    {
        if (channel < 0 || channel >= static_cast<int>(channels_.size()))
            return;
        const Parameters::IntType bit = Parameters::IntType(1) << (channel / 3 * kChipMaskBits + channel % 3);
        if (muted)
            mutedMask_ |= bit;
        else
            mutedMask_ &= ~bit;
        parameters_->SetValue(Parameters::ZXTune::Core::CHANNELS_MASK, mutedMask_);
    }

private:
    Module::Holder::Ptr holder_;
    int sampleRate_;
    Parameters::Container::Ptr parameters_;
    Module::Renderer::Ptr renderer_;
    Sound::Chunk pending_;
    size_t pendingStart_ = 0;
    std::vector<std::string> channels_;
    Parameters::IntType mutedMask_ = 0;
    bool looped_ = false;
    double fadeStart_ = 0;
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

std::string_view ZxtuneDecoderFactory::description() const
{
    return "ZXTune: i tracker AY dello ZX Spectrum e del CPC (PT3, STC, ASC, SQT, ...), PSG e VTX";
}

int ZxtuneDecoderFactory::probe(const SourceFile& file)
{
    return openModule(file.data) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> ZxtuneDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    Module::Holder::Ptr holder = openModule(file.data);
    if (!holder)
        throw DecoderError("ZXTune non riconosce il modulo");
    return std::make_unique<ZxtuneDecoder>(std::move(holder), spec);
}

} // namespace mp
