#include "HighlyExperimental.h"

#include "core/Decoder.h"

#include <bios.h>
#include <psx.h>

#include <mutex>
#include <vector>

// OpenBIOS, incorporata da CMake (cmake/EmbedBinary.cmake).
extern "C" const unsigned char mp_openbios[];
extern "C" const unsigned long mp_openbios_size;

namespace mp {

namespace {

// Dove fermare l'avvio di OpenBIOS: la chiamata a startShell(7), a kernel
// pronto, la stessa dove Highly Experimental ferma la BIOS di Sony. Lì si
// scrive un salto su sé stesso, e dopo si carica il programma del brano.
constexpr uint32_t kShellCall = 0x6FAC;
constexpr uint32_t kJalStartShell = 0x0FF01D57;
constexpr uint32_t kBranchSelf = 0x1000FFFF; // b .

uint32_t le32(const uint8_t* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24;
}

void putLe32(uint8_t* p, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        p[i] = static_cast<uint8_t>(value >> (8 * i));
}

} // namespace

void initHighlyExperimental()
{
    static std::once_flag once;
    std::call_once(once, [] {
        static std::vector<uint8_t> bios(mp_openbios, mp_openbios + mp_openbios_size);
        if (bios.size() != 512 * 1024 || le32(&bios[kShellCall]) != kJalStartShell)
            throw DecoderError("OpenBIOS non è quella attesa");
        putLe32(&bios[kShellCall], kBranchSelf);
        putLe32(&bios[kShellCall + 4], 0); // lo slot del salto: nop
        bios_set_image(bios.data(), static_cast<uint32>(bios.size()));
        psx_set_preboot(kShellCall, kPs2BootStop);
        if (psx_init() != 0)
            throw DecoderError("Highly Experimental non parte");
    });
}

} // namespace mp
