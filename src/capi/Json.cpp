#include "Json.h"

#include <cstdio>

namespace mp::json {

namespace {

std::string toUtf8(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool valid = length > 0 && i + length <= text.size();
        for (size_t k = 1; valid && k < length; ++k)
            valid = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
        if (valid) {
            out.append(text, i, length);
            i += length;
        } else {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
            ++i;
        }
    }
    return out;
}

} // namespace

std::string string(const std::string& text)
{
    std::string out = "\"";
    for (const char ch : toUtf8(text)) {
        const auto c = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (c < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
            out += escaped;
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

std::string number(std::optional<double> value)
{
    if (!value)
        return "null";
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", *value);
    return text;
}

} // namespace mp::json
