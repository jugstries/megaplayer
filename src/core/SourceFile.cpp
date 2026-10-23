#include "SourceFile.h"

#include <ancient/ancient.hpp>

#include <fstream>
#include <stdexcept>

namespace mp {

namespace {

// Alcuni file sono compressi più volte (es. XPK dentro PowerPacker).
constexpr int kMaxUnpackDepth = 4;

std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("impossibile aprire " + path.string());

    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(data.data()), size))
        throw std::runtime_error("errore di lettura su " + path.string());
    return data;
}

} // namespace

SourceFile loadSourceFile(const std::filesystem::path& path)
{
    SourceFile file;
    file.path = path;
    file.data = readFile(path);

    for (int depth = 0; depth < kMaxUnpackDepth; ++depth) {
        if (!ancient::Decompressor::detect(file.data))
            break;
        try {
            ancient::Decompressor decompressor(file.data, true, true);
            std::vector<uint8_t> unpacked = decompressor.decompress(true);
            file.packers.push_back(decompressor.getName());
            file.data = std::move(unpacked);
        } catch (const ancient::Error&) {
            // La firma somigliava a quella di un packer ma i dati non lo
            // sono: capita con i moduli, il cui header inizia con testo
            // libero. Si tiene il file così com'è.
            break;
        }
    }
    return file;
}

} // namespace mp
