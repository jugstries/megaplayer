#include "core/DecoderRegistry.h"

#include "MiniaudioDecoder.h"
#include "XaDecoder.h"

#ifdef MP_WITH_88EMU
#include "Emu88Decoder.h"
#endif
#ifdef MP_WITH_ADLMIDI
#include "AdlMidiDecoder.h"
#endif
#ifdef MP_WITH_FLUIDSYNTH
#include "FluidSynthDecoder.h"
#endif
#ifdef MP_WITH_ADPLUG
#include "AdplugDecoder.h"
#endif
#ifdef MP_WITH_ATARI
#include "AtariAudioDecoder.h"
#endif
#ifdef MP_WITH_PSF
#include "Psf2Decoder.h"
#include "PsfDecoder.h"
#endif
#ifdef MP_WITH_SSF
#include "SsfDecoder.h"
#endif
#ifdef MP_WITH_QSF
#include "QsfDecoder.h"
#endif
#ifdef MP_WITH_2SF
#include "TwosfDecoder.h"
#endif
#ifdef MP_WITH_GSF
#include "GsfDecoder.h"
#endif
#ifdef MP_WITH_USF
#include "UsfDecoder.h"
#endif
#ifdef MP_WITH_VGMSTREAM
#include "VgmstreamDecoder.h"
#endif
#ifdef MP_WITH_GME
#include "GmeDecoder.h"
#endif
#ifdef MP_WITH_OPENMPT
#include "OpenMptDecoder.h"
#endif
#ifdef MP_WITH_SID
#include "SidDecoder.h"
#endif
#ifdef MP_WITH_UADE
#include "UadeDecoder.h"
#endif

namespace mp {

void registerBuiltinDecoders(DecoderRegistry& registry)
{
    registry.add(std::make_unique<MiniaudioDecoderFactory>());
    registry.add(std::make_unique<XaDecoderFactory>());
#ifdef MP_WITH_GME
    registry.add(std::make_unique<GmeDecoderFactory>());
#endif
#ifdef MP_WITH_SID
    registry.add(std::make_unique<SidDecoderFactory>());
#endif
#ifdef MP_WITH_ATARI
    registry.add(std::make_unique<AtariAudioDecoderFactory>());
#endif
#ifdef MP_WITH_PSF
    registry.add(std::make_unique<PsfDecoderFactory>());
    registry.add(std::make_unique<Psf2DecoderFactory>());
#endif
#ifdef MP_WITH_SSF
    registry.add(std::make_unique<SsfDecoderFactory>());
#endif
#ifdef MP_WITH_QSF
    registry.add(std::make_unique<QsfDecoderFactory>());
#endif
#ifdef MP_WITH_2SF
    registry.add(std::make_unique<TwosfDecoderFactory>());
#endif
#ifdef MP_WITH_GSF
    registry.add(std::make_unique<GsfDecoderFactory>());
#endif
#ifdef MP_WITH_USF
    registry.add(std::make_unique<UsfDecoderFactory>());
#endif
#ifdef MP_WITH_VGMSTREAM
    registry.add(std::make_unique<VgmstreamDecoderFactory>());
#endif
#ifdef MP_WITH_88EMU
    registry.add(std::make_unique<Emu88DecoderFactory>());
#endif
#ifdef MP_WITH_FLUIDSYNTH
    registry.add(std::make_unique<FluidSynthDecoderFactory>());
#endif
#ifdef MP_WITH_ADLMIDI
    registry.add(std::make_unique<AdlMidiDecoderFactory>());
#endif
#ifdef MP_WITH_ADPLUG
    registry.add(std::make_unique<AdplugDecoderFactory>());
#endif
#ifdef MP_WITH_OPENMPT
    registry.add(std::make_unique<OpenMptDecoderFactory>());
#endif
#ifdef MP_WITH_UADE
    registry.add(std::make_unique<UadeDecoderFactory>());
#endif
}

void setUadeDataDirectory(const std::string& path)
{
#ifdef MP_WITH_UADE
    UadeDecoderFactory::setDataDirectory(path);
#else
    (void)path;
#endif
}

void addRolandRomDirectory(const std::string& path)
{
#ifdef MP_WITH_88EMU
    emu88::addRomDirectory(path);
#else
    (void)path;
#endif
}

void setRolandDevice(const std::string& name)
{
#ifdef MP_WITH_88EMU
    emu88::setPreferredDevice(name);
#else
    (void)name;
#endif
}

std::vector<RolandDevice> rolandDevices()
{
#ifdef MP_WITH_88EMU
    return emu88::devices();
#else
    return {};
#endif
}

void setSoundFont(const std::string& path)
{
#ifdef MP_WITH_FLUIDSYNTH
    fluidsynth::setSoundFont(path);
#else
    (void)path;
#endif
}

std::string soundFont()
{
#ifdef MP_WITH_FLUIDSYNTH
    return fluidsynth::soundFont();
#else
    return {};
#endif
}

} // namespace mp
