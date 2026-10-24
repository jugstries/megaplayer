#include "core/DecoderRegistry.h"

#include "MiniaudioDecoder.h"

#ifdef MP_WITH_ADLMIDI
#include "AdlMidiDecoder.h"
#endif
#ifdef MP_WITH_ADPLUG
#include "AdplugDecoder.h"
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
#ifdef MP_WITH_GME
    registry.add(std::make_unique<GmeDecoderFactory>());
#endif
#ifdef MP_WITH_SID
    registry.add(std::make_unique<SidDecoderFactory>());
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

} // namespace mp
