#include "Emu88Decoder.h"

#include "MidiFormats.h"
#include "SequencedMidiDecoder.h"
#include "core/Platform.h"

#include <c_interface.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>

namespace mp {

namespace {

constexpr int kProbeScore = 90; // prima di SoundFont e adlmidi, quando ci sono le ROM

// I moduli nell'ordine in cui sceglierli da soli, prima i Sound Canvas più
// diffusi, poi la famiglia MT-32. Il CM-32P (solo PCM) e l'MV-30 restano fuori.
constexpr int kGsOrder[] = {
    EMU88_DEVICE_SC55MK2, EMU88_DEVICE_SC88PRO, EMU88_DEVICE_SC88VL, EMU88_DEVICE_SC88, EMU88_DEVICE_SC8850,
    EMU88_DEVICE_SC55,    EMU88_DEVICE_SC55ST,  EMU88_DEVICE_SC155MK2, EMU88_DEVICE_SC155, EMU88_DEVICE_CM300,
    EMU88_DEVICE_SCC1A,   EMU88_DEVICE_SCB55,   EMU88_DEVICE_RLP3237, EMU88_DEVICE_VEGSPRO, EMU88_DEVICE_XPGS,
    EMU88_DEVICE_SC8820,
};
constexpr int kLaOrder[] = {
    EMU88_DEVICE_MT32_OLD, EMU88_DEVICE_CM32L, EMU88_DEVICE_MT32_NEW, EMU88_DEVICE_CM64, EMU88_DEVICE_CM32LN,
};

// Le funzioni globali di 88lib (ROM, elenco dei moduli) e i moduli accesi
// in attesa del brano dopo, condivisi da tutti i thread.
std::mutex globalMutex;
std::string preferredDevice;
std::multimap<int, emu88_context> idleContexts;

std::vector<RolandDevice> listDevices()
{
    std::vector<RolandDevice> devices;
    auto add = [&](int id, bool la) {
        const char* name = emu88_get_device_name(id);
        if (name != nullptr)
            devices.push_back({id, name, la, emu88_is_device_available(id) != 0});
    };
    for (const int id : kGsOrder)
        add(id, false);
    for (const int id : kLaOrder)
        add(id, true);
    return devices;
}

std::optional<RolandDevice> deviceFor(const std::vector<uint8_t>& data)
{
    std::lock_guard lock(globalMutex);
    return chooseRolandDevice(talksToLa(data), preferredDevice, listDevices());
}

// Un modulo acceso: uno in attesa se c'è, altrimenti uno nuovo, che il
// firmware deve avviare (da mezzo secondo a più di uno).
emu88_context takeContext(const RolandDevice& device, int sampleRate, bool& reused)
{
    std::lock_guard lock(globalMutex);
    const auto idle = idleContexts.find(device.id);
    if (idle != idleContexts.end()) {
        emu88_context context = idle->second;
        idleContexts.erase(idle);
        emu88_set_stereo_output_samplerate(context, sampleRate);
        reused = true;
        return context;
    }
    emu88_context context = emu88_create_context();
    if (context == nullptr)
        throw DecoderError("88emu: impossibile creare il modulo");
    emu88_select_device(context, device.id);
    emu88_set_stereo_output_samplerate(context, sampleRate);
    if (emu88_open_synth(context) != EMU88_RC_OK) {
        emu88_free_context(context);
        throw DecoderError("88emu: il " + device.name + " non si avvia (mancano delle ROM?)");
    }
    reused = false;
    return context;
}

// Rimette il modulo in attesa; ne basta uno per tipo.
void returnContext(int device, emu88_context context)
{
    std::lock_guard lock(globalMutex);
    if (idleContexts.count(device) == 0)
        idleContexts.emplace(device, context);
    else
        emu88_free_context(context);
}

class Emu88Decoder final : public SequencedMidiDecoder {
public:
    Emu88Decoder(const SourceFile& file, const OutputSpec& spec, const char* format, RolandDevice device)
        : SequencedMidiDecoder(file, spec, trackInfo(format, device))
        , device_(std::move(device))
    {
    }

    ~Emu88Decoder() override
    {
        if (context_ != nullptr) {
            emu88_play_silence(context_);
            returnContext(device_.id, context_);
        }
    }

private:
    static TrackInfo trackInfo(const char* format, const RolandDevice& device)
    {
        TrackInfo info;
        info.backend = "88emu";
        info.platform = platform::kPc;
        info.format = format;
        info.player = "Roland " + device.name;
        return info;
    }

    // Un modulo acceso: uno in attesa se c'è (rimesso come appena acceso),
    // altrimenti uno nuovo, che il firmware deve avviare.
    void start() override
    {
        bool reused = false;
        context_ = takeContext(device_, sampleRate(), reused);
        if (reused)
            reset();
    }

    // Reset del dispositivo (GS Reset, o il reset dei parametri sull'MT-32,
    // che ci mette un po' a tornare) e un attimo di audio buttato perché il
    // firmware lo esegua.
    void reset() override
    {
        emu88_play_silence(context_);
        emu88_play_device_reset(context_);
        std::vector<float> discard(static_cast<size_t>(sampleRate() / 10) * kChannels);
        const int tenths = device_.la ? 6 : 3;
        for (int i = 0; i < tenths; ++i)
            emu88_render_float(context_, discard.data(), static_cast<uint32_t>(discard.size() / kChannels));
    }

    void silence() override { emu88_play_silence(context_); }

    void shortMessage(unsigned port, uint32_t message) override
    {
        emu88_play_msg_on_port(context_, port, message);
    }

    void systemExclusive(unsigned port, const uint8_t* data, size_t size) override
    {
        emu88_play_sysex_on_port(context_, port, data, static_cast<uint32_t>(size));
    }

    void synthesize(float* out, size_t frames) override
    {
        emu88_render_float(context_, out, static_cast<uint32_t>(frames));
    }

    // Al ritmo del cavo MIDI (31250 baud, 3125 byte al secondo): il firmware
    // ha il tempo di eseguire quello che riceve. L'audio intanto si butta.
    void pace(size_t bytes, double settle) override
    {
        size_t frames = static_cast<size_t>((static_cast<double>(bytes) / 3125.0 + settle) * sampleRate());
        std::vector<float> discard(1024 * kChannels);
        while (frames > 0) {
            const size_t count = std::min<size_t>(frames, 1024);
            emu88_render_float(context_, discard.data(), static_cast<uint32_t>(count));
            frames -= count;
        }
    }

    unsigned ports() const override
    {
        return context_ == nullptr ? 1 : static_cast<unsigned>(std::max(1, emu88_get_midi_port_count(context_)));
    }

    RolandDevice device_;
    emu88_context context_ = nullptr;
};

} // namespace

std::string_view Emu88DecoderFactory::description() const
{
    return "88emu: MIDI su Roland Sound Canvas e MT-32 emulati (MID, RMI, XMI, MUS, HMI, HMP; servono le ROM)";
}

// Gli MDI dell'AdLib (anche rinominati .mid) restano ad AdPlug: senza i loro
// strumenti FM, su un Sound Canvas suonerebbero tutti col pianoforte.
int Emu88DecoderFactory::probe(const SourceFile& file)
{
    return midiFormat(file.data) != nullptr && !hasAdlibInstruments(file.data) && deviceFor(file.data) ? kProbeScore
                                                                                                        : 0;
}

std::unique_ptr<Decoder> Emu88DecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    const char* format = midiFormat(file.data);
    if (format == nullptr)
        throw DecoderError("non è un file MIDI");
    std::optional<RolandDevice> device = deviceFor(file.data);
    if (!device)
        throw DecoderError("88emu: nessun modulo Roland con le ROM");
    return std::make_unique<Emu88Decoder>(file, spec, format, std::move(*device));
}

namespace emu88 {

void addRomDirectory(const std::string& path)
{
    std::lock_guard lock(globalMutex);
    if (!path.empty())
        emu88_add_rom_path(path.c_str());
}

void setPreferredDevice(const std::string& name)
{
    std::lock_guard lock(globalMutex);
    preferredDevice = name;
}

std::vector<RolandDevice> devices()
{
    std::lock_guard lock(globalMutex);
    return listDevices();
}

} // namespace emu88

} // namespace mp
