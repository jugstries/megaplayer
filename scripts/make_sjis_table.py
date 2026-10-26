#!/usr/bin/env python3
"""Genera src/core/ShiftJisTable.inc: la tabella da Shift-JIS a Unicode per i
caratteri a due byte, nella variante cp932 di Microsoft (con i caratteri
speciali NEC dei PC-98), dal codec di Python. Uso: scripts/make_sjis_table.py"""

import pathlib

LEADS = list(range(0x81, 0xA0)) + list(range(0xE0, 0xFD))
TRAILS = list(range(0x40, 0x7F)) + list(range(0x80, 0xFD))

rows = []
for lead in LEADS:
    values = []
    for trail in TRAILS:
        try:
            text = bytes([lead, trail]).decode("cp932")
            values.append(ord(text) if len(text) == 1 and ord(text) <= 0xFFFF else 0)
        except UnicodeDecodeError:
            values.append(0)
    rows.append(values)

out = ["// Generato da scripts/make_sjis_table.py: non modificare a mano.",
       f"// {len(LEADS)} byte iniziali (0x81-0x9F, 0xE0-0xFC) per {len(TRAILS)} secondi byte",
       "// (0x40-0x7E, 0x80-0xFC); 0 dove cp932 non ha un carattere."]
for lead, values in zip(LEADS, rows):
    out.append(f"{{ // 0x{lead:02X}")
    for i in range(0, len(values), 16):
        out.append(" ".join(f"0x{v:04X}," for v in values[i:i + 16]))
    out.append("},")
path = pathlib.Path(__file__).resolve().parent.parent / "src/core/ShiftJisTable.inc"
path.write_text("\n".join(out) + "\n", encoding="utf-8")
print(f"scritto {path}")
