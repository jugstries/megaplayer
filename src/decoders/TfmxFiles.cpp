#include "TfmxFiles.h"

#include "AudioTags.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace mp {

namespace {

uint32_t le32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

bool equalIgnoringCase(const uint8_t* text, std::string_view expected)
{
    for (size_t i = 0; i < expected.size(); ++i) {
        if (std::tolower(text[i]) != std::tolower(static_cast<unsigned char>(expected[i])))
            return false;
    }
    return true;
}

// TFMX-MOD: "TFMX-MOD", inizio dei campioni e inizio delle informazioni
// (little endian, dall'inizio del file), 4 byte a zero, poi lo mdat. Le
// informazioni sono voci tipo (1 byte), lunghezza (2 byte), valore: 1 autore,
// 2 gioco, 5 sottobrano, 6 titolo.
std::optional<TfmxPair> unpackTfmxMod(const std::vector<uint8_t>& data)
{
    constexpr size_t kHeader = 20;
    if (data.size() < kHeader || std::memcmp(data.data(), "TFMX-MOD", 8) != 0)
        return std::nullopt;
    const size_t samples = le32(&data[8]);
    const size_t info = le32(&data[12]);
    if (samples < kHeader || samples > info || info > data.size())
        return std::nullopt;

    TfmxPair pair;
    pair.song.assign(data.begin() + kHeader, data.begin() + samples);
    pair.samples.assign(data.begin() + samples, data.begin() + info);
    std::string game;
    for (size_t pos = info; pos + 3 <= data.size();) {
        const int type = data[pos];
        const size_t length = data[pos + 1] | (data[pos + 2] << 8);
        pos += 3;
        if (type == 0 || length > data.size() - pos)
            break;
        const std::string value = textToUtf8(std::string_view(reinterpret_cast<const char*>(&data[pos]), length));
        switch (type) {
        case 1: pair.author = value; break;
        case 2: game = value; break;
        case 5:
            if (length == 1)
                pair.subsong = data[pos];
            break;
        case 6: pair.title = value; break;
        }
        pos += length;
    }
    // Il titolo da solo ("Level Complete") dice poco: si aggiunge il gioco.
    if (!game.empty())
        pair.title = pair.title.empty() ? game : game + " - " + pair.title;
    return isTfmxSong(pair.song) ? std::optional(std::move(pair)) : std::nullopt;
}

// TFMXPAK: "TFMXPAK <lunghezza mdat> <lunghezza smpl> >>>" in testo, poi i
// due file uno dopo l'altro.
std::optional<TfmxPair> unpackTfmxPak(const std::vector<uint8_t>& data)
{
    if (data.size() < 16 || std::memcmp(data.data(), "TFMXPAK ", 8) != 0)
        return std::nullopt;
    const size_t limit = std::min<size_t>(data.size(), 64);
    const std::string header(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(limit));
    const size_t marker = header.find(">>>");
    if (marker == std::string::npos)
        return std::nullopt;
    size_t songLength = 0;
    size_t samplesLength = 0;
    if (std::sscanf(header.c_str() + 8, "%zu %zu", &songLength, &samplesLength) != 2)
        return std::nullopt;
    const size_t start = marker + 3;
    if (songLength > data.size() - start || samplesLength > data.size() - start - songLength)
        return std::nullopt;

    TfmxPair pair;
    pair.song.assign(data.begin() + start, data.begin() + start + songLength);
    pair.samples.assign(data.begin() + start + songLength, data.begin() + start + songLength + samplesLength);
    return isTfmxSong(pair.song) ? std::optional(std::move(pair)) : std::nullopt;
}

} // namespace

bool isTfmxSong(const std::vector<uint8_t>& data)
{
    if (data.size() < 10 || !equalIgnoringCase(data.data(), "tfmx"))
        return false;
    const uint8_t* rest = &data[4];
    return std::memcmp(rest, "-SONG", 5) == 0 || std::memcmp(rest, "_SONG ", 6) == 0
        || equalIgnoringCase(rest, "song") || rest[0] == ' ';
}

std::optional<TfmxPair> unpackTfmxContainer(const std::vector<uint8_t>& data)
{
    if (auto pair = unpackTfmxMod(data))
        return pair;
    return unpackTfmxPak(data);
}

std::vector<std::string> pairedFileNames(const std::string& name)
{
    std::vector<std::string> names{name};
    auto add = [&](std::string candidate) {
        if (std::find(names.begin(), names.end(), candidate) == names.end())
            names.push_back(std::move(candidate));
    };
    if (const size_t dot = name.find('.'); dot != std::string::npos && dot > 0) {
        const std::string prefix = name.substr(0, dot);
        const std::string rest = name.substr(dot + 1);
        add(rest + "." + prefix);
        if (lowercase(prefix) == "smpl")
            add(rest + ".sam");
    }
    if (const size_t dot = name.rfind('.'); dot != std::string::npos && dot + 1 < name.size()) {
        const std::string suffix = name.substr(dot + 1);
        const std::string rest = name.substr(0, dot);
        add(suffix + "." + rest);
        if (lowercase(suffix) == "smpl")
            add(rest + ".sam");
    }
    return names;
}

bool isSampleHalf(const std::filesystem::path& path)
{
    const std::filesystem::path directory = path.parent_path();
    const std::string name = path.filename().string();
    auto exists = [&](const std::string& sibling) {
        std::error_code error;
        return std::filesystem::is_regular_file(directory / sibling, error);
    };
    if (lowercase(path.extension().string()) == ".sam") {
        const std::string stem = path.stem().string();
        return exists(stem + ".tfx") || exists(stem + ".TFX");
    }
    if (lowercase(name).rfind("smpl.", 0) == 0)
        return exists("mdat." + name.substr(5)) || exists("MDAT." + name.substr(5));
    return false;
}

} // namespace mp
