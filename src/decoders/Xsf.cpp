#include "Xsf.h"

#include "core/SourceFile.h"

#include <psflib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace mp {

namespace {

namespace fs = std::filesystem;

// Senza i tag length e fade: tre minuti, poi cinque secondi di dissolvenza.
constexpr double kDefaultLength = 180;
constexpr double kDefaultFade = 5;
constexpr size_t kBlock = 1024;

std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string toUtf8(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path fromUtf8(const char* text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

// I file per psflib: il brano dalla memoria, le librerie dal disco.
struct OpenFile {
    std::vector<uint8_t> owned;
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t position = 0;
};

struct Files {
    const SourceFile* song;
    std::string songUri;
};

void* openFile(void* context, const char* uri)
{
    const auto* files = static_cast<const Files*>(context);
    auto file = std::make_unique<OpenFile>();
    if (files->songUri == uri) {
        file->data = files->song->data.data();
        file->size = files->song->data.size();
        return file.release();
    }
    fs::path path = fromUtf8(uri);
    std::error_code error;
    if (!fs::is_regular_file(path, error))
        path = findIgnoringCase(path.parent_path(), path.filename().string());
    std::ifstream in(path, std::ios::binary);
    if (path.empty() || !in)
        return nullptr;
    file->owned.assign(std::istreambuf_iterator<char>(in), {});
    file->data = file->owned.data();
    file->size = file->owned.size();
    return file.release();
}

size_t readFile(void* buffer, size_t size, size_t count, void* handle)
{
    auto* file = static_cast<OpenFile*>(handle);
    if (size == 0)
        return 0;
    const size_t items = std::min(count, (file->size - file->position) / size);
    std::memcpy(buffer, file->data + file->position, items * size);
    file->position += items * size;
    return items;
}

int seekFile(void* handle, int64_t offset, int whence)
{
    auto* file = static_cast<OpenFile*>(handle);
    const int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<int64_t>(file->position)
                                                                    : static_cast<int64_t>(file->size);
    const int64_t target = base + offset;
    if (target < 0 || target > static_cast<int64_t>(file->size))
        return -1;
    file->position = static_cast<size_t>(target);
    return 0;
}

int closeFile(void* handle)
{
    delete static_cast<OpenFile*>(handle);
    return 0;
}

long tellFile(void* handle)
{
    return static_cast<long>(static_cast<OpenFile*>(handle)->position);
}

struct LoadState {
    const XsfSection* section;
    XsfTags* tags;
    std::string status;
};

int loadSection(void* context, const uint8_t* exe, size_t size, const uint8_t* reserved, size_t reservedSize)
{
    const auto* state = static_cast<LoadState*>(context);
    return (*state->section)(exe, size, reserved, reservedSize) ? 0 : -1;
}

int readTag(void* context, const char* name, const char* value)
{
    XsfTags& tags = *static_cast<LoadState*>(context)->tags;
    const std::string key = lowercase(name);
    auto keep = [&](std::optional<std::string>& field) {
        if (!field)
            field = value;
    };
    if (key == "title")
        keep(tags.title);
    else if (key == "artist")
        keep(tags.artist);
    else if (key == "length")
        keep(tags.length);
    else if (key == "fade")
        keep(tags.fade);
    else if (key == "volume")
        keep(tags.volume);
    else if (key.rfind("_lib", 0) == 0)
        tags.hasLibrary = true;
    else if (!tags.find(key))
        tags.others.emplace_back(key, value);
    return 0;
}

void addStatus(void* context, const char* message)
{
    static_cast<LoadState*>(context)->status += message;
}

// "3:25.5", "1:02:03" o "95" in secondi.
std::optional<double> parseTime(const std::optional<std::string>& tag)
{
    if (!tag)
        return std::nullopt;
    const std::string_view text = std::string_view(*tag).substr(0, tag->find_first_of("\r\n"));
    double seconds = 0;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = std::min(text.find(':', start), text.size());
        const std::string part(text.substr(start, end - start));
        char* rest = nullptr;
        const double value = std::strtod(part.c_str(), &rest);
        if (rest == part.c_str())
            return std::nullopt;
        seconds = seconds * 60 + value;
        start = end + 1;
    }
    return seconds;
}

} // namespace

const std::string* XsfTags::find(std::string_view name) const
{
    for (const auto& [key, value] : others) {
        if (key == name)
            return &value;
    }
    return nullptr;
}

void loadXsf(const SourceFile& file, uint8_t version, const XsfSection& section, XsfTags& tags)
{
    Files files{&file, toUtf8(file.path)};
    const psf_file_callbacks callbacks = {"\\/", &files, openFile, readFile, seekFile, closeFile, tellFile};
    LoadState state{&section, &tags, {}};
    if (psf_load(files.songUri.c_str(), &callbacks, version, loadSection, &state, readTag, &state, 1, addStatus, &state)
        <= 0) {
        const std::string reason = state.status.substr(0, state.status.find('\n'));
        throw DecoderError(reason.empty() ? "file non valido" : reason);
    }
}

bool isXsf(const SourceFile& file, uint8_t version)
{
    if (file.data.size() < 16 || std::memcmp(file.data.data(), "PSF", 3) != 0 || file.data[3] != version)
        return false;
    const std::string extension = lowercase(file.path.extension().string());
    return extension.size() < 4 || extension.compare(extension.size() - 3, 3, "lib") != 0;
}

XsfDecoder::XsfDecoder(const OutputSpec& spec, int nativeRate, const XsfTags& tags)
    : sampleRate_(spec.sampleRate)
    , nativeRate_(nativeRate)
    , resampler_(nativeRate, spec.sampleRate)
    , block_(kBlock * kChannels)
{
    if (tags.volume) {
        const double volume = std::atof(tags.volume->c_str());
        if (volume > 0)
            gain_ = static_cast<float>(volume);
    }
    const double length = parseTime(tags.length).value_or(kDefaultLength);
    const double fade = std::max(0.0, parseTime(tags.fade).value_or(kDefaultFade));
    fadeStart_ = static_cast<size_t>(length * sampleRate_);
    total_ = fadeStart_ + static_cast<size_t>(fade * sampleRate_);
    info_.duration = static_cast<double>(total_) / sampleRate_;
}

void XsfDecoder::start()
{
    restart();
}

// Questi formati hanno un brano solo.
void XsfDecoder::selectSubsong(int /*index*/)
{
    restart();
}

size_t XsfDecoder::render(float* out, size_t frames)
{
    frames = std::min(frames, total_ - std::min(total_, rendered_));
    const size_t got = resampler_.render(out, frames, [this](float* buffer, size_t count) { return pull(buffer, count); });
    for (size_t i = 0; i < got; ++i) {
        const size_t frame = rendered_ + i;
        float gain = gain_;
        if (frame >= fadeStart_)
            gain *= static_cast<float>(total_ - frame) / static_cast<float>(total_ - fadeStart_);
        out[i * kChannels] *= gain;
        out[i * kChannels + 1] *= gain;
    }
    rendered_ += got;
    return got;
}

// L'emulatore non torna indietro: per un punto già passato si riparte da
// capo; poi si emula fino al punto richiesto, a blocchi come suonando e con i
// campioni, scartati (senza, alcuni chip saltano il riverbero, che poi per un
// paio di secondi suonerebbe diverso).
void XsfDecoder::seek(double seconds)
{
    const auto target =
        static_cast<size_t>(std::clamp(seconds, 0.0, static_cast<double>(total_) / sampleRate_) * sampleRate_);
    const auto nativeTarget = static_cast<uint64_t>(static_cast<double>(target) * nativeRate_ / sampleRate_);
    // L'ultimo blocco è ancora in memoria: si torna indietro solo da lì.
    if (nativeTarget < produced_ - blockLength_)
        restart();
    if (nativeTarget >= produced_) {
        while (produced_ + kBlock <= nativeTarget && !failed_)
            emulateBlock();
        emulateBlock();
    }
    blockPosition_ = static_cast<size_t>(std::min<uint64_t>(nativeTarget - (produced_ - blockLength_), blockLength_));
    resampler_.reset();
    rendered_ = target;
}

double XsfDecoder::position() const
{
    return static_cast<double>(rendered_) / sampleRate_;
}

void XsfDecoder::setChannelMuted(int channel, bool muted)
{
    const uint64_t bit = uint64_t{1} << channel;
    mutedMask_ = muted ? mutedMask_ | bit : mutedMask_ & ~bit;
    applyMutes();
}

void XsfDecoder::restart()
{
    reset();
    resampler_.reset();
    rendered_ = 0;
    produced_ = 0;
    blockLength_ = blockPosition_ = 0;
    failed_ = false;
    info_.currentSubsong = 0;
}

void XsfDecoder::emulateBlock()
{
    const size_t done = failed_ ? 0 : emulate(block_.data(), kBlock);
    failed_ = done < kBlock;
    produced_ += done;
    blockLength_ = done;
    blockPosition_ = 0;
}

// Frame alla frequenza dell'emulatore; meno di frames solo se si è fermato.
size_t XsfDecoder::pull(float* out, size_t frames)
{
    size_t done = 0;
    while (done < frames) {
        if (blockPosition_ == blockLength_) {
            emulateBlock();
            if (blockLength_ == 0)
                break;
        }
        const size_t take = std::min(frames - done, blockLength_ - blockPosition_);
        for (size_t i = 0; i < take * kChannels; ++i)
            out[done * kChannels + i] = block_[blockPosition_ * kChannels + i] / 32768.0f;
        blockPosition_ += take;
        done += take;
    }
    return done;
}

} // namespace mp
