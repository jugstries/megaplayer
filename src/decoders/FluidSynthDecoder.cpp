#include "FluidSynthDecoder.h"

#include "MidiFormats.h"
#include "SequencedMidiDecoder.h"
#include "core/Platform.h"

#include <fluidsynth.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>

namespace mp {

namespace {

// Dopo i moduli Roland (88emu, con le ROM) e prima dell'FM di adlmidi.
constexpr int kProbeScore = 85;
// Un brano GS completo esce così allo stesso livello del Sound Canvas di
// 88emu e dell'FM di adlmidi.
constexpr double kGain = 0.3;

// Il SoundFont di tutti i decoder e il sintetizzatore in attesa del brano
// dopo, con il suo SoundFont già caricato.
struct Synth {
    fluid_settings_t* settings = nullptr;
    fluid_synth_t* synth = nullptr;
    std::string soundFont;
    int sampleRate = 0;

    ~Synth()
    {
        if (synth != nullptr)
            delete_fluid_synth(synth);
        if (settings != nullptr)
            delete_fluid_settings(settings);
    }
};

std::mutex globalMutex;
#ifdef MP_DEFAULT_SOUNDFONT
std::string currentSoundFont = std::filesystem::exists(MP_DEFAULT_SOUNDFONT) ? MP_DEFAULT_SOUNDFONT : "";
#else
std::string currentSoundFont;
#endif
std::unique_ptr<Synth> idleSynth;

std::string soundFontInUse()
{
    std::lock_guard lock(globalMutex);
    return currentSoundFont;
}

// Un sintetizzatore con il SoundFont: quello in attesa se ha lo stesso
// SoundFont e la stessa frequenza, altrimenti uno nuovo, che lo carica
// (un decimo di secondo per GeneralUser GS).
std::unique_ptr<Synth> takeSynth(const std::string& soundFont, int sampleRate, bool& reused)
{
    {
        std::lock_guard lock(globalMutex);
        if (idleSynth && idleSynth->soundFont == soundFont && idleSynth->sampleRate == sampleRate) {
            reused = true;
            return std::move(idleSynth);
        }
    }
    static std::once_flag quiet;
    std::call_once(quiet, [] {
        // I suoi messaggi su stderr no: gli errori arrivano come DecoderError.
        for (const int level : {FLUID_ERR, FLUID_WARN, FLUID_INFO, FLUID_DBG})
            fluid_set_log_function(level, nullptr, nullptr);
    });
    auto synth = std::make_unique<Synth>();
    synth->soundFont = soundFont;
    synth->sampleRate = sampleRate;
    synth->settings = new_fluid_settings();
    fluid_settings_setnum(synth->settings, "synth.sample-rate", sampleRate);
    fluid_settings_setnum(synth->settings, "synth.gain", kGain);
    // Due porte, per i file da 32 canali come quelli per SC-88.
    fluid_settings_setint(synth->settings, "synth.midi-channels", 32);
    fluid_settings_setint(synth->settings, "synth.polyphony", 256);
    fluid_settings_setstr(synth->settings, "synth.midi-bank-select", "gs");
    synth->synth = new_fluid_synth(synth->settings);
    if (synth->synth == nullptr)
        throw DecoderError("fluidsynth: impossibile creare il sintetizzatore");
    if (fluid_synth_sfload(synth->synth, soundFont.c_str(), 1) == FLUID_FAILED)
        throw DecoderError("fluidsynth: SoundFont illeggibile: " + soundFont);
    reused = false;
    return synth;
}

void returnSynth(std::unique_ptr<Synth> synth)
{
    std::lock_guard lock(globalMutex);
    idleSynth = std::move(synth);
}

class FluidSynthDecoder final : public SequencedMidiDecoder {
public:
    FluidSynthDecoder(const SourceFile& file, const OutputSpec& spec, const char* format, std::string soundFont)
        : SequencedMidiDecoder(file, spec, trackInfo(format, soundFont))
        , soundFont_(std::move(soundFont))
    {
    }

    ~FluidSynthDecoder() override
    {
        if (synth_) {
            fluid_synth_all_sounds_off(synth_->synth, -1);
            returnSynth(std::move(synth_));
        }
    }

private:
    static TrackInfo trackInfo(const char* format, const std::string& soundFont)
    {
        TrackInfo info;
        info.backend = "fluidsynth";
        info.platform = platform::kPc;
        info.format = format;
        info.player = "SoundFont " + std::filesystem::path(soundFont).stem().string();
        return info;
    }

    void start() override
    {
        bool reused = false;
        synth_ = takeSynth(soundFont_, sampleRate(), reused);
        if (reused)
            reset();
    }

    void reset() override { fluid_synth_system_reset(synth_->synth); }

    void silence() override { fluid_synth_all_sounds_off(synth_->synth, -1); }

    void shortMessage(unsigned port, uint32_t message) override
    {
        fluid_synth_t* synth = synth_->synth;
        const int channel = static_cast<int>(message & 0x0F) + static_cast<int>(port) * 16;
        const int first = static_cast<int>((message >> 8) & 0x7F);
        const int second = static_cast<int>((message >> 16) & 0x7F);
        switch (message & 0xF0) {
        case 0x80: fluid_synth_noteoff(synth, channel, first); break;
        case 0x90: fluid_synth_noteon(synth, channel, first, second); break;
        case 0xA0: fluid_synth_key_pressure(synth, channel, first, second); break;
        case 0xB0: fluid_synth_cc(synth, channel, first, second); break;
        case 0xC0: fluid_synth_program_change(synth, channel, first); break;
        case 0xD0: fluid_synth_channel_pressure(synth, channel, first); break;
        case 0xE0: fluid_synth_pitch_bend(synth, channel, first | second << 7); break;
        default: break;
        }
    }

    // FluidSynth vuole il SysEx senza F0 e F7; capisce GS e GM Reset.
    void systemExclusive(unsigned /*port*/, const uint8_t* data, size_t size) override
    {
        if (size > 2)
            fluid_synth_sysex(synth_->synth, reinterpret_cast<const char*>(data + 1), static_cast<int>(size - 2),
                              nullptr, nullptr, nullptr, 0);
    }

    void synthesize(float* out, size_t frames) override
    {
        fluid_synth_write_float(synth_->synth, static_cast<int>(frames), out, 0, 2, out, 1, 2);
    }

    unsigned ports() const override { return 2; }

    std::string soundFont_;
    std::unique_ptr<Synth> synth_;
};

} // namespace

std::string_view FluidSynthDecoderFactory::description() const
{
    return "FluidSynth: MIDI su un SoundFont, di serie GeneralUser GS (MID, RMI, XMI, MUS, HMI, HMP)";
}

// Gli MDI dell'AdLib restano ad AdPlug, come con 88emu.
int FluidSynthDecoderFactory::probe(const SourceFile& file)
{
    if (midiFormat(file.data) == nullptr || hasAdlibInstruments(file.data))
        return 0;
    return soundFontInUse().empty() ? 0 : kProbeScore;
}

std::unique_ptr<Decoder> FluidSynthDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const char* format = midiFormat(file.data);
    if (format == nullptr)
        throw DecoderError("non è un file MIDI");
    std::string soundFont = soundFontInUse();
    if (soundFont.empty())
        throw DecoderError("fluidsynth: nessun SoundFont");
    return std::make_unique<FluidSynthDecoder>(file, spec, format, std::move(soundFont));
}

namespace fluidsynth {

void setSoundFont(const std::string& path)
{
    std::lock_guard lock(globalMutex);
    currentSoundFont = path;
}

std::string soundFont()
{
    return soundFontInUse();
}

} // namespace fluidsynth

} // namespace mp
