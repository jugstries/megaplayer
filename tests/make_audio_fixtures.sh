#!/usr/bin/env bash
#
# Rigenera i file audio di prova in tests/data: un secondo di sinusoidi, 440 Hz
# a sinistra e 660 Hz a destra, con titolo e autore, in WAV, MP3 (tag ID3v2.3
# in UTF-16), FLAC e Ogg Vorbis per miniaudio, e in M4A (AAC e ALAC), Opus,
# AIFF, WMA e WavPack per il backend ffmpeg, a frequenze diverse per provare
# anche il ricampionamento. Serve ffmpeg con libmp3lame, libvorbis e libopus.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/data"

tone=(-f lavfi -i "sine=frequency=440:duration=1:sample_rate=44100"
      -f lavfi -i "sine=frequency=660:duration=1:sample_rate=44100"
      -filter_complex "[0][1]amerge=inputs=2")
tags=(-metadata "title=Prova àèì" -metadata "artist=Megaplayer")

ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -ar 8000 -c:a pcm_u8 tono.wav
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -c:a libmp3lame -b:a 48k -id3v2_version 3 tono.mp3
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -ar 22050 -c:a flac tono.flac
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -ar 32000 -c:a libvorbis -q:a 0 tono.ogg
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -c:a aac -b:a 64k tono.m4a
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -ar 22050 -c:a alac tono-alac.m4a
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -c:a libopus -b:a 32k tono.opus
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -ar 11025 -c:a pcm_s16be -write_id3v2 1 tono.aiff
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -ar 32000 -c:a wmav2 -b:a 64k tono.wma
ffmpeg -loglevel error -y "${tone[@]}" "${tags[@]}" -ar 16000 -c:a wavpack tono.wv
