#include "Emu88Decoder.h"

#include "AudioTags.h"
#include "MidiFormats.h"
#include "core/Platform.h"

#include <c_interface.h>

// Il sequencer MIDI di libADLMIDI (licenza MIT), compilato qui con un altro
// nome perché non si scontri con la copia dentro libADLMIDI.
#define BW_MidiSequencer MpMidiSequencer
#include "midiseq/midi_sequencer_impl.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>

namespace mp {

namespace {

constexpr int kProbeScore = 90; // prima di adlmidi, quando ci sono le ROM
constexpr int kMidiChannels = 16;
// Dopo l'ultimo evento: le note in rilascio e il riverbero.
constexpr double kTail = 2.0;

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

class Emu88Decoder final : public Decoder {
public:
    Emu88Decoder(const SourceFile& file, const OutputSpec& spec, const char* format, RolandDevice device)
        : sampleRate_(spec.sampleRate)
        , data_(file.data)
        , device_(std::move(device))
    {
        interface_.rtUserData = this;
        interface_.rt_noteOn = noteOn;
        interface_.rt_noteOff = noteOff;
        interface_.rt_noteOffVel = noteOffVelocity;
        interface_.rt_noteAfterTouch = noteAfterTouch;
        interface_.rt_channelAfterTouch = channelAfterTouch;
        interface_.rt_controllerChange = controllerChange;
        interface_.rt_patchChange = patchChange;
        interface_.rt_pitchBend = pitchBend;
        interface_.rt_systemExclusive = systemExclusive;
        interface_.rt_deviceSwitch = deviceSwitch;
        interface_.rt_currentDevice = currentDevice;
        interface_.pcmSampleRate = static_cast<uint32_t>(sampleRate_);
        interface_.pcmFrameSize = 2 * sizeof(float);
        sequencer_.setInterface(&interface_);
        sequencer_.setLoopEnabled(false);

        info_.backend = "88emu";
        info_.platform = platform::kPc;
        info_.format = format;
        info_.player = "Roland " + device_.name;
        load(0);
        info_.subsongCount = std::max(1, sequencer_.getSongsCount());
    }

    ~Emu88Decoder() override
    {
        if (context_ != nullptr) {
            emu88_play_silence(context_);
            returnContext(device_.id, context_);
        }
    }

    Emu88Decoder(const Emu88Decoder&) = delete;
    Emu88Decoder& operator=(const Emu88Decoder&) = delete;

    const TrackInfo& info() const override { return info_; }

    // Gli XMI possono avere più brani: il sequencer li sceglie prima di
    // leggere il file.
    void selectSubsong(int index) override
    {
        load(index);
        if (context_ != nullptr)
            resetDevice();
    }

    size_t render(float* out, size_t frames) override
    {
        start();
        size_t done = 0;
        while (done < frames) {
            if (!ended_ && untilEvent_ < 0.5 / sampleRate_) {
                untilEvent_ = sequencer_.Tick(sinceTick_, granularity());
                sinceTick_ = 0;
                ended_ = sequencer_.positionAtEnd();
            }
            size_t count = frames - done;
            if (ended_) {
                count = std::min(count, tailLeft_);
                if (count == 0)
                    break;
                tailLeft_ -= count;
            } else {
                // Fino al prossimo evento, che il modulo riceve al primo
                // campione dopo.
                const double toEvent = std::ceil(untilEvent_ * sampleRate_ - 1e-6);
                count = std::min(count, static_cast<size_t>(std::max(1.0, toEvent)));
            }
            emu88_render_float(context_, out + done * kChannels, static_cast<uint32_t>(count));
            const double seconds = static_cast<double>(count) / sampleRate_;
            untilEvent_ -= seconds;
            sinceTick_ += seconds;
            done += count;
        }
        rendered_ += done;
        return done;
    }

    bool canSeek() const override { return true; }

    // Il sequencer riparte dall'inizio e manda al modulo tutto tranne le
    // note, così programmi, controller e SysEx sono quelli giusti; suonano le
    // note che partono da lì in poi.
    void seek(double seconds) override
    {
        start();
        emu88_play_silence(context_);
        const double length = sequencer_.timeLength();
        seconds = std::clamp(seconds, 0.0, length + kTail);
        if (seconds >= length) {
            ended_ = true;
            tailLeft_ = static_cast<size_t>((length + kTail - seconds) * sampleRate_);
        } else {
            untilEvent_ = sequencer_.seek(seconds, granularity());
            sinceTick_ = 0;
            ended_ = sequencer_.positionAtEnd();
            tailLeft_ = static_cast<size_t>(kTail * sampleRate_);
        }
        rendered_ = static_cast<size_t>(seconds * sampleRate_);
    }

    double position() const override { return static_cast<double>(rendered_) / sampleRate_; }

    // I sedici canali MIDI; il decimo è la batteria (anche sull'MT-32, dove
    // i canali dal 2 al 9 sono le sue parti).
    std::vector<std::string> channels() const override
    {
        std::vector<std::string> names;
        for (int channel = 0; channel < kMidiChannels; ++channel)
            names.push_back("Canale " + std::to_string(channel + 1) + (channel == 9 ? " · batteria" : ""));
        return names;
    }

    // Zittito, un canale non riceve più note e quelle accese si spengono;
    // riacceso, suona dalla nota dopo.
    void setChannelMuted(int channel, bool muted) override
    {
        muted_[channel] = muted;
        if (!muted || context_ == nullptr)
            return;
        for (int port = 0; port < emu88_get_midi_port_count(context_); ++port) {
            const uint32_t status = 0xB0 | static_cast<uint32_t>(channel);
            for (const uint32_t controller : {64u << 8, 123u << 8, 120u << 8}) // pedale, note, suoni
                emu88_play_msg_on_port(context_, static_cast<unsigned>(port), status | controller);
        }
    }

private:
    void load(int song)
    {
        sequencer_.setSongNum(song);
        if (!sequencer_.loadMIDI(data_.data(), data_.size()))
            throw DecoderError(std::string("88emu: ") + sequencer_.getErrorString());
        info_.currentSubsong = song;
        if (const char* title = sequencer_.getMusicTitle(); title != nullptr && *title != '\0')
            info_.title = textToUtf8(title);
        info_.duration = sequencer_.timeLength() + kTail;
        untilEvent_ = 0;
        sinceTick_ = 0;
        ended_ = false;
        tailLeft_ = static_cast<size_t>(kTail * sampleRate_);
        rendered_ = 0;
        portOfTrack_.clear();
        portNames_.clear();
    }

    // Il modulo si accende al primo audio, così chi legge solo le
    // informazioni del file (la libreria) non lo avvia mai.
    void start()
    {
        if (context_ != nullptr)
            return;
        bool reused = false;
        context_ = takeContext(device_, sampleRate_, reused);
        if (reused)
            resetDevice();
    }

    // Il modulo come appena acceso: reset del dispositivo (GS Reset, o il
    // reset dei parametri sull'MT-32, che ci mette un po' a tornare) e un
    // attimo di audio buttato perché il firmware lo esegua.
    void resetDevice()
    {
        emu88_play_silence(context_);
        emu88_play_device_reset(context_);
        std::vector<float> discard(static_cast<size_t>(sampleRate_ / 10) * kChannels);
        const int tenths = device_.la ? 6 : 3;
        for (int i = 0; i < tenths; ++i)
            emu88_render_float(context_, discard.data(), static_cast<uint32_t>(discard.size() / kChannels));
    }

    double granularity() const { return 1.0 / sampleRate_; }

    // Il canale arriva dal sequencer già spostato di 16 per ogni porta.
    void send(uint8_t channel, uint32_t message)
    {
        const unsigned port = static_cast<unsigned>(channel / 16) % static_cast<unsigned>(std::max(1, ports()));
        emu88_play_msg_on_port(context_, port, message | (channel % 16u));
    }

    int ports() const { return emu88_get_midi_port_count(context_); }

    static Emu88Decoder& self(void* userdata) { return *static_cast<Emu88Decoder*>(userdata); }

    static void noteOn(void* userdata, uint8_t channel, uint8_t note, uint8_t velocity)
    {
        Emu88Decoder& decoder = self(userdata);
        if (!decoder.muted_[channel % 16])
            decoder.send(channel, 0x90u | note << 8 | velocity << 16);
    }

    static void noteOff(void* userdata, uint8_t channel, uint8_t note)
    {
        self(userdata).send(channel, 0x80u | note << 8);
    }

    static void noteOffVelocity(void* userdata, uint8_t channel, uint8_t note, uint8_t velocity)
    {
        self(userdata).send(channel, 0x80u | note << 8 | velocity << 16);
    }

    static void noteAfterTouch(void* userdata, uint8_t channel, uint8_t note, uint8_t value)
    {
        self(userdata).send(channel, 0xA0u | note << 8 | value << 16);
    }

    static void channelAfterTouch(void* userdata, uint8_t channel, uint8_t value)
    {
        self(userdata).send(channel, 0xD0u | value << 8);
    }

    static void controllerChange(void* userdata, uint8_t channel, uint8_t type, uint8_t value)
    {
        self(userdata).send(channel, 0xB0u | type << 8 | value << 16);
    }

    static void patchChange(void* userdata, uint8_t channel, uint8_t patch)
    {
        self(userdata).send(channel, 0xC0u | patch << 8);
    }

    static void pitchBend(void* userdata, uint8_t channel, uint8_t msb, uint8_t lsb)
    {
        self(userdata).send(channel, 0xE0u | lsb << 8 | msb << 16);
    }

    // Il SysEx completo, F0 ... F7, anche se il file lo tiene senza.
    static void systemExclusive(void* userdata, const uint8_t* message, size_t size)
    {
        Emu88Decoder& decoder = self(userdata);
        std::vector<uint8_t>& sysex = decoder.sysex_;
        sysex.clear();
        if (size == 0 || message[0] != 0xF0)
            sysex.push_back(0xF0);
        sysex.insert(sysex.end(), message, message + size);
        if (sysex.back() != 0xF7)
            sysex.push_back(0xF7);
        emu88_play_sysex(decoder.context_, sysex.data(), static_cast<uint32_t>(sysex.size()));
    }

    // Il meta evento "porta" dei file per più moduli: ogni nome nuovo è la
    // porta successiva, come in libADLMIDI.
    static void deviceSwitch(void* userdata, size_t track, const char* data, size_t length)
    {
        Emu88Decoder& decoder = self(userdata);
        const std::string name(data, length);
        const auto known = decoder.portNames_.find(name);
        const size_t port = known != decoder.portNames_.end() ? known->second : decoder.portNames_.size();
        decoder.portNames_.emplace(name, port);
        decoder.portOfTrack_[track] = port;
    }

    static size_t currentDevice(void* userdata, size_t track)
    {
        const Emu88Decoder& decoder = self(userdata);
        const auto port = decoder.portOfTrack_.find(track);
        return port == decoder.portOfTrack_.end() ? 0 : port->second * 16;
    }

    int sampleRate_;
    std::vector<uint8_t> data_;
    RolandDevice device_;
    TrackInfo info_;
    BW_MidiRtInterface interface_{};
    MpMidiSequencer sequencer_;
    emu88_context context_ = nullptr;
    bool muted_[kMidiChannels] = {};
    std::vector<uint8_t> sysex_;
    std::map<std::string, size_t> portNames_;
    std::map<size_t, size_t> portOfTrack_;

    double untilEvent_ = 0; // secondi al prossimo evento
    double sinceTick_ = 0;  // secondi suonati dall'ultimo Tick()
    bool ended_ = false;
    size_t tailLeft_ = 0; // frame di coda dopo l'ultimo evento
    size_t rendered_ = 0;
};

} // namespace

std::string_view Emu88DecoderFactory::description() const
{
    return "88emu: MIDI su Roland Sound Canvas e MT-32 emulati (MID, RMI, XMI, MUS, HMI, HMP; servono le ROM)";
}

int Emu88DecoderFactory::probe(const SourceFile& file)
{
    return midiFormat(file.data) != nullptr && deviceFor(file.data) ? kProbeScore : 0;
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
