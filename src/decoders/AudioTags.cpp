#include "AudioTags.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace mp {

namespace {

uint32_t le32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

uint32_t syncsafe(const uint8_t* p)
{
    return ((p[0] & 0x7f) << 21) | ((p[1] & 0x7f) << 14) | ((p[2] & 0x7f) << 7) | (p[3] & 0x7f);
}

void appendUtf8(std::string& out, uint32_t c)
{
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xc0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xe0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
}

bool validUtf8(std::string_view s)
{
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<uint8_t>(s[i]);
        const size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (length == 0 || i + length > s.size())
            return false;
        for (size_t k = 1; k < length; ++k) {
            if ((static_cast<uint8_t>(s[i + k]) >> 6) != 2)
                return false;
        }
        i += length;
    }
    return true;
}

std::string utf16(const uint8_t* p, size_t size, bool bigEndian)
{
    if (size >= 2 && ((p[0] == 0xff && p[1] == 0xfe) || (p[0] == 0xfe && p[1] == 0xff))) {
        bigEndian = p[0] == 0xfe;
        p += 2;
        size -= 2;
    }
    std::string out;
    for (size_t i = 0; i + 1 < size; i += 2) {
        uint32_t c = bigEndian ? (p[i] << 8) | p[i + 1] : p[i] | (p[i + 1] << 8);
        if (c == 0)
            break;
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < size) {
            const uint32_t low = bigEndian ? (p[i + 2] << 8) | p[i + 3] : p[i + 2] | (p[i + 3] << 8);
            c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
            i += 2;
        }
        appendUtf8(out, c);
    }
    return out;
}

// Commenti Vorbis: fornitore, poi coppie CHIAVE=valore, lunghezze little
// endian.
AudioTags vorbisComments(const uint8_t* p, size_t size)
{
    AudioTags tags;
    if (size < 8)
        return tags;
    size_t pos = 4 + static_cast<size_t>(le32(p));
    if (pos + 4 > size)
        return tags;
    const uint32_t count = le32(p + pos);
    pos += 4;
    for (uint32_t i = 0; i < count && pos + 4 <= size; ++i) {
        const size_t length = le32(p + pos);
        pos += 4;
        if (length > size - pos)
            break;
        const std::string_view comment(reinterpret_cast<const char*>(p + pos), length);
        pos += length;
        const size_t equal = comment.find('=');
        if (equal == std::string_view::npos)
            continue;
        std::string key(comment.substr(0, equal));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::toupper(c); });
        const std::string value = textToUtf8(comment.substr(equal + 1));
        if (key == "TITLE" && tags.title.empty())
            tags.title = value;
        else if (key == "ARTIST" && tags.artist.empty())
            tags.artist = value;
    }
    return tags;
}

} // namespace

std::string textToUtf8(std::string_view text)
{
    text = text.substr(0, text.find('\0'));
    if (validUtf8(text))
        return std::string(text);
    std::string out;
    for (char c : text)
        appendUtf8(out, static_cast<uint8_t>(c));
    return out;
}

size_t id3TagSize(const std::vector<uint8_t>& data)
{
    if (data.size() < 10 || data[0] != 'I' || data[1] != 'D' || data[2] != '3')
        return 0;
    const bool footer = data[3] == 4 && (data[5] & 0x10);
    return std::min(data.size(), 10 + static_cast<size_t>(syncsafe(&data[6])) + (footer ? 10 : 0));
}

AudioTags readId3Tags(const std::vector<uint8_t>& data)
{
    AudioTags tags;
    const size_t end = id3TagSize(data);
    if (end == 0)
        return tags;
    const int version = data[3];
    const bool shortFrames = version == 2;
    size_t pos = 10;
    if ((data[5] & 0x40) && version >= 3 && pos + 4 <= end) // header esteso
        pos += version == 4 ? syncsafe(&data[pos]) : 4 + be32(&data[pos]);
    const size_t headerSize = shortFrames ? 6 : 10;
    while (pos + headerSize <= end && data[pos] != 0) {
        const std::string_view id(reinterpret_cast<const char*>(&data[pos]), shortFrames ? 3 : 4);
        const size_t size = shortFrames ? (data[pos + 3] << 16) | (data[pos + 4] << 8) | data[pos + 5]
                            : version == 4 ? syncsafe(&data[pos + 4])
                                           : be32(&data[pos + 4]);
        pos += headerSize;
        if (size > end - pos)
            break;
        const bool title = id == "TIT2" || id == "TT2";
        const bool artist = id == "TPE1" || id == "TP1";
        if ((title || artist) && size > 1) {
            const uint8_t* text = &data[pos + 1];
            const size_t length = size - 1;
            std::string value;
            switch (data[pos]) {
            case 1: value = utf16(text, length, false); break;
            case 2: value = utf16(text, length, true); break;
            default: value = textToUtf8(std::string_view(reinterpret_cast<const char*>(text), length)); break;
            }
            (title ? tags.title : tags.artist) = value;
        }
        pos += size;
    }
    return tags;
}

AudioTags readFlacTags(const std::vector<uint8_t>& data)
{
    size_t pos = id3TagSize(data);
    if (pos + 4 > data.size() || std::string_view(reinterpret_cast<const char*>(&data[pos]), 4) != "fLaC")
        return {};
    pos += 4;
    // Blocchi di metadati: tipo (il bit alto segna l'ultimo) e lunghezza a
    // 24 bit big endian. Il 4 sono i commenti Vorbis.
    while (pos + 4 <= data.size()) {
        const uint8_t type = data[pos];
        const size_t length = (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
        pos += 4;
        if (length > data.size() - pos)
            break;
        if ((type & 0x7f) == 4)
            return vorbisComments(&data[pos], length);
        if (type & 0x80)
            break;
        pos += length;
    }
    return {};
}

AudioTags readOggVorbisTags(const std::vector<uint8_t>& data)
{
    // Rimette insieme i pacchetti dalle pagine Ogg: il secondo sono i
    // commenti, preceduti da "\x03vorbis".
    std::vector<uint8_t> packet;
    int packets = 0;
    size_t pos = 0;
    while (pos + 27 <= data.size() && std::string_view(reinterpret_cast<const char*>(&data[pos]), 4) == "OggS") {
        const size_t segments = data[pos + 26];
        if (pos + 27 + segments > data.size())
            break;
        const uint8_t* table = &data[pos + 27];
        size_t body = pos + 27 + segments;
        for (size_t s = 0; s < segments; ++s) {
            const size_t length = table[s];
            if (body + length > data.size())
                return {};
            if (packets == 1)
                packet.insert(packet.end(), data.begin() + body, data.begin() + body + length);
            body += length;
            if (length < 255 && ++packets == 2) {
                if (packet.size() > 7 && packet[0] == 3 && std::string_view(reinterpret_cast<char*>(&packet[1]), 6) == "vorbis")
                    return vorbisComments(&packet[7], packet.size() - 7);
                return {};
            }
        }
        pos = body;
    }
    return {};
}

AudioTags readWavTags(const std::vector<uint8_t>& data)
{
    AudioTags tags;
    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const std::string_view id(reinterpret_cast<const char*>(&data[pos]), 4);
        const size_t size = le32(&data[pos + 4]);
        const size_t body = pos + 8;
        if (size > data.size() - body)
            break;
        if (id == "LIST" && size >= 4 && std::string_view(reinterpret_cast<const char*>(&data[body]), 4) == "INFO") {
            for (size_t sub = body + 4; sub + 8 <= body + size;) {
                const std::string_view subId(reinterpret_cast<const char*>(&data[sub]), 4);
                const size_t subSize = le32(&data[sub + 4]);
                if (subSize > body + size - sub - 8)
                    break;
                const std::string_view value(reinterpret_cast<const char*>(&data[sub + 8]), subSize);
                if (subId == "INAM")
                    tags.title = textToUtf8(value);
                else if (subId == "IART")
                    tags.artist = textToUtf8(value);
                sub += 8 + subSize + (subSize & 1);
            }
        }
        pos = body + size + (size & 1);
    }
    return tags;
}

} // namespace mp
