#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mp {

struct AudioTags {
    std::string title;
    std::string artist;
};

// Titolo e autore dai tag dei file audio: ID3v2 (MP3), commenti Vorbis (FLAC
// e Ogg Vorbis) e chunk LIST/INFO (WAV). Quello che manca resta vuoto.
AudioTags readId3Tags(const std::vector<uint8_t>& data);
AudioTags readFlacTags(const std::vector<uint8_t>& data);
AudioTags readOggVorbisTags(const std::vector<uint8_t>& data);
AudioTags readWavTags(const std::vector<uint8_t>& data);

// Lunghezza del tag ID3v2 all'inizio del file, 0 se non c'è.
size_t id3TagSize(const std::vector<uint8_t>& data);

// Testo a 8 bit in UTF-8: com'è se lo è già, altrimenti letto come Latin-1.
// Si ferma al primo carattere nullo.
std::string textToUtf8(std::string_view text);

} // namespace mp
