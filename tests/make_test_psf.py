"""Genera i file PlayStation di prova: un PSF, un MiniPSF con la sua
libreria, un flusso XA ADPCM con due canali e i PSF2 della PlayStation 2.

Uso: make_test_psf.py DIR

In DIR scrive:
- test.psf: un programma per l'R3000 che copia un campione ADPCM con la
  memcpy della BIOS (se la BIOS non è partita non suona niente), lo carica
  nella RAM dello SPU e suona un'onda quadra sulla voce 1, per sempre;
- test.minipsf + Test.PSFLIB: lo stesso programma nella libreria (con un
  nome dalle maiuscole diverse da quello del tag _lib, come capita nei set),
  e nel MiniPSF solo il campione, con un'onda quadra più larga;
- test.xa: un RIFF/CDXA con due canali intrecciati, uno stereo a 37,8 kHz e
  uno mono a 18,9 kHz, ciascuno un'onda quadra;
- test.psf2: un filesystem con psf2.irx, un modulo dell'IOP che mette il
  campione nella RAM dello SPU2 dalla sua porta dati e suona un'onda quadra
  sulla prima voce, per sempre (se non partono i moduli di sistema, non
  parte neanche lui);
- test.minipsf2 + Test.PSF2LIB: la libreria con quel psf2.irx, e nel
  MiniPSF2 un altro psf2.irx, con un'onda quadra più larga, che lo sostituisce.

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


BRANCH_SELF = 0x1000FFFF  # b .


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


# --- PSF2: un modulo dell'IOP (IRX) e il filesystem virtuale ---

SPU2 = 0xBF90  # i registri dello SPU2, da 0xBF900000; il core 1 a +0x400
SPU2_RAM = 0x5010


def iop_program(high):
    """Il codice di psf2.irx: senza indirizzi assoluti, perché modload lo
    carica dove capita, e senza import, perché non chiama nessuno."""
    code = [lui(S0, SPU2)]

    def write(offset, value):
        code.extend([ori(T0, ZERO, value), sh(T0, offset, S0)])

    # I due core accesi, con l'uscita; il core 1 prende anche il suo ingresso
    # esterno, che è l'uscita del core 0.
    write(0x19A, 0xC000)
    write(0x59A, 0xC001)
    # Il campione: indirizzo di trasferimento (in halfword), poi i dati.
    write(0x1A8, 0)
    write(0x1AA, SPU2_RAM >> 1)
    block = adpcm_square(high)
    for half in struct.unpack("<%dH" % (len(block) // 2), block):
        write(0x1AC, half)
    # La voce nel mix asciutto; il core 0 nel core 1; volumi generali.
    for offset in (0x188, 0x190):
        write(offset, 0xFFFF)
        write(offset + 2, 0x00FF)
    write(0x198, 0x0FF0)
    write(0x598, 0x0FFC)
    for offset in (0x760, 0x762, 0x788, 0x78A):
        write(offset, 0x3FFF)
    # Il volume con cui il core 1 prende il core 0, dal suo ingresso esterno.
    write(0x790, 0x7FFF)
    write(0x792, 0x7FFF)
    # Voce 1: volume, frequenza (12 kHz), inviluppo con attacco immediato e
    # sostegno pieno, inizio del campione; key on, poi niente più.
    write(0x000, 0x3FFF)
    write(0x002, 0x3FFF)
    write(0x004, 0x0400)
    write(0x006, 0x000F)
    write(0x008, 0x5FC0)
    write(0x1C0, 0)
    write(0x1C2, SPU2_RAM >> 1)
    write(0x1A0, 1)
    code += [BRANCH_SELF, NOP]
    return struct.pack("<%dI" % len(code), *code)


def irx(name, code):
    """Un modulo IRX: intestazione ELF con il tipo dell'IOP, .iopmod e il
    codice, che parte dall'inizio."""
    iopmod = struct.pack("<6IH", 0xFFFFFFFF, 0, 0, len(code), 0, 0, 0x0101) + name.encode() + b"\0"
    iopmod += bytes(-len(iopmod) % 4)
    text = 52 + 2 * 32 + len(iopmod)
    text += -text % 16
    header = b"\x7FELF" + bytes([1, 1, 1]) + bytes(9)
    header += struct.pack("<HHIIIIIHHHHHH", 0xFF80, 8, 1, 0, 52, 0, 0, 52, 32, 2, 40, 0, 0)
    phdrs = struct.pack("<8I", 0x70000080, 52 + 64, 0, 0, len(iopmod), 0, 4, 4)
    phdrs += struct.pack("<8I", 1, text, 0, 0, len(code), len(code), 7, 16)
    body = header + phdrs + iopmod
    return body + bytes(text - len(body)) + code


def psf2(files, tags):
    """Un PSF2: i file nella radice del filesystem virtuale, ciascuno a
    blocchi compressi con zlib, nell'area riservata; nessun programma."""
    head = 4 + 48 * len(files)
    entries = data = b""
    for name, body in files:
        blocks = [zlib.compress(body[i:i + 0x8000], 9) for i in range(0, len(body), 0x8000)]
        entries += name.encode().ljust(36, b"\0") + struct.pack("<III", head + len(data), len(body), 0x8000)
        data += b"".join(struct.pack("<I", len(b)) for b in blocks) + b"".join(blocks)
    reserved = struct.pack("<I", len(files)) + entries + data
    tag = "[TAG]" + "".join(f"{key}={value}\n" for key, value in tags.items())
    return b"PSF\x02" + struct.pack("<III", len(reserved), 0, 0) + reserved + tag.encode()


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
    with open(os.path.join(out, "test.psf2"), "wb") as f:
        f.write(psf2([("psf2.irx", irx("psf2", iop_program(14)))], tags))
    with open(os.path.join(out, "Test.PSF2LIB"), "wb") as f:
        f.write(psf2([("psf2.irx", irx("psf2", iop_program(14)))], {}))
    mini_tags = dict(tags, title="Onda larga", _lib="test.psf2lib")
    with open(os.path.join(out, "test.minipsf2"), "wb") as f:
        f.write(psf2([("psf2.irx", irx("psf2", iop_program(20)))], mini_tags))


if __name__ == "__main__":
    main()
