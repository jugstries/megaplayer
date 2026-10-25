#include "Mp3Writer.h"

#include <lame.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace mp {

namespace {

// Il testo in UTF-16 con il BOM, terminato da zero, come lo vuole LAME per
// i tag ID3v2: così titoli con accenti o in giapponese restano giusti.
std::vector<unsigned short> utf16(const std::string& text)
{
    std::vector<unsigned short> out = {0xFEFF};
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        const size_t length = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (i + length > text.size())
            break;
        uint32_t code = length == 1 ? c : c & (0x7Fu >> length);
        for (size_t k = 1; k < length; ++k)
            code = code << 6 | (static_cast<unsigned char>(text[i + k]) & 0x3Fu);
        if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<unsigned short>(0xD800 + (code >> 10)));
            out.push_back(static_cast<unsigned short>(0xDC00 + (code & 0x3FF)));
        } else {
            out.push_back(static_cast<unsigned short>(code));
        }
        i += length;
    }
    out.push_back(0);
    return out;
}

void tag(lame_t lame, const char* frame, const std::string& text)
{
    if (!text.empty())
        id3tag_set_textinfo_utf16(lame, frame, utf16(text).data());
}

} // namespace

Mp3Writer::Mp3Writer(const std::filesystem::path& path, int sampleRate, const FileTags& tags)
    : lame_(lame_init())
{
    if (lame_ == nullptr)
        throw std::runtime_error("impossibile avviare l'encoder MP3");
    lame_set_in_samplerate(lame_, sampleRate);
    lame_set_num_channels(lame_, 2);
    lame_set_VBR(lame_, vbr_default);
    lame_set_VBR_quality(lame_, 0);
    id3tag_init(lame_);
    id3tag_add_v2(lame_);
    tag(lame_, "TIT2", tags.title);
    tag(lame_, "TPE1", tags.artist);
    tag(lame_, "TALB", tags.album);
    tag(lame_, "TSSE", "megaplayer");
    // L'ID3v2 lo scriviamo noi, per sapere dove comincia l'audio.
    lame_set_write_id3tag_automatic(lame_, 0);
    if (lame_init_params(lame_) < 0) {
        lame_close(lame_);
        throw std::runtime_error("parametri MP3 non validi per " + std::to_string(sampleRate) + " Hz");
    }
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        lame_close(lame_);
        throw std::runtime_error("impossibile creare " + path.string());
    }
    std::vector<unsigned char> id3v2(lame_get_id3v2_tag(lame_, nullptr, 0));
    put(id3v2.data(), lame_get_id3v2_tag(lame_, id3v2.data(), id3v2.size()));
    firstFrame_ = file_.tellp();
}

Mp3Writer::~Mp3Writer()
{
    if (lame_ != nullptr)
        lame_close(lame_);
}

void Mp3Writer::write(const float* frames, size_t count)
{
    // La dimensione che LAME raccomanda per il caso peggiore.
    buffer_.resize(count * 5 / 4 + 7200);
    const int encoded = lame_encode_buffer_interleaved_ieee_float(lame_, frames, static_cast<int>(count),
                                                                  buffer_.data(), static_cast<int>(buffer_.size()));
    if (encoded < 0)
        throw std::runtime_error("errore dell'encoder MP3 (" + std::to_string(encoded) + ")");
    put(buffer_.data(), static_cast<size_t>(encoded));
}

void Mp3Writer::finish()
{
    if (finished_)
        return;
    finished_ = true;
    buffer_.resize(7200);
    const int flushed = lame_encode_flush(lame_, buffer_.data(), static_cast<int>(buffer_.size()));
    if (flushed > 0)
        put(buffer_.data(), static_cast<size_t>(flushed));
    std::vector<unsigned char> id3v1(128);
    put(id3v1.data(), lame_get_id3v1_tag(lame_, id3v1.data(), id3v1.size()));
    // Il tag LAME (Xing) con frame e byte totali va nella prima frame.
    std::vector<unsigned char> lametag(lame_get_lametag_frame(lame_, nullptr, 0));
    const size_t size = lame_get_lametag_frame(lame_, lametag.data(), lametag.size());
    if (size > 0) {
        file_.seekp(firstFrame_);
        put(lametag.data(), size);
    }
    file_.close();
    if (!file_)
        throw std::runtime_error("errore di scrittura del file MP3");
}

void Mp3Writer::put(const unsigned char* data, size_t size)
{
    file_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file_)
        throw std::runtime_error("errore di scrittura del file MP3");
}

} // namespace mp
