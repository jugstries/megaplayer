"""Genera gli stream audio di prova per vgmstream: due GENH, un .int, un VAG
e uno SVAG.

Uso: make_test_streams.py DIR

In DIR scrive:
- test.genh: PS-ADPCM stereo a 44,1 kHz con l'interleave di 0x800 byte dei
  rip della PS2, e un loop che parte a metà di un frame. Ogni canale è
  un'onda quadra (sinistra ±0,5, destra ±0,25) che nel ritornello cambia
  periodo; i campioni ADPCM hanno filtro 0 e spostamento 0, quindi i valori
  decodificati sono esatti e il test li confronta uno per uno;
- test-pcm.genh: PCM16 little endian mono a 22,05 kHz, senza loop, con
  l'intestazione corta dei GENH più vecchi: un secondo di seno a 1 kHz;
- test.int: PCM16 little endian senza intestazione, come i .int della PS2
  (stereo, interleave 0x200, 48 kHz per convenzione): due secondi, seno a
  440 Hz a sinistra e a 880 Hz a destra;
- test.vag: il VAG mono dell'SDK di Sony (intestazione "VAGp" big endian),
  due secondi a 22,05 kHz, con il loop segnato dai flag dei frame PS-ADPCM
  (6 all'inizio, 3 alla fine) come li scrivono i suoi strumenti;
- test.svag: lo SVAG stereo dei giochi Konami (Silent Hill 2, OZ), 32 kHz,
  interleave 0x800, col loop nell'intestazione.

Serve ai test (CMakeLists.txt, tests/VgmstreamDecoderTest.cpp), così nel
repository non ci sono brani con copyright.
"""
import math
import os
import struct
import sys

# --- test.genh ---

GENH_RATE = 44100
INTERLEAVE = 0x800
SAMPLES_PER_BLOCK = INTERLEAVE // 16 * 28  # 3584
LOOP_START = 44110                         # a metà di un frame da 28
LOOP_END = 37 * SAMPLES_PER_BLOCK          # 132608, la fine dei dati

# (periodo nell'introduzione, periodo nel ritornello, nibble di ampiezza)
CHANNELS = [(100, 50, 4), (200, 80, 2)]


def square_nibbles(intro, chorus, amplitude):
    """Un canale: l'onda quadra dell'introduzione, poi quella del ritornello
    con la fase che riparte dal loop."""
    out = []
    for n in range(LOOP_END):
        period, t = (intro, n) if n < LOOP_START else (chorus, n - LOOP_START)
        out.append(amplitude if (t // (period // 2)) % 2 == 0 else -amplitude)
    return out


def ps_adpcm(nibbles, flags=None):
    """Frame da 16 byte: filtro 0 e spostamento 0 (campione = nibble * 4096),
    i flag di flags (frame -> flag), 28 nibble dal basso."""
    data = bytearray()
    for start in range(0, len(nibbles), 28):
        frame = nibbles[start:start + 28]
        frame += [0] * (28 - len(frame))
        data += bytes([0x00, (flags or {}).get(start // 28, 0)])
        data += bytes((frame[i] & 0x0F) | (frame[i + 1] & 0x0F) << 4 for i in range(0, 28, 2))
    return bytes(data)


def genh_header(channels, interleave, rate, loop_start, loop_end, codec, start, size):
    """I campi dei GENH; header_size uguale all'inizio dei dati."""
    head = b"GENH" + struct.pack("<7iI", channels, interleave, rate, loop_start, loop_end, codec, start, size)
    return head + bytes(size - len(head))


def interleaved(channels, block):
    out = bytearray()
    for offset in range(0, len(channels[0]), block):
        for data in channels:
            out += data[offset:offset + block]
    return bytes(out)


def genh_adpcm():
    data = [ps_adpcm(square_nibbles(*channel)) for channel in CHANNELS]
    assert len(data[0]) % INTERLEAVE == 0
    # Le intestazioni dei rip occupano 0x1000 byte, con i campi estesi a zero.
    return genh_header(2, INTERLEAVE, GENH_RATE, LOOP_START, LOOP_END, 0, 0x1000, 0x1000) + interleaved(data, INTERLEAVE)


# --- test-pcm.genh e test.int ---

def sine(frequency, rate, count, amplitude=0.5):
    return [round(amplitude * 32767 * math.sin(2 * math.pi * frequency * n / rate)) for n in range(count)]


def pcm16(samples):
    return struct.pack(f"<{len(samples)}h", *samples)


def genh_pcm():
    rate = 22050
    # Codec 4: PCM16 little endian. Intestazione corta (0x40): i campioni
    # sono la fine del loop.
    return genh_header(1, 0, rate, -1, rate, 4, 0x40, 0x40) + pcm16(sine(1000, rate, rate))


def raw_int():
    rate = 48000
    return interleaved([pcm16(sine(440, rate, 2 * rate)), pcm16(sine(880, rate, 2 * rate))], 0x200)


# --- test.vag e test.svag ---

def square(period, amplitude, count):
    return [amplitude if (n // (period // 2)) % 2 == 0 else -amplitude for n in range(count)]


def vag():
    rate = 22050
    frames = 2 * rate // 28
    # Un frame vuoto davanti, come negli originali; loop dal frame 500.
    body = bytes(16) + ps_adpcm(square(64, 4, frames * 28), {500: 6, frames - 1: 3})
    head = b"VAGp" + struct.pack(">IIII", 0x20, 0, len(body), rate) + bytes(12) + b"prova".ljust(16, b"\0")
    return head + body


def svag():
    blocks = 40  # blocchi da 0x800 per canale
    data = interleaved([ps_adpcm(square(100, 4, blocks * SAMPLES_PER_BLOCK)),
                        ps_adpcm(square(50, 2, blocks * SAMPLES_PER_BLOCK))], INTERLEAVE)
    # Byte dei dati, frequenza, canali, interleave, loop, inizio del loop (byte
    # per canale); l'intestazione ripetuta a 0x400, dati a 0x800.
    head = b"Svag" + struct.pack("<IIHHIII", len(data), 32000, 2, 0, INTERLEAVE, 1, 0x4000)
    return (head.ljust(0x400, b"\0") + b"Svag").ljust(0x800, b"\0") + data


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    files = (("test.genh", genh_adpcm()), ("test-pcm.genh", genh_pcm()), ("test.int", raw_int()),
             ("test.vag", vag()), ("test.svag", svag()))
    for name, data in files:
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
