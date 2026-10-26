"""Genera i brani di prova dei formati dei computer giapponesi.

Uso: make_test_japanese.py DIR

In DIR scrive:
- test.mdx: un MDX dello Sharp X68000 col titolo in Shift-JIS ("Prova
  テスト"), senza PDX. La traccia A, a tempo 200 (14,336 ms per clock), suona
  per 128 clock una nota con una voce FM che è un seno puro (solo
  l'operatore 1, algoritmo 7), poi 64 clock di pausa; le altre otto tracce
  finiscono subito. Il brano dura quindi 192 clock, 2,75 secondi, e non ha
  loop;
- test.m: un PMD del PC-98. La parte SSG 1, col timer B a 200 (896 campioni
  a 55467 Hz, 16,15 ms per tick), suona in loop un La della quarta ottava
  per 96 tick a volume 15 e 48 tick di pausa; le altre parti finiscono
  subito. Un giro dura 144 tick, 2,33 secondi: con due giri e la
  dissolvenza il brano dura 14,65 secondi.

Serve ai test (CMakeLists.txt), così nel repository non ci sono brani con
copyright.
"""
import os
import struct
import sys

TEMPO = 200


def mdx():
    title = "Prova ".encode() + "テスト".encode("cp932")
    header = title + b"\r\n\x1a" + b"\x00"  # nessun PDX
    tracks = 9
    track_a = bytes([
        0xFF, TEMPO,  # @t
        0xFD, 0x00,   # @0
        0xFC, 0x03,   # p3: centro
        0xFB, 0x0F,   # v15
        0xB0, 0x7F,   # nota, 128 clock
        0x3F,         # pausa, 64 clock
        0xF1, 0x00,   # fine, senza loop
    ])
    others = bytes([0xF1, 0x00])
    # La voce 0: FL/CON, maschera degli operatori, poi per ogni parametro i
    # quattro operatori. Solo l'operatore 1 suona (TL 0), gli altri muti.
    voice = bytes([0x00, 0x07, 0x0F,
                   0x01, 0x01, 0x01, 0x01,   # DT1/MUL
                   0x00, 0x7F, 0x7F, 0x7F,   # TL
                   0x1F, 0x1F, 0x1F, 0x1F,   # KS/AR
                   0x00, 0x00, 0x00, 0x00,   # AME/D1R
                   0x00, 0x00, 0x00, 0x00,   # DT2/D2R
                   0x0F, 0x0F, 0x0F, 0x0F])  # D1L/RR
    # Dopo l'intestazione: l'offset delle voci e quelli delle tracce, tutti
    # dall'inizio di questa tabella.
    table = 2 + tracks * 2
    offsets = [table]
    body = track_a
    for _ in range(tracks - 1):
        offsets.append(table + len(body))
        body += others
    voices_at = table + len(body)
    return header + struct.pack(">H", voices_at) + b"".join(struct.pack(">H", o) for o in offsets) + body + voice


def pmd():
    # Dopo il primo byte (0: OPN, non OPM) gli offset delle 11 parti (FM 1-6,
    # SSG 1-3, ADPCM, ritmo) e della tabella del ritmo, dall'inizio di questi
    # dati. Con la prima a 0x18 il brano non ha voci FM sue.
    end = 0x18
    ssg = end + 1
    offsets = [end] * 6 + [ssg, end, end, end, end]
    header = b"".join(struct.pack("<H", o) for o in offsets) + struct.pack("<H", end)
    part = bytes([
        0xFC, 200,    # tempo: il timer B
        0xFD, 0x0F,   # volume 15
        0xF6,         # L: il loop riparte da qui
        0x49, 96,     # o4 La, 96 tick
        0x0F, 48,     # pausa, 48 tick
        0x80,         # fine: torna alla L
    ])
    return b"\x00" + header + bytes([0x80]) + part


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "test.mdx"), "wb") as f:
        f.write(mdx())
    with open(os.path.join(out, "test.m"), "wb") as f:
        f.write(pmd())


if __name__ == "__main__":
    main()
