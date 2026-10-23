#include "core/DecoderRegistry.h"

#ifdef MP_WITH_OPENMPT
#include "OpenMptDecoder.h"
#endif
#ifdef MP_WITH_UADE
#include "UadeDecoder.h"
#endif

namespace mp {

void registerBuiltinDecoders(DecoderRegistry& registry)
{
#ifdef MP_WITH_OPENMPT
    registry.add(std::make_unique<OpenMptDecoderFactory>());
#endif
#ifdef MP_WITH_UADE
    registry.add(std::make_unique<UadeDecoderFactory>());
#endif
    (void)registry;
}

} // namespace mp
