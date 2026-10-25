#include "RolandDevices.h"

namespace mp {

bool talksToLa(const std::vector<uint8_t>& data)
{
    // Roland (41h), un numero di dispositivo, il modello 16h dell'MT-32 e il
    // comando DT1 (12h), con cui i giochi gli mandano timbri e impostazioni.
    for (size_t i = 0; i + 3 < data.size(); ++i) {
        if (data[i] == 0x41 && data[i + 1] < 0x20 && data[i + 2] == 0x16 && data[i + 3] == 0x12)
            return true;
    }
    return false;
}

std::optional<RolandDevice> chooseRolandDevice(bool la, std::string_view preferred,
                                               const std::vector<RolandDevice>& devices)
{
    if (!preferred.empty()) {
        for (const RolandDevice& device : devices) {
            if (device.available && device.la == la && device.name == preferred)
                return device;
        }
    }
    for (const RolandDevice& device : devices) {
        if (device.available && device.la == la)
            return device;
    }
    for (const RolandDevice& device : devices) {
        if (device.available && !device.la)
            return device;
    }
    return std::nullopt;
}

} // namespace mp
