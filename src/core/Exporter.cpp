#include "Exporter.h"

#include "dsp/MegaSound.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <vector>

namespace mp {

namespace {

constexpr size_t kChunkFrames = 4096;

// Quanto esportare al più: il limite chiesto, o la durata annunciata con un
// secondo di margine, o kExportLimit.
double limitOf(const Decoder& decoder, const ExportOptions& options)
{
    if (options.maxSeconds)
        return *options.maxSeconds;
    if (const std::optional<double> duration = decoder.info().duration)
        return *duration + 1;
    return kExportLimit;
}

} // namespace

void exportTrack(Decoder& decoder, const ExportOptions& options, const std::function<void(double)>& progress,
                 const std::function<bool()>& cancelled)
{
    // Spenta, la catena si salta e il file resta identico al bit
    // all'originale.
    MegaSound megaSound(options.sampleRate);
    megaSound.setAmount(options.megaSound);
    megaSound.reset();
    const auto maxFrames = static_cast<size_t>(std::llround(limitOf(decoder, options) * options.sampleRate));

    try {
        std::unique_ptr<AudioFileWriter> writer = createAudioFileWriter(options.output, options.sampleRate, options.tags);
        std::vector<float> chunk(kChunkFrames * kChannels);
        size_t total = 0;
        while (total < maxFrames) {
            if (cancelled && cancelled())
                throw std::runtime_error("esportazione annullata");
            const size_t got = decoder.render(chunk.data(), std::min(kChunkFrames, maxFrames - total));
            if (got == 0)
                break;
            if (megaSound.engaged())
                megaSound.process(chunk.data(), got);
            writer->write(chunk.data(), got);
            total += got;
            if (progress)
                progress(static_cast<double>(total) / options.sampleRate);
        }
        writer->finish();
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(options.output, ignored);
        throw;
    }
}

Exporter::Exporter(std::unique_ptr<Decoder> decoder, ExportOptions options)
    : decoder_(std::move(decoder))
    , options_(std::move(options))
{
    status_.running = true;
    status_.total = options_.maxSeconds ? options_.maxSeconds : decoder_->info().duration;
    thread_ = std::thread([this] {
        try {
            exportTrack(
                *decoder_, options_,
                [this](double seconds) {
                    std::lock_guard lock(mutex_);
                    status_.seconds = seconds;
                },
                [this] { return cancel_.load(); });
            std::lock_guard lock(mutex_);
            status_.running = false;
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex_);
            status_.running = false;
            status_.cancelled = cancel_;
            status_.failed = !cancel_;
            status_.error = e.what();
        }
        // UADE suona un brano per volta: il decoder si chiude subito.
        decoder_.reset();
    });
}

Exporter::~Exporter()
{
    cancel_ = true;
    if (thread_.joinable())
        thread_.join();
}

Exporter::Status Exporter::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

} // namespace mp
