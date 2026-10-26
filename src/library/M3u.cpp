#include "M3u.h"

#include "decoders/AudioTags.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace mp {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kSubsongTag = "#MEGAPLAYER-SUBSONG:";

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size()
        && std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
           });
}

// "http://...", "smb://...": un indirizzo, non un file.
bool isUrl(std::string_view line)
{
    const size_t separator = line.find("://");
    if (separator == std::string_view::npos || separator < 2 || !std::isalpha(static_cast<unsigned char>(line[0])))
        return false;
    return std::all_of(line.begin(), line.begin() + static_cast<std::ptrdiff_t>(separator), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
    });
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// "file:///Users/...", "file://localhost/...", "file:///C:/...".
std::string fileUrlPath(std::string_view url)
{
    url.remove_prefix(7);
    if (startsWithNoCase(url, "localhost/"))
        url.remove_prefix(9);
    std::string path;
    for (size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '%' && i + 2 < url.size() && hexDigit(url[i + 1]) >= 0 && hexDigit(url[i + 2]) >= 0) {
            path += static_cast<char>(hexDigit(url[i + 1]) * 16 + hexDigit(url[i + 2]));
            i += 2;
        } else {
            path += url[i];
        }
    }
#ifdef _WIN32
    if (path.size() > 2 && path[0] == '/' && path[2] == ':')
        path.erase(0, 1);
#endif
    return path;
}

// Titoli e nomi stanno su una riga.
std::string oneLine(std::string text)
{
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '\r' || c == '\n'; }, ' ');
    return text;
}

// Relativo se il file sta dentro `folder`, altrimenti completo.
std::string pathFor(const std::string& path, const fs::path& folder)
{
    const fs::path relative = fs::path(path).lexically_normal().lexically_relative(folder.lexically_normal());
    if (relative.empty() || *relative.begin() == "..")
        return path;
    return relative.string();
}

} // namespace

M3uPlaylist parseM3u(std::string_view raw, const fs::path& folder)
{
    if (raw.substr(0, 3) == "\xEF\xBB\xBF")
        raw.remove_prefix(3);
    const std::string text = textToUtf8(raw);

    M3uPlaylist playlist;
    M3uEntry pending;
    size_t start = 0;
    while (start < text.size()) {
        const size_t end = std::min(text.find('\n', start), text.size());
        const std::string_view line = trim(std::string_view(text).substr(start, end - start));
        start = end + 1;
        if (line.empty())
            continue;

        if (line[0] == '#') {
            if (startsWithNoCase(line, "#EXTINF:")) {
                // "#EXTINF:123,Autore - Titolo"; la durata -1 è sconosciuta.
                const std::string_view info = line.substr(8);
                const size_t comma = info.find(',');
                const std::string seconds(info.substr(0, comma));
                char* parsed = nullptr;
                const double value = std::strtod(seconds.c_str(), &parsed);
                pending.duration = parsed != seconds.c_str() && value >= 0 ? std::optional(value) : std::nullopt;
                pending.title = comma == std::string_view::npos ? "" : std::string(trim(info.substr(comma + 1)));
            } else if (startsWithNoCase(line, "#PLAYLIST:")) {
                playlist.name = trim(line.substr(10));
            } else if (startsWithNoCase(line, kSubsongTag)) {
                const int number = std::atoi(std::string(line.substr(kSubsongTag.size())).c_str());
                pending.subsong = number > 0 ? std::optional(number - 1) : std::nullopt;
            }
            continue;
        }

        std::string path;
        if (startsWithNoCase(line, "file://")) {
            path = fileUrlPath(line);
        } else if (isUrl(line)) {
            ++playlist.skipped;
            pending = {};
            continue;
        } else {
            path = line;
#ifndef _WIN32
            // Un M3U scritto su Windows.
            std::replace(path.begin(), path.end(), '\\', '/');
#endif
        }
        fs::path resolved(path);
        if (resolved.is_relative())
            resolved = folder / resolved;
        pending.path = resolved.lexically_normal().string();
        playlist.entries.push_back(std::move(pending));
        pending = {};
    }
    return playlist;
}

std::string formatM3u(const M3uPlaylist& playlist, const fs::path& folder)
{
    std::string text = "#EXTM3U\n";
    if (!playlist.name.empty())
        text += "#PLAYLIST:" + oneLine(playlist.name) + "\n";
    for (const M3uEntry& entry : playlist.entries) {
        const long seconds = entry.duration ? std::lround(*entry.duration) : -1;
        text += "#EXTINF:" + std::to_string(seconds) + "," + oneLine(entry.title) + "\n";
        if (entry.subsong)
            text += std::string(kSubsongTag) + std::to_string(*entry.subsong + 1) + "\n";
        text += pathFor(entry.path, folder) + "\n";
    }
    return text;
}

} // namespace mp
