"""Genera un modulo ProTracker (M.K.) di prova con due campioni sintetici.

Uso: make_test_mod.py OUT.mod [--subsongs]

Con --subsongs il primo pattern torna su se stesso e il secondo non viene mai
raggiunto: per i player sono due sottobrani. Serve ai test (CMakeLists.txt),
così nel repository non ci sono moduli con copyright.
"""
import struct
import sys

PERIODS = {  # ProTracker, finetune 0
    "C-2": 428, "D-2": 381, "E-2": 339, "F-2": 320, "G-2": 285, "A-2": 254, "B-2": 226,
    "C-3": 214, "D-3": 190, "E-3": 170, "G-3": 143, "A-3": 127,
}


def sample_header(name, data, volume=64):
    words = len(data) // 2
    return struct.pack(">22sHBBHH", name.encode(), words, 0, volume, 0, words)


def note(sample, period, effect=0, param=0):
    return bytes([
        (sample & 0xF0) | (period >> 8),
        period & 0xFF,
        ((sample & 0x0F) << 4) | effect,
        param,
    ])


EMPTY = bytes(4)


def pattern(melody, bass, jump_to=None):
    rows = []
    for row in range(64):
        cells = [EMPTY, EMPTY, EMPTY, EMPTY]
        if row % 4 == 0:
            cells[0] = note(1, PERIODS[melody[(row // 4) % len(melody)]])
        if row % 8 == 0:
            cells[1] = note(2, PERIODS[bass[(row // 8) % len(bass)]])
        if row == 63 and jump_to is not None:
            cells[3] = note(0, 0, 0xB, jump_to)  # Bxx: salta alla posizione xx
        rows.append(b"".join(cells))
    return b"".join(rows)


def main(path, subsongs=False):
    square = bytes([0x60] * 16 + [0xA0] * 16) * 2
    saw = bytes(((i * 8) - 128) & 0xFF for i in range(32)) * 2

    header = b"megaplayer test".ljust(20, b"\0")
    header += sample_header("square", square)
    header += sample_header("saw", saw, volume=48)
    header += bytes(30) * 29

    # Con subsongs il pattern 0 torna su se stesso e il pattern 1 non viene
    # mai raggiunto: per i player è un secondo sottobrano.
    orders = [0, 1] if subsongs else [0, 1, 0, 1]
    header += bytes([len(orders), 127]) + bytes(orders).ljust(128, b"\0") + b"M.K."

    patterns = pattern(["C-3", "E-3", "G-3", "E-3"], ["C-2", "C-2", "G-2", "G-2"], 0 if subsongs else None)
    patterns += pattern(["A-2", "C-3", "E-3", "C-3"], ["F-2", "F-2", "G-2", "G-2"], 1 if subsongs else None)

    with open(path, "wb") as f:
        f.write(header + patterns + square + saw)


if __name__ == "__main__":
    main(sys.argv[1], "--subsongs" in sys.argv[2:])
