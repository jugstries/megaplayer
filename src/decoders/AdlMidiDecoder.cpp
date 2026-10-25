#include "AdlMidiDecoder.h"

#include "AudioTags.h"
#include "MidiFormats.h"
#include "core/Platform.h"

#include <adlmidi.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// Quattro chip OPL3: 72 voci a due operatori, abbastanza per il General MIDI.
constexpr int kChips = 4;
constexpr int kMidiChannels = 16;

// Il formato, dalla firma, e il banco di strumenti con cui suonava: i giochi
// con la libreria Miles (XMI) usavano i timbri AIL, Doom (MUS) quelli DMX, i
// giochi con la libreria HMI i suoi. Per il MIDI generico, il banco General
// MIDI più curato tra quelli incorporati.
struct Family {
    const char* format;
    const char* bank; // parte del nome del banco in libADLMIDI
};

std::optional<Family> family(const std::vector<uint8_t>& data)
{
    const char* format = midiFormat(data);
    if (format == nullptr)
        return std::nullopt;
    const std::string_view name = format;
    if (name == "XMI")
        return Family{format, "default AIL"};
    if (name == "MUS")
        return Family{format, "Bobby Prince v2"};
    if (name == "HMI" || name == "HMP")
        return Family{format, "HMI (Descent, Asterix)"};
    return Family{format, "DMXOPL3"};
}

int findBank(const char* wanted)
{
    const char* const* names = adl_getBankNames();
    for (int i = 0; i < adl_getBanksCount(); ++i) {
        if (std::strstr(names[i], wanted) != nullptr)
            return i;
    }
    return 0;
}

struct PlayerDeleter {
    void operator()(ADL_MIDIPlayer* player) const { adl_close(player); }
};

class AdlMidiDecoder final : public Decoder {
public:
    AdlMidiDecoder(const SourceFile& file, const OutputSpec& spec, const Family& family)
        : sampleRate_(spec.sampleRate)
        , data_(file.data)
        , player_(adl_init(spec.sampleRate))
    {
        if (!player_)
            throw DecoderError(adl_errorString());
        const int bank = findBank(family.bank);
        if (adl_setBank(player_.get(), bank) < 0 || adl_setNumChips(player_.get(), kChips) < 0
            || adl_switchEmulator(player_.get(), ADLMIDI_EMU_NUKED) < 0)
            throw DecoderError(adl_errorInfo(player_.get()));
        adl_setSoftPanEnabled(player_.get(), 1);
        adl_setLoopEnabled(player_.get(), 0);

        info_.backend = "adlmidi";
        info_.platform = platform::kPc;
        info_.format = family.format;
        info_.player = std::string("OPL3, banco ") + adl_getBankNames()[bank];
        selectSubsong(0);
        info_.subsongCount = std::max(1, adl_getSongsCount(player_.get()));
    }

    AdlMidiDecoder(const AdlMidiDecoder&) = delete;
    AdlMidiDecoder& operator=(const AdlMidiDecoder&) = delete;

    const TrackInfo& info() const override { return info_; }

    // Il sottobrano (gli XMI ne hanno più d'uno) si sceglie prima di aprire.
    void selectSubsong(int index) override
    {
        adl_selectSongNum(player_.get(), index);
        if (adl_openData(player_.get(), data_.data(), static_cast<unsigned long>(data_.size())) < 0)
            throw DecoderError(adl_errorInfo(player_.get()));
        info_.currentSubsong = index;
        // I canali spenti sono del sequencer, che riaprendo si azzera.
        for (int channel = 0; channel < kMidiChannels; ++channel)
            adl_setChannelEnabled(player_.get(), static_cast<size_t>(channel), muted_[channel] ? 0 : 1);
        if (const char* title = adl_metaMusicTitle(player_.get()))
            info_.title = textToUtf8(title);
        // Comprende il secondo che libADLMIDI aspetta dopo l'ultimo evento,
        // per le note in rilascio.
        info_.duration = adl_totalTimeLength(player_.get());
        rendered_ = 0;
    }

    size_t render(float* out, size_t frames) override
    {
        // adl_atEnd() diventa vero all'ultimo evento; adl_play() continua
        // fino alla fine dell'attesa, poi restituisce 0.
        samples_.resize(frames * kChannels);
        const size_t done =
            static_cast<size_t>(adl_play(player_.get(), static_cast<int>(frames * kChannels), samples_.data()))
            / kChannels;
        for (size_t i = 0; i < done * kChannels; ++i)
            out[i] = samples_[i] / 32768.0f;
        rendered_ += done;
        return done;
    }

    bool canSeek() const override { return true; }

    // Come ogni sequencer MIDI, dopo un salto suonano solo le note che
    // partono da lì in poi.
    void seek(double seconds) override
    {
        seconds = std::max(0.0, seconds);
        adl_positionSeek(player_.get(), seconds);
        rendered_ = static_cast<size_t>(seconds * sampleRate_);
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    // I sedici canali MIDI; il decimo è la batteria del General MIDI.
    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        for (int channel = 0; channel < kMidiChannels; ++channel)
            names.push_back("Canale " + std::to_string(channel + 1) + (channel == 9 ? " · batteria" : ""));
        return names;
    }

    // Zittendo un canale libADLMIDI rilascia le sue note; riacceso, suona
    // dalla nota dopo.
    void setChannelMuted(int channel, bool muted) override
    {
        muted_[channel] = muted;
        adl_setChannelEnabled(player_.get(), static_cast<size_t>(channel), muted ? 0 : 1);
    }

private:
    int sampleRate_;
    bool muted_[kMidiChannels] = {};
    std::vector<uint8_t> data_;
    std::unique_ptr<ADL_MIDIPlayer, PlayerDeleter> player_;
    std::vector<short> samples_;
    size_t rendered_ = 0;
    TrackInfo info_;
};

} // namespace

std::string_view AdlMidiDecoderFactory::description() const
{
    return "libADLMIDI: MIDI su chip FM OPL3 (MID, RMI, XMI, MUS, HMI, HMP)";
}

int AdlMidiDecoderFactory::probe(const SourceFile& file)
{
    return family(file.data) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> AdlMidiDecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const std::optional<Family> found = family(file.data);
    if (!found)
        throw DecoderError("non è un file MIDI");
    return std::make_unique<AdlMidiDecoder>(file, spec, *found);
}

} // namespace mp
