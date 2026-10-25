"""Genera i file di prova della famiglia PSF: SSF, DSF, QSF, GSF e 2SF.

Uso: make_test_xsf.py DIR

In DIR scrive test.ssf, test.dsf, test.qsf, test.gsf e test.2sf: per ciascuno
un programma per la CPU del suono (il 68000 del Saturn, l'ARM7 del Dreamcast e
del DS, lo Z80 delle schede Capcom, l'ARM7TDMI del GBA) che suona un'onda
quadra per sempre, da un campione sul primo canale del chip (SCSP, AICA,
QSound) o dal generatore di onde quadre (GBA, DS). Serve ai test (CMakeLists.txt), così nel
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


# --- ARM (ARM7 del Dreamcast e del DS, ARM7TDMI del GBA) ---

def arm(ops):
    """Codice ARM: ("ldr", rd, valore) carica una costante da una tabella dopo
    il codice, ("str", rs, rb, offset) e ("strh", ...) scrivono, un intero è
    un'istruzione già fatta. Alla fine un salto su sé stesso."""
    words = len(ops) + 1
    constants = []
    code = []
    for op in ops:
        at = len(code) * 4
        if isinstance(op, int):
            code.append(op)
        elif op[0] == "ldr":
            constants.append(op[2])
            offset = words * 4 + (len(constants) - 1) * 4 - (at + 8)
            code.append(0xE59F0000 | op[1] << 12 | offset)
        elif op[0] == "str":
            code.append(0xE5800000 | op[2] << 16 | op[1] << 12 | op[3])
        elif op[0] == "strh":
            code.append(0xE1C000B0 | op[2] << 16 | op[1] << 12 | (op[3] >> 4) << 8 | (op[3] & 0xF))
    code.append(0xEAFFFFFE)  # b .
    return struct.pack("<%dI" % len(code), *code) + struct.pack("<%dI" % len(constants), *constants)


# --- Dreamcast: ARM7 e AICA (registri da 0x800000, 64 canali da 0x80 byte) ---

def dsf():
    writes = [
        (0x04, SAMPLE),  # inizio del campione (byte)
        (0x08, 0),       # inizio del ciclo (campioni)
        (0x0C, 32),      # fine del ciclo
        (0x10, 0x001F),  # attacco immediato
        (0x14, 0x001F),  # rilascio
        (0x18, 0x7000),  # due ottave sotto: 11025 Hz
        (0x24, 0x0F00),  # mandata diretta al massimo, al centro
        (0x28, 0x0020),  # filtro escluso, livello totale massimo
        (0x00, 0xC200),  # key on (KYONEX, KYONB), ciclo, 16 bit
    ]
    # In testa i vettori delle eccezioni: il reset salta al codice, le altre
    # (le interruzioni dell'AICA) si fermano lì. r0 = canale 1, r2 = registri
    # comuni.
    ops = [0xEA000006] + [0xEAFFFFFE] * 7  # b 0x20; b .
    ops += [("ldr", 0, 0x800000), ("ldr", 2, 0x802800), ("ldr", 1, 0x000F), ("str", 1, 2, 0)]
    for offset, value in writes:
        ops += [("ldr", 1, value), ("str", 1, 0, offset)]
    ram = arm(ops)
    ram += bytes(SAMPLE - len(ram)) + square16(32, 0x2000, False)
    return psf(0x12, struct.pack("<I", 0) + ram, TAGS)


# --- Game Boy Advance: ARM7TDMI e la prima onda quadra (registri da
# 0x04000000) ---

def gsf():
    writes = [
        (0x84, 0x0080),  # SOUNDCNT_X: suono acceso
        (0x80, 0xFF77),  # SOUNDCNT_L: volume pieno, tutti i canali ai due lati
        (0x82, 0x0002),  # SOUNDCNT_H: canali PSG al 100%
        (0x60, 0x0000),  # SOUND1CNT_L: niente sweep
        (0x62, 0xF080),  # SOUND1CNT_H: inviluppo a 15, onda al 50%
        (0x64, 0x86D6),  # SOUND1CNT_X: via, 440 Hz
    ]
    ops = [("ldr", 0, 0x04000000)]
    for offset, value in writes:
        ops += [("ldr", 1, value), ("strh", 1, 0, offset)]
    rom = arm(ops)
    # Punto d'ingresso e indirizzo in ROM, lunghezza, poi il programma.
    return psf(0x22, struct.pack("<III", 0x08000000, 0x08000000, len(rom)) + rom, TAGS)


# --- Nintendo DS: una ROM con un programma per l'ARM9 (fermo) e uno per
# l'ARM7, che accende il canale 9, l'onda quadra (PSG), a 440 Hz ---

def twosf():
    # Come nei giochi, i processori poi si fermano in attesa di un'interruzione
    # (che non arriva): l'ARM9 con il CP15, l'ARM7 con la BIOS (swi 6).
    arm9 = arm([0xEE070F90])  # mcr p15, 0, r0, c7, c0, 4
    arm7 = arm([
        ("ldr", 2, 0x04000500), ("ldr", 1, 0x807F), ("strh", 1, 2, 0),        # SOUNDCNT: acceso, volume pieno
        ("ldr", 3, 0x04000480), ("ldr", 1, 0xED68), ("strh", 1, 3, 8),        # SOUND8TMR: 440 Hz
        ("ldr", 1, 0xE340007F), ("str", 1, 3, 0),                             # SOUND8CNT: via, PSG 50%, centro
        0xEF060000,                                                           # swi 6: Halt
    ])
    rom = bytearray(0x600)
    rom[0:12] = b"MEGAPLAYER\0\0"
    struct.pack_into("<IIII", rom, 0x20, 0x200, 0x02000000, 0x02000000, len(arm9))
    struct.pack_into("<IIII", rom, 0x30, 0x400, 0x02380000, 0x02380000, len(arm7))
    rom[0x200:0x200 + len(arm9)] = arm9
    rom[0x400:0x400 + len(arm7)] = arm7
    return psf(0x24, struct.pack("<II", 0, len(rom)) + bytes(rom), TAGS)


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
    files = (("test.ssf", ssf()), ("test.dsf", dsf()), ("test.qsf", qsf()), ("test.gsf", gsf()), ("test.2sf", twosf()))
    for name, data in files:
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
