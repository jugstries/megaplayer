#include "megaplayer.h"

#include "core/DecoderRegistry.h"
#include "core/Player.h"
#include "core/SourceFile.h"
#include "core/TrackList.h"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

struct mp_engine {
    explicit mp_engine(int sampleRate)
        : player(sampleRate)
    {
        mp::registerBuiltinDecoders(registry);
    }

    // L'ordine conta: il player chiude il decoder prima che il collector
    // cancelli i file estratti dagli archivi.
    mp::DecoderRegistry registry;
    mp::TrackCollector collector{registry};
    mp::Player player;

    std::string error;
    std::string backendsJson;
    std::string collectJson;
    std::string infoJson;

    std::string currentFile;
    std::vector<std::string> currentPackers;
};

namespace {

// I nomi dei moduli Amiga sono spesso in Latin-1: il JSON vuole UTF-8
// valido, quindi le sequenze non valide vengono lette come Latin-1.
std::string toUtf8(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool valid = length > 0 && i + length <= text.size();
        for (size_t k = 1; valid && k < length; ++k)
            valid = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
        if (valid) {
            out.append(text, i, length);
            i += length;
        } else {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
            ++i;
        }
    }
    return out;
}

std::string jsonString(const std::string& text)
{
    std::string out = "\"";
    for (const char ch : toUtf8(text)) {
        const auto c = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (c < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
            out += escaped;
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

const char* fail(mp_engine* engine, const std::exception& e)
{
    engine->error = e.what();
    return nullptr;
}

} // namespace

extern "C" {

void mp_set_uade_data_dir(const char* path)
{
    mp::setUadeDataDirectory(path ? path : "");
}

mp_engine* mp_engine_create(int32_t sample_rate)
{
    try {
        return new mp_engine(sample_rate);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "megaplayer: %s\n", e.what());
        return nullptr;
    }
}

void mp_engine_destroy(mp_engine* engine)
{
    delete engine;
}

const char* mp_engine_last_error(mp_engine* engine)
{
    return engine->error.c_str();
}

const char* mp_engine_backends(mp_engine* engine)
{
    std::string json = "[";
    for (const auto& factory : engine->registry.factories()) {
        if (json.size() > 1)
            json += ',';
        json += "{\"name\":" + jsonString(std::string(factory->name()))
            + ",\"description\":" + jsonString(std::string(factory->description())) + "}";
    }
    engine->backendsJson = json + "]";
    return engine->backendsJson.c_str();
}

const char* mp_engine_collect(mp_engine* engine, const char* path)
{
    try {
        std::string json = "[";
        for (const mp::Track& track : engine->collector.collect(path)) {
            if (json.size() > 1)
                json += ',';
            json += "{\"path\":" + jsonString(track.path.string()) + ",\"name\":" + jsonString(track.name)
                + ",\"archive\":" + (track.fromArchive ? "true" : "false") + "}";
        }
        engine->collectJson = json + "]";
        return engine->collectJson.c_str();
    } catch (const std::exception& e) {
        return fail(engine, e);
    }
}

int32_t mp_engine_play(mp_engine* engine, const char* path)
{
    try {
        engine->player.stop();
        const mp::SourceFile source = mp::loadSourceFile(path);
        engine->player.play(engine->registry.open(source, {engine->player.sampleRate()}));
        engine->currentFile = source.fileName();
        engine->currentPackers = source.packers;
        return 0;
    } catch (const std::exception& e) {
        fail(engine, e);
        return -1;
    }
}

void mp_engine_stop(mp_engine* engine)
{
    engine->player.stop();
}

void mp_engine_set_paused(mp_engine* engine, int32_t paused)
{
    engine->player.setPaused(paused != 0);
}

void mp_engine_seek(mp_engine* engine, double seconds)
{
    try {
        engine->player.seek(seconds);
    } catch (const std::exception& e) {
        fail(engine, e);
    }
}

void mp_engine_select_subsong(mp_engine* engine, int32_t index)
{
    try {
        engine->player.selectSubsong(index);
    } catch (const std::exception& e) {
        fail(engine, e);
    }
}

void mp_engine_set_volume(mp_engine* engine, float volume)
{
    engine->player.setVolume(volume);
}

void mp_engine_status(mp_engine* engine, mp_status* out)
{
    const mp::PlayerStatus status = engine->player.status();
    out->state = static_cast<int32_t>(status.state);
    out->subsong = status.subsong;
    out->subsong_count = status.subsongCount;
    out->reserved = 0;
    out->position = status.position;
    out->duration = status.duration.value_or(-1.0);
}

const char* mp_engine_track_info(mp_engine* engine)
{
    const mp::TrackInfo info = engine->player.info();
    std::string packers = "[";
    for (const std::string& packer : engine->currentPackers)
        packers += (packers.size() > 1 ? "," : "") + jsonString(packer);
    packers += "]";

    char duration[32] = "null";
    if (info.duration)
        std::snprintf(duration, sizeof duration, "%.3f", *info.duration);

    engine->infoJson = "{\"file\":" + jsonString(engine->currentFile) + ",\"packers\":" + packers
        + ",\"backend\":" + jsonString(info.backend) + ",\"format\":" + jsonString(info.format)
        + ",\"player\":" + jsonString(info.player) + ",\"title\":" + jsonString(info.title)
        + ",\"author\":" + jsonString(info.author) + ",\"subsongCount\":" + std::to_string(info.subsongCount)
        + ",\"currentSubsong\":" + std::to_string(info.currentSubsong) + ",\"duration\":" + duration + "}";
    return engine->infoJson.c_str();
}

int32_t mp_engine_scope(mp_engine* engine, float* out, int32_t frames)
{
    if (frames <= 0)
        return 0;
    return static_cast<int32_t>(engine->player.scope(out, static_cast<size_t>(frames)));
}

} // extern "C"
