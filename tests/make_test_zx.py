"""Genera i brani di prova per i chip AY dello ZX Spectrum (backend zxtune).

Uso: make_test_zx.py DIR

In DIR scrive test.psg: un dump PSG dei registri dell'AY-3-8910, a 50 frame
al secondo. Per 100 frame il canale A suona un'onda quadra (periodo 254, 491
Hz con l'AY a 1,7734 MHz) a volume 15, poi 50 frame di silenzio: tre secondi,
senza loop.

Serve ai test (CMakeLists.txt), così nel repository non ci sono brani con
copyright.
"""
import os
import sys


def psg():
    # Ogni frame comincia con 0xFF, seguito dalle scritture ai registri.
    header = b"PSG\x1a" + bytes(12)
    data = bytes([0xFF, 7, 0x3E, 0, 0xFE, 1, 0x00, 8, 0x0F])  # tono A, periodo, volume
    data += bytes([0xFF]) * 99
    data += bytes([0xFF, 8, 0x00]) + bytes([0xFF]) * 49
    return header + data + bytes([0xFD])


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "test.psg"), "wb") as f:
        f.write(psg())


if __name__ == "__main__":
    main()
