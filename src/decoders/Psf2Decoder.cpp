#include "Psf2Decoder.h"

#include "AudioTags.h"
#include "HighlyExperimental.h"
#include "Xsf.h"
#include "core/Platform.h"

#include <iop.h>
#include <psf2fs.h>
#include <psx.h>
#include <r3000.h>
#include <spu.h>

#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// I moduli IOP di src/ps2/irx, incorporati da CMake nell'ordine in cui
// loadcore li avvia (cmake/EmbedBinary.cmake, mp_embed_binaries).
extern "C" {
struct mp_embedded_file {
    const char* name;
    const unsigned char* data;
    unsigned long size;
};
extern const struct mp_embedded_file mp_iop_modules[];
extern const unsigned long mp_iop_modules_count;
}

namespace mp {

namespace {

constexpr int kProbeScore = 80;
// Lo SPU2 suona a 48000 Hz (768 cicli dell'IOP per campione).
constexpr int kNativeRate = 48000;
constexpr int kVoices = 48; // 24 per ciascuno dei due core
constexpr uint32_t kRamSize = 0x200000;

// La ROM: al reset il registro di controllo della cache come lo lascia la
// BIOS (Highly Experimental lo legge per sapere se la cache è isolata), poi
// un salto su sé stesso, dove si ferma l'avvio; dopo, i moduli in fila.
constexpr uint32_t kRomSize = 512 * 1024;
constexpr uint32_t kRomBase = 0xBFC00000;
constexpr uint32_t kFirstModule = 0x100;
constexpr uint32_t kResetCode[] = {
    0x3C090001, // lui  t1, 0x0001
    0x3529EDD8, // ori  t1, t1, 0xEDD8
    0x3C01FFFE, // lui  at, 0xFFFE
    0xAC290130, // sw   t1, 0x130(at)
    0x1000FFFF, // b    .           (kPs2BootStop)
    0x00000000, // nop
};
static_assert(sizeof(kResetCode) == kPs2BootStop + 8, "il salto su sé stesso è a kPs2BootStop");

// Dove IOPBOOT mette le cose: sysmem a 0x800 (il codice dopo i 0x30 byte di
// ModuleInfo), i parametri per loadcore a 0x20000. A 0x400 un salto su sé
// stesso, per sapere quando una funzione chiamata dall'host torna.
constexpr uint32_t kSysmemText = 0x830;
constexpr uint32_t kBootParams = 0x20000;
constexpr uint32_t kReturnTrap = 0x400;

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

uint16_t le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

void putLe32(uint8_t* p, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(value >> (8 * i));
}

// I file che l'IOP apre da hefile:, dal filesystem virtuale del brano.
sint32 EMU_CALL readVirtualFile(void* filesystem, const char* path, sint32 offset, char* buffer, sint32 length)
{
    return psf2fs_virtual_readfile(filesystem, path, offset, buffer, length);
}

struct Rom {
    std::vector<uint8_t> image;
    std::vector<uint32_t> modules; // indirizzi nella ROM, nell'ordine di avvio
};

// La ROM è la stessa per tutti i brani: si fa una volta sola.
const Rom& rom()
{
    static Rom built;
    static std::once_flag once;
    std::call_once(once, [] {
        built.image.assign(kRomSize, 0);
        built.modules.clear();
        for (size_t i = 0; i < std::size(kResetCode); ++i)
            putLe32(&built.image[4 * i], kResetCode[i]);
        uint32_t position = kFirstModule;
        for (unsigned long i = 0; i < mp_iop_modules_count; ++i) {
            const mp_embedded_file& module = mp_iop_modules[i];
            if (position + module.size > kRomSize)
                throw DecoderError("i moduli IOP non stanno nella ROM");
            std::memcpy(&built.image[position], module.data, module.size);
            built.modules.push_back(kRomBase + position);
            position = (position + static_cast<uint32_t>(module.size) + 15) & ~15u;
        }
    });
    return built;
}

// La RAM dell'IOP vista dall'host, per fare il lavoro di IOPBOOT.
class IopMemory {
public:
    explicit IopMemory(void* iop)
        : iop_(iop)
    {
    }

    uint32_t read(uint32_t address) const { return iop_getword(iop_, address & (kRamSize - 4)); }
    void write(uint32_t address, uint32_t value) { iop_setword(iop_, address & (kRamSize - 4), value); }

private:
    void* iop_;
};

// Le informazioni di un modulo IRX (.iopmod), relative al suo inizio.
struct IrxInfo {
    uint32_t moduleId = 0;
    uint32_t entry = 0;
    uint32_t gp = 0;
    uint32_t textSize = 0;
    uint32_t dataSize = 0;
    uint32_t bssSize = 0;
    uint16_t version = 0;
    uint32_t memorySize = 0;
};

// Carica un modulo IRX dalla ROM in RAM, con il codice a base, e applica le
// rilocazioni come ApplyElfRelSection di loadcore.
IrxInfo loadIrx(IopMemory& ram, uint32_t romAddress, uint32_t base)
{
    const std::vector<uint8_t>& image = rom().image;
    const uint8_t* elf = &image[romAddress - kRomBase];
    const size_t available = image.size() - (romAddress - kRomBase);
    auto fail = [] { return DecoderError("modulo IOP non valido nella ROM"); };
    if (available < 52 || std::memcmp(elf, "\x7F" "ELF", 4) != 0 || le16(elf + 16) != 0xFF80)
        throw fail();
    const uint32_t programHeaders = le32(elf + 28);
    const uint32_t sectionHeaders = le32(elf + 32);
    const uint16_t programCount = le16(elf + 44);
    const uint16_t sectionCount = le16(elf + 48);
    if (programCount != 2 || programHeaders + 64 > available || sectionHeaders + 40u * sectionCount > available)
        throw fail();

    IrxInfo info;
    const uint32_t iopmodOffset = le32(elf + programHeaders + 4);
    if (iopmodOffset + 26 > available)
        throw fail();
    const uint8_t* iopmod = elf + iopmodOffset;
    info.moduleId = le32(iopmod);
    info.entry = le32(iopmod + 4);
    info.gp = le32(iopmod + 8);
    info.textSize = le32(iopmod + 12);
    info.dataSize = le32(iopmod + 16);
    info.bssSize = le32(iopmod + 20);
    info.version = le16(iopmod + 24);

    const uint8_t* load = elf + programHeaders + 32;
    const uint32_t offset = le32(load + 4);
    const uint32_t fileSize = le32(load + 16);
    info.memorySize = le32(load + 20);
    if (offset + fileSize > available || fileSize > info.memorySize)
        throw fail();
    for (uint32_t i = 0; i < info.memorySize; i += 4)
        ram.write(base + i, i < fileSize ? le32(elf + offset + i) : 0);

    for (uint16_t s = 0; s < sectionCount; ++s) {
        const uint8_t* section = elf + sectionHeaders + 40u * s;
        if (le32(section + 4) != 9) // SHT_REL
            continue;
        const uint32_t relocations = le32(section + 16);
        const uint32_t count = le32(section + 20) / 8;
        if (relocations + 8u * count > available)
            throw fail();
        const uint8_t* relocation = elf + relocations;
        for (uint32_t r = 0; r < count; ++r) {
            const uint32_t where = le32(relocation + 8 * r);
            const uint32_t type = le32(relocation + 8 * r + 4) & 0xFF;
            const uint32_t address = base + where;
            const uint32_t word = ram.read(address);
            switch (type) {
            case 2: // R_MIPS_32
                ram.write(address, word + base);
                break;
            case 4: { // R_MIPS_26
                const uint32_t target = base + ((where & 0xF0000000) | (4 * (word & 0x3FFFFFF)));
                ram.write(address, (word & 0xFC000000) | (target << 4 >> 6));
                break;
            }
            case 5: { // R_MIPS_HI16, con la LO16 che la segue
                if (r + 1 >= count)
                    throw fail();
                const uint32_t low = ram.read(base + le32(relocation + 8 * (r + 1)));
                const uint32_t target = base + static_cast<uint32_t>(static_cast<int16_t>(low)) + (word << 16);
                ram.write(address, (word & 0xFFFF0000) | (((target >> 15) + 1) >> 1 & 0xFFFF));
                break;
            }
            case 6: // R_MIPS_LO16
                ram.write(address, (word & 0xFFFF0000) | ((base + word) & 0xFFFF));
                break;
            default:
                throw fail();
            }
        }
    }
    return info;
}

// L'intestazione (ModuleInfo) che IOPBOOT scrive 0x30 byte prima del codice.
void writeModuleInfo(IopMemory& ram, uint32_t base, const IrxInfo& info)
{
    const uint32_t header = base - 0x30;
    const uint32_t name = info.moduleId != 0xFFFFFFFF ? ram.read(base + info.moduleId) : 0;
    const uint32_t words[12] = {0, name, info.version, 0, base + info.entry, base + info.gp, base,
                                info.textSize, info.dataSize, info.bssSize, 0, 0};
    for (int i = 0; i < 12; ++i)
        ram.write(header + 4 * i, words[i]);
}

class Psf2Decoder final : public XsfDecoder {
public:
    Psf2Decoder(const SourceFile& file, const OutputSpec& spec)
        : Psf2Decoder(spec, load(file))
    {
    }

private:
    using Filesystem = std::unique_ptr<void, void (*)(void*)>;

    struct Loaded {
        XsfTags tags;
        Filesystem filesystem{nullptr, psf2fs_delete};
    };

    // I file del brano e delle sue librerie in un filesystem virtuale, che
    // l'IOP legge da hefile:.
    static Loaded load(const SourceFile& file)
    {
        initHighlyExperimental();
        rom();
        Loaded loaded;
        loaded.filesystem.reset(psf2fs_create());
        if (!loaded.filesystem)
            throw DecoderError("memoria esaurita");
        void* filesystem = loaded.filesystem.get();
        loadXsf(
            file, 2,
            [&](const uint8_t* exe, size_t size, const uint8_t* reserved, size_t reservedSize) {
                return psf2fs_load_callback(filesystem, exe, size, reserved, reservedSize) == 0;
            },
            loaded.tags);
        char none;
        if (psf2fs_virtual_readfile(filesystem, "/psf2.irx", 0, &none, 0) <= 0)
            throw DecoderError("PSF2 senza psf2.irx");
        return loaded;
    }

    Psf2Decoder(const OutputSpec& spec, Loaded loaded)
        : XsfDecoder(spec, kNativeRate, loaded.tags)
        , loaded_(std::move(loaded))
        , state_(new uint8_t[psx_get_state_size(2)])
    {
        info_.backend = "psf2";
        info_.platform = platform::kPlayStation2;
        info_.format = loaded_.tags.hasLibrary ? "MiniPSF2" : "PSF2";
        info_.player = "Highly Experimental";
        info_.title = textToUtf8(loaded_.tags.title.value_or(""));
        info_.author = textToUtf8(loaded_.tags.artist.value_or(""));
        for (int voice = 1; voice <= kVoices; ++voice)
            channelNames_.push_back("Voce " + std::to_string(voice));
        start();
    }

    // L'IOP da capo: la ROM fino al suo salto su sé stesso, poi il lavoro di
    // IOPBOOT, e loadcore che avvia i moduli fino a hefile:/psf2.irx.
    void reset() override
    {
        const Rom& image = rom();
        psx_clear_state_bios(state_.get(), 2, const_cast<uint8*>(image.image.data()), kRomSize);
        psx_set_readfile(state_.get(), readVirtualFile, loaded_.filesystem.get());
        void* iop = psx_get_iop_state(state_.get());
        IopMemory ram(iop);

        // sysmem, avviato con la dimensione della RAM.
        const IrxInfo sysmem = loadIrx(ram, image.modules.at(0), kSysmemText);
        writeModuleInfo(ram, kSysmemText, sysmem);
        callIop(kSysmemText + sysmem.entry, kRamSize, kSysmemText + sysmem.gp);

        // loadcore subito dopo la tabella dei blocchi di sysmem, che sysmem
        // mette nel primo blocco da 256 byte dopo la sua fine.
        const uint32_t table = (kSysmemText + sysmem.memorySize + 0x103) & ~0xFFu;
        const uint32_t loadcoreText = table + 0x100 + 0x30;
        const IrxInfo loadcore = loadIrx(ram, image.modules.at(1), loadcoreText);
        writeModuleInfo(ram, loadcoreText, loadcore);

        // I parametri: la RAM in MB, la tabella esportata da sysmem (all'inizio
        // del suo codice), e i moduli da avviare, tutti dalla ROM.
        const uint32_t moduleList = kBootParams + 0x20;
        const uint32_t count = static_cast<uint32_t>(image.modules.size());
        for (uint32_t i = 0; i < count; ++i)
            ram.write(moduleList + 4 * i, image.modules[i]);
        ram.write(moduleList + 4 * count, 0);
        const uint32_t params[8] = {kRamSize >> 20, 0, 0, kSysmemText, 0, 0, count, moduleList};
        for (int i = 0; i < 8; ++i)
            ram.write(kBootParams + 4 * i, params[i]);
        void* cpu = iop_get_r3000_state(iop);
        r3000_setreg(cpu, R3000_REG_PC, loadcoreText + loadcore.entry);
        r3000_setreg(cpu, R3000_REG_GEN + 4, kBootParams);
        applyMutes();
    }

    // Esegue una funzione dell'IOP fino al suo ritorno.
    void callIop(uint32_t address, uint32_t argument, uint32_t gp)
    {
        void* iop = psx_get_iop_state(state_.get());
        void* cpu = iop_get_r3000_state(iop);
        iop_setword(iop, kReturnTrap, 0x1000FFFF); // b .
        iop_setword(iop, kReturnTrap + 4, 0);
        r3000_setreg(cpu, R3000_REG_PC, address);
        r3000_setreg(cpu, R3000_REG_GEN + 4, argument);
        r3000_setreg(cpu, R3000_REG_GEN + 28, gp);
        r3000_setreg(cpu, R3000_REG_GEN + 29, kRamSize - 0x100);
        r3000_setreg(cpu, R3000_REG_GEN + 31, kReturnTrap);
        for (int i = 0; i < 100000; ++i) {
            uint32 samples = 1 << 20;
            if (psx_execute(state_.get(), 100, nullptr, &samples, 0) < 0)
                break;
            if ((r3000_getreg(cpu, R3000_REG_PC) & 0x1FFFFFFF) == kReturnTrap)
                return;
        }
        throw DecoderError("l'avvio dell'IOP non torna");
    }

    size_t emulate(int16_t* buffer, size_t frames) override
    {
        size_t done = 0;
        while (done < frames) {
            uint32 got = static_cast<uint32>(frames - done);
            const sint32 result = psx_execute(state_.get(), 0x7FFFFFFF, buffer + done * kChannels, &got, 0);
            done += got;
            if (result < 0)
                break;
        }
        return done;
    }

    void applyMutes() override
    {
        void* spu = iop_get_spu_state(psx_get_iop_state(state_.get()));
        for (int voice = 0; voice < kVoices; ++voice)
            spu_enable_mute(spu, static_cast<uint8>(voice), (mutedMask() >> voice) & 1);
    }

    Loaded loaded_;
    std::unique_ptr<uint8_t[]> state_;
};

} // namespace

std::string_view Psf2DecoderFactory::description() const
{
    return "Highly Experimental: PlayStation 2 (PSF2, MiniPSF2) con i moduli liberi di ps2sdk";
}

int Psf2DecoderFactory::probe(const SourceFile& file)
{
    return isXsf(file, 2) ? kProbeScore : 0;
}

std::unique_ptr<Decoder> Psf2DecoderFactory::open(const SourceFile& file, const OutputSpec& spec)
{
    return std::make_unique<Psf2Decoder>(file, spec);
}

} // namespace mp
