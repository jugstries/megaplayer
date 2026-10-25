"""Genera i file di prova di Saturn, Dreamcast e QSound: SSF, DSF e QSF.

Uso: make_test_xsf.py DIR

In DIR scrive test.ssf, test.dsf e test.qsf: per ciascuno un programma per la
CPU del suono (il 68000 del Saturn, l'ARM7 del Dreamcast, lo Z80 delle schede
Capcom) che prepara un campione a onda quadra sul primo canale del chip (SCSP,
AICA, QSound) e lo suona per sempre. Serve ai test (CMakeLists.txt), così nel
repository non ci sono brani con copyright.
"""
import os
import struct
import sys
import zlib

TAGS = {"title": "Onda quadra", "artist": "megaplayer", "length": "0:05", "fade": "1"}
SAMPLE = 0x1000  # dove sta il campione nella RAM del suono


def psf(version, exe, tags):
    packed = zlib.compress(exe, 9)
    head = b"PSF" + bytes([version]) + struct.pack("<III", 0, len(packed), zlib.crc32(packed))
    tag = "[TAG]" + "".join(f"{key}={value}\n" for key, value in tags.items())
    return head + packed + tag.encode()


def square16(count, amplitude, big_endian):
    values = [amplitude] * (count // 2) + [-amplitude] * (count // 2)
    return struct.pack((">" if big_endian else "<") + "%dh" % count, *values)


# --- Saturn: 68000 e SCSP (registri da 0x100000, 32 slot da 0x20 byte) ---

def ssf():
    writes = [
        (0x100400, 0x000F),  # volume generale
        (0x100002, SAMPLE),  # inizio del campione (byte)
        (0x100004, 0),       # inizio del ciclo (campioni)
        (0x100006, 32),      # fine del ciclo
        (0x100008, 0x001F),  # attacco immediato
        (0x10000A, 0x001F),  # rilascio
        (0x10000C, 0x0000),  # livello totale: massimo
        (0x100010, 0x7000),  # due ottave sotto: 11025 Hz
        (0x100016, 0xE000),  # mandata diretta al massimo, al centro
        (0x100000, 0x1820),  # key on (KYONEX, KYONB), ciclo normale, 16 bit
    ]
    code = b"".join(struct.pack(">HHI", 0x33FC, value, address) for address, value in writes)  # move.w #,(abs).l
    code += struct.pack(">H", 0x60FE)  # bra.s *
    ram = bytearray(SAMPLE)
    # Stack e PC iniziali, poi le altre eccezioni (le interruzioni dello SCSP)
    # su un gestore che torna subito (rte).
    struct.pack_into(">II", ram, 0, 0x0007FFF0, 0x400)
    for vector in range(2, 64):
        struct.pack_into(">I", ram, vector * 4, 0x500)
    ram[0x400:0x400 + len(code)] = code
    struct.pack_into(">H", ram, 0x500, 0x4E73)
    ram += square16(32, 0x2000, True)
    return psf(0x11, struct.pack("<I", 0) + bytes(ram), TAGS)


# --- Dreamcast: ARM7 e AICA (registri da 0x800000, 64 canali da 0x80 byte) ---

def dsf():
    writes = [
        (0x800004, SAMPLE),  # inizio del campione (byte)
        (0x800008, 0),       # inizio del ciclo (campioni)
        (0x80000C, 32),      # fine del ciclo
        (0x800010, 0x001F),  # attacco immediato
        (0x800014, 0x001F),  # rilascio
        (0x800018, 0x7000),  # due ottave sotto: 11025 Hz
        (0x800024, 0x0F00),  # mandata diretta al massimo, al centro
        (0x800028, 0x0020),  # filtro escluso, livello totale massimo
        (0x800000, 0xC200),  # key on (KYONEX, KYONB), ciclo, 16 bit
    ]
    # In testa i vettori delle eccezioni: il reset salta al codice, le altre
    # (le interruzioni dell'AICA) si fermano lì. Poi r0 = canale 1, r2 =
    # registri comuni; ogni valore da una tabella dopo il codice, con
    # ldr rN, [pc, #...].
    constants = [0x800000, 0x802800, 0x000F] + [value for _, value in writes]
    loads = 2 + 1 + len(writes)
    count = 8 + loads + 1 + len(writes) + 1  # vettori, ldr, str e il salto finale
    pool = count * 4

    def ldr(rd, index, at):
        return 0xE59F0000 | rd << 12 | (pool + index * 4 - (at + 8))

    def store(rs, rb, offset):
        return 0xE5800000 | rb << 16 | rs << 12 | offset

    code = [0xEA000006] + [0xEAFFFFFE] * 7  # b 0x20; b .
    code.append(ldr(0, 0, len(code) * 4))
    code.append(ldr(2, 1, len(code) * 4))
    code.append(ldr(1, 2, len(code) * 4))
    code.append(store(1, 2, 0))  # volume generale, 0x802800
    for i, (address, _) in enumerate(writes):
        code.append(ldr(1, 3 + i, len(code) * 4))
        code.append(store(1, 0, address - 0x800000))
    code.append(0xEAFFFFFE)  # b .
    assert len(code) * 4 == pool
    ram = struct.pack("<%dI" % len(code), *code) + struct.pack("<%dI" % len(constants), *constants)
    ram += bytes(SAMPLE - len(ram)) + square16(32, 0x2000, False)
    return psf(0x12, struct.pack("<I", 0) + ram, TAGS)


# --- QSound: Z80 e il chip QSound (porte 0xD000-0xD002, pronto in 0xD007) ---

def qsf():
    writes = [
        (0x01, 0x0000),  # voce 1: posizione
        (0x02, 0x1000),  # passo 1.0: 24038 Hz
        (0x04, 64),      # lunghezza del ciclo
        (0x05, 64),      # fine
        (0x06, 0x1000),  # volume
    ]
    code = bytearray([0xF3, 0x31, 0xF0, 0xEF])  # di; ld sp, 0xEFF0
    for register, value in writes:
        wait = len(code)
        code += bytes([0x3A, 0x07, 0xD0, 0xB7, 0xF2, wait & 0xFF, wait >> 8])  # aspetta il pronto
        code += bytes([0x3E, value >> 8, 0x32, 0x00, 0xD0])
        code += bytes([0x3E, value & 0xFF, 0x32, 0x01, 0xD0])
        code += bytes([0x3E, register, 0x32, 0x02, 0xD0])
    code += bytes([0x18, 0xFE])  # jr $
    samples = bytes([0x40] * 32 + [0xC0] * 32)
    chunks = b"Z80" + struct.pack("<II", 0, len(code)) + bytes(code)
    chunks += b"SMP" + struct.pack("<II", 0, len(samples)) + samples
    return psf(0x41, chunks, TAGS)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, data in (("test.ssf", ssf()), ("test.dsf", dsf()), ("test.qsf", qsf())):
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
