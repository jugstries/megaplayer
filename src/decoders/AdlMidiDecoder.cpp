#include "AdlMidiDecoder.h"

#include "AudioTags.h"
#include "core/Platform.h"

#include <adlmidi.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// Quattro chip OPL3: 72 voci a due operatori, abbastanza per il General MIDI.
constexpr int kChips = 4;

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
    auto at = [&](size_t offset, const char* signature) {
        const size_t size = std::strlen(signature);
        return data.size() >= offset + size && std::memcmp(data.data() + offset, signature, size) == 0;
    };
    if (at(0, "MThd"))
        return Family{"MIDI", "DMXOPL3"};
    if (at(0, "RIFF") && at(8, "RMID"))
        return Family{"RMI", "DMXOPL3"};
    if (at(0, "GMF\x01"))
        return Family{"GMF", "DMXOPL3"};
    if (at(0, "FORM") && at(8, "XDIR"))
        return Family{"XMI", "default AIL"};
    if (at(0, "MUS\x1a"))
        return Family{"MUS", "Bobby Prince v2"};
    if (at(0, "HMI-MIDISONG06"))
        return Family{"HMI", "HMI (Descent, Asterix)"};
    if (at(0, "HMIMIDIP"))
        return Family{"HMP", "HMI (Descent, Asterix)"};
    return std::nullopt;
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

private:
    int sampleRate_;
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
