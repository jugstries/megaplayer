#include "ShiftJis.h"

#include <cstdint>

namespace mp {

namespace {

// I caratteri a due byte: [byte iniziale][secondo byte], 0 se non c'è.
const uint16_t kDoubleByte[60][188] = {
#include "ShiftJisTable.inc"
};

int leadIndex(uint8_t c)
{
    if (c >= 0x81 && c <= 0x9F)
        return c - 0x81;
    if (c >= 0xE0 && c <= 0xFC)
        return c - 0xE0 + 31;
    return -1;
}

int trailIndex(uint8_t c)
{
    if (c >= 0x40 && c <= 0x7E)
        return c - 0x40;
    if (c >= 0x80 && c <= 0xFC)
        return c - 0x80 + 63;
    return -1;
}

void appendUtf8(std::string& out, uint32_t code)
{
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | code >> 6);
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | code >> 12);
        out += static_cast<char>(0x80 | (code >> 6 & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

} // namespace

std::string shiftJisToUtf8(std::string_view text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<uint8_t>(text[i]);
        if (c == 0)
            break;
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c >= 0xA1 && c <= 0xDF) {
            appendUtf8(out, 0xFF61 + (c - 0xA1)); // katakana a mezza larghezza
        } else if (const int lead = leadIndex(c); lead >= 0 && i + 1 < text.size()) {
            const int trail = trailIndex(static_cast<uint8_t>(text[i + 1]));
            const uint16_t code = trail >= 0 ? kDoubleByte[lead][trail] : 0;
            if (code) {
                appendUtf8(out, code);
                ++i;
            } else {
                out += '?';
            }
        } else {
            out += '?';
        }
    }
    return out;
}

} // namespace mp
