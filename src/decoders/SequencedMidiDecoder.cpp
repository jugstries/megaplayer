#include "SequencedMidiDecoder.h"

#include "AudioTags.h"

// Il sequencer MIDI di libADLMIDI (licenza MIT), compilato qui con un altro
// nome perché non si scontri con la copia dentro libADLMIDI.
#define BW_MidiSequencer MpMidiSequencer
#include "midiseq/midi_sequencer_impl.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace mp {

namespace {

constexpr int kMidiChannels = 16;
// Dopo l'ultimo evento: le note in rilascio e il riverbero.
constexpr double kTail = 2.0;
// Il tempo che un modulo vuole dopo un reset prima di ascoltare il resto.
constexpr double kResetSettle = 0.2;

// GS Reset, GM System On/Off, GM2 On, il cambio di modo dell'SC-88 e l'XG
// Reset: dopo uno di questi conta solo quello che viene dopo.
bool isReset(const std::vector<uint8_t>& sysex)
{
    auto starts = [&](std::initializer_list<int> head, size_t skip) {
        if (sysex.size() < head.size() + 1)
            return false;
        size_t i = 0;
        for (const int byte : head) {
            if (i != skip && sysex[i] != byte)
                return false;
            ++i;
        }
        return true;
    };
    return starts({0xF0, 0x41, 0x00, 0x42, 0x12, 0x40, 0x00, 0x7F}, 2)  // GS Reset
        || starts({0xF0, 0x41, 0x00, 0x42, 0x12, 0x00, 0x00, 0x7F}, 2)  // modo SC-88
        || starts({0xF0, 0x7E, 0x00, 0x09}, 2)                          // GM / GM2
        || starts({0xF0, 0x43, 0x00, 0x4C, 0x00, 0x00, 0x7E, 0x00}, 2); // XG Reset
}

// Un DT1 Roland (F0 41 dispositivo modello 12 indirizzo dati somma F7): la
// chiave che lo distingue da un altro allo stesso indirizzo, o "".
std::string rolandAddress(const std::vector<uint8_t>& sysex)
{
    if (sysex.size() < 10 || sysex[1] != 0x41 || sysex[4] != 0x12)
        return {};
    return std::string(sysex.begin() + 3, sysex.begin() + 8) + char(sysex.size());
}

} // namespace

// Quello che un salto incontra prima del punto d'arrivo, ridotto allo stato
// finale: il sintetizzatore lo riceve tutto insieme arrivati lì, e non le
// migliaia di messaggi del brano fin lì (un modulo emulato, come uno vero, ne
// perderebbe e suonerebbe a caso).
struct SequencedMidiDecoder::SeekState {
    struct Channel {
        int program = -1;
        int controllers[128];
        int bend = -1;
        int pressure = -1;
        // Il parametro RPN o NRPN scelto e i valori dati a ciascuno.
        bool nrpn = false;
        int parameterMsb = 127;
        int parameterLsb = 127;
        std::map<int, std::pair<int, int>> parameters; // (nrpn, msb, lsb) -> (dato msb, dato lsb)

        Channel() { std::fill(std::begin(controllers), std::end(controllers), -1); }
    };
    std::vector<Channel> channels = std::vector<Channel>(32);
    // I SysEx dopo l'ultimo reset; dei DT1 Roland conta l'ultimo per
    // indirizzo.
    std::vector<std::vector<uint8_t>> sysex;
    std::vector<bool> resets;
};

struct SequencedMidiDecoder::Sequence {
    // Non "interface": su Windows è una macro.
    BW_MidiRtInterface events{};
    MpMidiSequencer sequencer;
    std::vector<uint8_t> sysex;
    // Le porte dei file per più moduli: ogni nome nuovo è la successiva.
    std::map<std::string, size_t> portNames;
    std::map<size_t, size_t> portOfTrack;
};

namespace {

SequencedMidiDecoder& self(void* userdata)
{
    return *static_cast<SequencedMidiDecoder*>(userdata);
}

} // namespace

SequencedMidiDecoder::SequencedMidiDecoder(const SourceFile& file, const OutputSpec& spec, TrackInfo info)
    : sampleRate_(spec.sampleRate)
    , data_(file.data)
    , info_(std::move(info))
    , sequence_(std::make_unique<Sequence>())
{
    // I callback del sequencer: i messaggi vanno al sintetizzatore, le note
    // dei canali zittiti no. Il canale arriva già spostato di 16 per porta.
    BW_MidiRtInterface& events = sequence_->events;
    events.rtUserData = this;
    events.rt_noteOn = [](void* userdata, uint8_t channel, uint8_t note, uint8_t velocity) {
        SequencedMidiDecoder& decoder = self(userdata);
        if (!decoder.muted_[channel % kMidiChannels] && !decoder.seekState_)
            decoder.send(channel, 0x90u | note << 8 | velocity << 16);
    };
    // Durante un salto le note non suonano, e i note-off non servono.
    events.rt_noteOff = [](void* userdata, uint8_t channel, uint8_t note) {
        if (!self(userdata).seekState_)
            self(userdata).send(channel, 0x80u | note << 8);
    };
    events.rt_noteOffVel = [](void* userdata, uint8_t channel, uint8_t note, uint8_t velocity) {
        if (!self(userdata).seekState_)
            self(userdata).send(channel, 0x80u | note << 8 | velocity << 16);
    };
    events.rt_noteAfterTouch = [](void* userdata, uint8_t channel, uint8_t note, uint8_t value) {
        self(userdata).send(channel, 0xA0u | note << 8 | value << 16);
    };
    events.rt_channelAfterTouch = [](void* userdata, uint8_t channel, uint8_t value) {
        self(userdata).send(channel, 0xD0u | value << 8);
    };
    events.rt_controllerChange = [](void* userdata, uint8_t channel, uint8_t type, uint8_t value) {
        self(userdata).send(channel, 0xB0u | type << 8 | value << 16);
    };
    events.rt_patchChange = [](void* userdata, uint8_t channel, uint8_t patch) {
        self(userdata).send(channel, 0xC0u | patch << 8);
    };
    events.rt_pitchBend = [](void* userdata, uint8_t channel, uint8_t msb, uint8_t lsb) {
        self(userdata).send(channel, 0xE0u | lsb << 8 | msb << 16);
    };
    // Il SysEx completo, F0 ... F7, anche se il file lo tiene senza.
    events.rt_systemExclusive = [](void* userdata, const uint8_t* message, size_t size) {
        SequencedMidiDecoder& decoder = self(userdata);
        std::vector<uint8_t>& sysex = decoder.sequence_->sysex;
        sysex.clear();
        if (size == 0 || message[0] != 0xF0)
            sysex.push_back(0xF0);
        sysex.insert(sysex.end(), message, message + size);
        if (sysex.back() != 0xF7)
            sysex.push_back(0xF7);
        if (decoder.seekState_)
            decoder.recordSystemExclusive(sysex);
        else
            decoder.systemExclusive(0, sysex.data(), sysex.size());
    };
    // Il meta evento "porta": ogni nome nuovo è la porta successiva, come in
    // libADLMIDI.
    events.rt_deviceSwitch = [](void* userdata, size_t track, const char* data, size_t length) {
        Sequence& sequence = *self(userdata).sequence_;
        const std::string name(data, length);
        const auto known = sequence.portNames.find(name);
        const size_t port = known != sequence.portNames.end() ? known->second : sequence.portNames.size();
        sequence.portNames.emplace(name, port);
        sequence.portOfTrack[track] = port;
    };
    events.rt_currentDevice = [](void* userdata, size_t track) -> size_t {
        const Sequence& sequence = *self(userdata).sequence_;
        const auto port = sequence.portOfTrack.find(track);
        return port == sequence.portOfTrack.end() ? 0 : port->second * kMidiChannels;
    };
    events.pcmSampleRate = static_cast<uint32_t>(sampleRate_);
    events.pcmFrameSize = 2 * sizeof(float);
    sequence_->sequencer.setInterface(&events);
    sequence_->sequencer.setLoopEnabled(false);

    load(0);
    info_.subsongCount = std::max(1, sequence_->sequencer.getSongsCount());
}

SequencedMidiDecoder::~SequencedMidiDecoder() = default;

// Gli XMI possono avere più brani: il sequencer li sceglie prima di leggere
// il file.
void SequencedMidiDecoder::selectSubsong(int index)
{
    load(index);
    if (started_)
        reset();
}

size_t SequencedMidiDecoder::render(float* out, size_t frames)
{
    ensureStarted();
    MpMidiSequencer& sequencer = sequence_->sequencer;
    const double granularity = 1.0 / sampleRate_;
    size_t done = 0;
    while (done < frames) {
        if (!ended_ && untilEvent_ < 0.5 / sampleRate_) {
            untilEvent_ = sequencer.Tick(sinceTick_, granularity);
            sinceTick_ = 0;
            ended_ = sequencer.positionAtEnd();
        }
        size_t count = frames - done;
        if (ended_) {
            count = std::min(count, tailLeft_);
            if (count == 0)
                break;
            tailLeft_ -= count;
        } else {
            // Fino al prossimo evento, che il sintetizzatore riceve al primo
            // campione dopo.
            const double toEvent = std::ceil(untilEvent_ * sampleRate_ - 1e-6);
            count = std::min(count, static_cast<size_t>(std::max(1.0, toEvent)));
        }
        synthesize(out + done * kChannels, count);
        const double seconds = static_cast<double>(count) / sampleRate_;
        untilEvent_ -= seconds;
        sinceTick_ += seconds;
        done += count;
    }
    rendered_ += done;
    return done;
}

// Il sequencer riparte dall'inizio e manda tutto tranne le note, così
// programmi, controller e SysEx sono quelli giusti; suonano le note che
// partono da lì in poi.
void SequencedMidiDecoder::seek(double seconds)
{
    ensureStarted();
    // Il sintetizzatore come appena acceso: lo stato giusto è quello del
    // brano fin qui, non quello di dove si era.
    reset();
    MpMidiSequencer& sequencer = sequence_->sequencer;
    const double length = sequencer.timeLength();
    seconds = std::clamp(seconds, 0.0, length + kTail);
    if (seconds >= length) {
        ended_ = true;
        tailLeft_ = static_cast<size_t>((length + kTail - seconds) * sampleRate_);
    } else {
        seekState_ = std::make_unique<SeekState>();
        untilEvent_ = sequencer.seek(seconds, 1.0 / sampleRate_);
        replaySeekState();
        seekState_.reset();
        sinceTick_ = 0;
        ended_ = sequencer.positionAtEnd();
        tailLeft_ = static_cast<size_t>(kTail * sampleRate_);
    }
    rendered_ = static_cast<size_t>(seconds * sampleRate_);
}

double SequencedMidiDecoder::position() const
{
    return static_cast<double>(rendered_) / sampleRate_;
}

// I sedici canali MIDI; il decimo è la batteria (anche sull'MT-32, dove i
// canali dal 2 al 9 sono le sue parti).
std::vector<std::string> SequencedMidiDecoder::channels() const
{
    std::vector<std::string> names;
    for (int channel = 0; channel < kMidiChannels; ++channel)
        names.push_back("Canale " + std::to_string(channel + 1) + (channel == 9 ? " · batteria" : ""));
    return names;
}

// Zittito, un canale non riceve più note e quelle accese si spengono, su
// tutte le porte; riacceso, suona dalla nota dopo.
void SequencedMidiDecoder::setChannelMuted(int channel, bool muted)
{
    muted_[channel] = muted;
    if (!muted || !started_)
        return;
    for (unsigned port = 0; port < ports(); ++port) {
        const uint32_t status = 0xB0 | static_cast<uint32_t>(channel);
        for (const uint32_t controller : {64u << 8, 123u << 8, 120u << 8}) // pedale, note, suoni
            shortMessage(port, status | controller);
    }
}

void SequencedMidiDecoder::load(int song)
{
    MpMidiSequencer& sequencer = sequence_->sequencer;
    sequencer.setSongNum(song);
    if (!sequencer.loadMIDI(data_.data(), data_.size()))
        throw DecoderError(info_.backend + ": " + sequencer.getErrorString());
    info_.currentSubsong = song;
    if (const char* title = sequencer.getMusicTitle(); title != nullptr && *title != '\0')
        info_.title = textToUtf8(title);
    info_.duration = sequencer.timeLength() + kTail;
    untilEvent_ = 0;
    sinceTick_ = 0;
    ended_ = false;
    tailLeft_ = static_cast<size_t>(kTail * sampleRate_);
    rendered_ = 0;
    sequence_->portOfTrack.clear();
    sequence_->portNames.clear();
}

void SequencedMidiDecoder::ensureStarted()
{
    if (started_)
        return;
    start();
    started_ = true;
}

void SequencedMidiDecoder::send(uint8_t channel, uint32_t message)
{
    if (seekState_) {
        record(channel, message);
        return;
    }
    const unsigned port = (channel / kMidiChannels) % std::max(1u, ports());
    shortMessage(port, message | (channel % kMidiChannels));
}

void SequencedMidiDecoder::record(uint8_t channel, uint32_t message)
{
    if (channel >= seekState_->channels.size())
        return;
    SeekState::Channel& state = seekState_->channels[channel];
    const int first = static_cast<int>((message >> 8) & 0x7F);
    const int second = static_cast<int>((message >> 16) & 0x7F);
    switch (message & 0xF0) {
    case 0xC0: state.program = first; break;
    case 0xD0: state.pressure = first; break;
    case 0xE0: state.bend = first | second << 7; break;
    case 0xB0:
        switch (first) {
        case 99: case 98: case 101: case 100: // scelta del parametro NRPN o RPN
            state.nrpn = first == 99 || first == 98;
            (first == 99 || first == 101 ? state.parameterMsb : state.parameterLsb) = second;
            break;
        case 6: case 38: { // il valore del parametro scelto
            if (state.parameterMsb == 127 && state.parameterLsb == 127)
                break;
            auto& value = state.parameters[state.nrpn << 14 | state.parameterMsb << 7 | state.parameterLsb];
            (first == 6 ? value.first : value.second) = second;
            break;
        }
        case 121: // reset dei controller, come dopo un reset del modulo
            for (const int controller : {1, 11, 64, 65, 66, 67})
                state.controllers[controller] = -1;
            state.bend = -1;
            state.pressure = -1;
            break;
        default:
            if (first < 120)
                state.controllers[first] = second;
            break;
        }
        break;
    default: break;
    }
}

void SequencedMidiDecoder::recordSystemExclusive(const std::vector<uint8_t>& sysex)
{
    SeekState& state = *seekState_;
    if (isReset(sysex)) {
        // Quello che c'era prima il reset l'ha cancellato.
        state = SeekState();
        state.sysex.push_back(sysex);
        state.resets.push_back(true);
        return;
    }
    if (const std::string address = rolandAddress(sysex); !address.empty()) {
        for (size_t i = 0; i < state.sysex.size(); ++i) {
            if (!state.resets[i] && rolandAddress(state.sysex[i]) == address) {
                state.sysex.erase(state.sysex.begin() + static_cast<std::ptrdiff_t>(i));
                state.resets.erase(state.resets.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    state.sysex.push_back(sysex);
    state.resets.push_back(false);
}

// Lo stato del brano al punto d'arrivo: i SysEx nell'ordine in cui sono
// venuti, poi per ogni canale banco e programma, i controller, i parametri
// RPN e NRPN, pitch bend e pressione. Al ritmo che il sintetizzatore regge.
void SequencedMidiDecoder::replaySeekState()
{
    std::unique_ptr<SeekState> state = std::move(seekState_);
    for (size_t i = 0; i < state->sysex.size(); ++i) {
        systemExclusive(0, state->sysex[i].data(), state->sysex[i].size());
        pace(state->sysex[i].size(), state->resets[i] ? kResetSettle : 0);
    }
    for (size_t channel = 0; channel < state->channels.size(); ++channel) {
        const SeekState::Channel& values = state->channels[channel];
        const unsigned port = static_cast<unsigned>(channel / kMidiChannels);
        if (port >= ports())
            break;
        const uint32_t number = static_cast<uint32_t>(channel % kMidiChannels);
        size_t bytes = 0;
        auto message = [&](uint32_t status, int first, int second) {
            shortMessage(port, status | number | static_cast<uint32_t>(first) << 8 | static_cast<uint32_t>(second) << 16);
            bytes += 3;
        };
        for (const int bank : {0, 32}) {
            if (values.controllers[bank] >= 0)
                message(0xB0, bank, values.controllers[bank]);
        }
        if (values.program >= 0)
            message(0xC0, values.program, 0);
        for (int controller = 1; controller < 120; ++controller) {
            const bool parameter = controller == 6 || controller == 38 || (controller >= 96 && controller <= 101);
            if (controller != 32 && !parameter && values.controllers[controller] >= 0)
                message(0xB0, controller, values.controllers[controller]);
        }
        for (const auto& [key, value] : values.parameters) {
            const bool nrpn = key >> 14;
            message(0xB0, nrpn ? 99 : 101, (key >> 7) & 0x7F);
            message(0xB0, nrpn ? 98 : 100, key & 0x7F);
            message(0xB0, 6, value.first);
            message(0xB0, 38, value.second);
        }
        if (!values.parameters.empty()) {
            message(0xB0, 101, 127); // nessun parametro scelto
            message(0xB0, 100, 127);
        }
        if (values.bend >= 0)
            message(0xE0, values.bend & 0x7F, values.bend >> 7);
        if (values.pressure >= 0)
            message(0xD0, values.pressure, 0);
        if (bytes > 0)
            pace(bytes, 0);
    }
}

} // namespace mp
