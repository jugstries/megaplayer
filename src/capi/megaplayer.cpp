#include "megaplayer.h"

#include "Json.h"
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
    std::string rolandJson;

    std::string currentFile;
    std::vector<std::string> currentPackers;
};

namespace {

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

void mp_add_roland_rom_dir(const char* path)
{
    mp::addRolandRomDirectory(path ? path : "");
}

void mp_set_roland_device(const char* name)
{
    mp::setRolandDevice(name ? name : "");
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
        json += "{\"name\":" + mp::json::string(std::string(factory->name()))
            + ",\"description\":" + mp::json::string(std::string(factory->description())) + "}";
    }
    engine->backendsJson = json + "]";
    return engine->backendsJson.c_str();
}

const char* mp_engine_roland_devices(mp_engine* engine)
{
    std::string json = "[";
    for (const mp::RolandDevice& device : mp::rolandDevices()) {
        if (json.size() > 1)
            json += ',';
        json += "{\"name\":" + mp::json::string(device.name) + ",\"family\":\"" + (device.la ? "la" : "gs")
            + "\",\"available\":" + (device.available ? "true" : "false") + "}";
    }
    engine->rolandJson = json + "]";
    return engine->rolandJson.c_str();
}

const char* mp_engine_collect(mp_engine* engine, const char* path)
{
    try {
        std::string json = "[";
        for (const mp::Track& track : engine->collector.collect(path)) {
            if (json.size() > 1)
                json += ',';
            json += "{\"path\":" + mp::json::string(track.path.string()) + ",\"ref\":" + mp::json::string(track.ref)
                + ",\"name\":" + mp::json::string(track.name)
                + ",\"archive\":" + (track.fromArchive ? "true" : "false") + "}";
        }
        engine->collectJson = json + "]";
        return engine->collectJson.c_str();
    } catch (const std::exception& e) {
        return fail(engine, e);
    }
}

int32_t mp_engine_play(mp_engine* engine, const char* ref)
{
    try {
        engine->player.stop();
        const mp::SourceFile source = mp::loadSourceFile(engine->collector.resolve(ref));
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

void mp_engine_set_megasound(mp_engine* engine, float amount)
{
    engine->player.setMegaSound(amount);
}

void mp_engine_set_channel_muted(mp_engine* engine, int32_t channel, int32_t muted)
{
    engine->player.setChannelMuted(channel, muted != 0);
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
        packers += (packers.size() > 1 ? "," : "") + mp::json::string(packer);
    packers += "]";
    std::string channels = "[";
    for (const std::string& channel : engine->player.channels())
        channels += (channels.size() > 1 ? "," : "") + mp::json::string(channel);
    channels += "]";

    engine->infoJson = "{\"file\":" + mp::json::string(engine->currentFile) + ",\"packers\":" + packers
        + ",\"backend\":" + mp::json::string(info.backend) + ",\"format\":" + mp::json::string(info.format)
        + ",\"player\":" + mp::json::string(info.player) + ",\"platform\":" + mp::json::string(info.platform)
        + ",\"title\":" + mp::json::string(info.title)
        + ",\"author\":" + mp::json::string(info.author) + ",\"subsongCount\":" + std::to_string(info.subsongCount)
        + ",\"currentSubsong\":" + std::to_string(info.currentSubsong) + ",\"duration\":" + mp::json::number(info.duration)
        + ",\"channels\":" + channels + "}";
    return engine->infoJson.c_str();
}

int32_t mp_engine_scope(mp_engine* engine, float* out, int32_t frames)
{
    if (frames <= 0)
        return 0;
    return static_cast<int32_t>(engine->player.scope(out, static_cast<size_t>(frames)));
}

} // extern "C"
