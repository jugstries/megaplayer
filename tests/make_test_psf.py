"""Genera i file PlayStation di prova: un PSF, un MiniPSF con la sua
libreria e un flusso XA ADPCM con due canali.

Uso: make_test_psf.py DIR

In DIR scrive:
- test.psf: un programma per l'R3000 che copia un campione ADPCM con la
  memcpy della BIOS (se la BIOS non è partita non suona niente), lo carica
  nella RAM dello SPU e suona un'onda quadra sulla voce 1, per sempre;
- test.minipsf + Test.PSFLIB: lo stesso programma nella libreria (con un
  nome dalle maiuscole diverse da quello del tag _lib, come capita nei set),
  e nel MiniPSF solo il campione, con un'onda quadra più larga;
- test.xa: un RIFF/CDXA con due canali intrecciati, uno stereo a 37,8 kHz e
  uno mono a 18,9 kHz, ciascuno un'onda quadra.

Serve ai test (CMakeLists.txt), così nel repository non ci sono brani con
copyright.
"""
import os
import struct
import sys
import zlib

# --- R3000 ---

ZERO, A0, A1, A2, T0, T1, T2, S0, RA = 0, 4, 5, 6, 8, 9, 10, 16, 31


def lui(rt, imm):
    return 0x0F << 26 | rt << 16 | imm


def ori(rt, rs, imm):
    return 0x0D << 26 | rs << 21 | rt << 16 | imm


def addiu(rt, rs, imm):
    return 0x09 << 26 | rs << 21 | rt << 16 | (imm & 0xFFFF)


def sh(rt, offset, base):
    return 0x29 << 26 | base << 21 | rt << 16 | offset


def lhu(rt, offset, base):
    return 0x25 << 26 | base << 21 | rt << 16 | offset


def jalr(rs):
    return rs << 21 | RA << 11 | 0x09


def j(target):
    return 0x02 << 26 | (target >> 2) & 0x03FFFFFF


NOP = 0
TEXT = 0x80010000
SAMPLE = 0x80010800  # il campione sta nella seconda pagina da 2 KB
BUFFER = 0x80020000
SPU_RAM = 0x1000      # dove va il campione nella RAM dello SPU


def program():
    """Il codice, da caricare in TEXT."""
    code = [
        # memcpy(BUFFER, SAMPLE, 16) con la tabella A0 della BIOS (A0:2A).
        lui(A0, BUFFER >> 16),
        lui(A1, SAMPLE >> 16),
        ori(A1, A1, SAMPLE & 0xFFFF),
        addiu(A2, ZERO, 16),
        addiu(T1, ZERO, 0x2A),
        addiu(T0, ZERO, 0xA0),
        jalr(T0),
        NOP,
        lui(S0, 0x1F80),
        # SPU acceso e volume generale al massimo.
        ori(T0, ZERO, 0xC000), sh(T0, 0x1DAA, S0),
        ori(T0, ZERO, 0x3FFF), sh(T0, 0x1D80, S0), sh(T0, 0x1D82, S0),
        # Il campione dal buffer alla RAM dello SPU, una halfword per volta.
        ori(T0, ZERO, SPU_RAM >> 3), sh(T0, 0x1DA6, S0),
        lui(T2, BUFFER >> 16),
    ]
    for i in range(8):
        code += [lhu(T0, i * 2, T2), NOP, sh(T0, 0x1DA8, S0)]
    code += [
        # Voce 1: volume, frequenza (11025 Hz), inizio e ritorno del ciclo,
        # inviluppo con attacco immediato e sostegno pieno.
        ori(T0, ZERO, 0x3FFF), sh(T0, 0x1C00, S0), sh(T0, 0x1C02, S0),
        ori(T0, ZERO, 0x0400), sh(T0, 0x1C04, S0),
        ori(T0, ZERO, SPU_RAM >> 3), sh(T0, 0x1C06, S0), sh(T0, 0x1C0E, S0),
        ori(T0, ZERO, 0x000F), sh(T0, 0x1C08, S0),
        ori(T0, ZERO, 0x1FC0), sh(T0, 0x1C0A, S0),
        # Key on, poi niente più.
        ori(T0, ZERO, 1), sh(T0, 0x1D88, S0),
    ]
    loop = TEXT + len(code) * 4
    code += [j(loop), NOP]
    return struct.pack("<%dI" % len(code), *code)


def adpcm_square(high):
    """Un blocco ADPCM dello SPU che si ripete: high campioni a +7, il resto a -8."""
    nibbles = [7] * high + [8] * (28 - high)  # -8 in complemento a due
    data = bytes(nibbles[i] | nibbles[i + 1] << 4 for i in range(0, 28, 2))
    return bytes([0x02, 0x07]) + data  # spostamento 2, filtro 0; inizio, fine e ripeti


def ps_x_exe(address, text, pc=TEXT):
    text += bytes(-len(text) % 0x800)
    header = bytearray(0x800)
    header[0:8] = b"PS-X EXE"
    struct.pack_into("<IIII", header, 0x10, pc, 0, address, len(text))
    struct.pack_into("<I", header, 0x30, 0x801FFFF0)
    region = b"Sony Computer Entertainment Inc. for North America area"
    header[0x4C:0x4C + len(region)] = region
    return bytes(header) + text


def psf(exe, tags):
    packed = zlib.compress(exe, 9)
    head = b"PSF\x01" + struct.pack("<III", 0, len(packed), zlib.crc32(packed))
    tag = "[TAG]" + "".join(f"{key}={value}\n" for key, value in tags.items())
    return head + packed + tag.encode()


# --- XA ADPCM ---

def xa_group(blocks):
    """Un gruppo audio da 128 byte: 8 blocchi da 28 nibble, spostamento 2, filtro 0."""
    headers = [0x02] * 8
    group = bytearray(128)
    group[0:4] = bytes(headers[0:4])
    group[4:12] = bytes(headers)
    group[12:16] = bytes(headers[4:8])
    for i in range(28):
        for b in range(0, 8, 2):
            group[16 + i * 4 + b // 2] = (blocks[b][i] & 0x0F) | (blocks[b + 1][i] & 0x0F) << 4
    return bytes(group)


def square(period, count, start):
    return [7 if ((start + i) // (period // 2)) % 2 == 0 else -8 for i in range(count)]


def xa_sector(index, channel, coding, position, period, stereo):
    groups = b""
    for g in range(18):
        if stereo:
            # Blocchi pari a sinistra, dispari a destra: 4 per lato, 112 frame.
            blocks = []
            for b in range(4):
                left = square(period, 28, position + b * 28)
                blocks += [left, left]
            position += 112
        else:
            blocks = [square(period, 28, position + b * 28) for b in range(8)]
            position += 224
        groups += xa_group(blocks)
    sync = bytes([0]) + bytes([0xFF] * 10) + bytes([0])
    minute, second, frame = index // 4500, (index // 75) % 60, index % 75
    bcd = lambda v: (v // 10) << 4 | v % 10
    header = bytes([bcd(minute), bcd(second + 2), bcd(frame), 2])
    subheader = bytes([1, channel, 0x64, coding]) * 2
    sector = sync + header + subheader + groups
    return sector + bytes(2352 - len(sector)), position


def xa_file(sectors_per_channel):
    data = b""
    positions = [0, 0]
    for i in range(sectors_per_channel * 2):
        channel = i % 2
        if channel == 0:  # stereo, 37,8 kHz
            sector, positions[0] = xa_sector(i, 0, 0x01, positions[0], 84, True)
        else:  # mono, 18,9 kHz
            sector, positions[1] = xa_sector(i, 1, 0x04, positions[1], 42, False)
        data += sector
    fmt = struct.pack("<HHHBBBB", 0, 0, 0x5555, ord("U"), ord("X"), 1, 0) + bytes(6)
    body = b"CDXA" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    return b"RIFF" + struct.pack("<I", len(body)) + body


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    code = program()
    whole = code + bytes(0x800 - len(code)) + adpcm_square(14)
    tags = {"title": "Onda quadra", "artist": "megaplayer", "length": "0:05", "fade": "1"}
    with open(os.path.join(out, "test.psf"), "wb") as f:
        f.write(psf(ps_x_exe(TEXT, whole), tags))
    with open(os.path.join(out, "Test.PSFLIB"), "wb") as f:
        f.write(psf(ps_x_exe(TEXT, whole), {}))
    mini_tags = dict(tags, title="Onda larga", _lib="test.psflib")
    with open(os.path.join(out, "test.minipsf"), "wb") as f:
        f.write(psf(ps_x_exe(SAMPLE, adpcm_square(20)), mini_tags))
    with open(os.path.join(out, "test.xa"), "wb") as f:
        f.write(xa_file(150))  # 8 s a 37,8 kHz in stereo, 32 s a 18,9 kHz in mono


if __name__ == "__main__":
    main()
