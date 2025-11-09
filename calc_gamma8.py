#!/usr/bin/env python3
import math

OUT_FILE = "gamma_table.hex"

def gamma_srgb(x: int) -> int:
    """sRGB-гамма коррекция (IEC 61966-2-1:1999)"""
    norm = x / 255.0
    if norm <= 0.04045:
        corrected = norm / 12.92
    else:
        corrected = pow((norm + 0.055) / 1.055, 2.4)
    return int(round(corrected * 255))

# === Расчет таблицы ===
table = [gamma_srgb(x) for x in range(256)]

# === Запись в файл (16 значений в строке) ===
with open(OUT_FILE, "w") as f:
    for i in range(0, 256, 16):
        chunk = table[i:i+16]
        hex_line = ", ".join(f"0x{v:02X}" for v in chunk)
        f.write(hex_line + "\n")

print(f"Таблица sRGB-гаммы записана в {OUT_FILE}")