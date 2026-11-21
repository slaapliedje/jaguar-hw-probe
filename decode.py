#!/usr/bin/env python3
"""Decode Jaguar hardware-probe saves and compare them side by side.

    python3 decode.py LABEL=FILE [LABEL=FILE ...]

FILE is any 128-byte cart EEPROM dump: a JagGD .e2p, a BigPEmu
game*_eeprom.bigpeep, a Virtual Jaguar .eep. Byte order is detected from the
"PR" magic. Durations are Jerry timer-2 ticks (prescaler 1 → nominally 2
system clocks, ~75.2 ns at 26.5909 MHz NTSC); whether a tick really is
2 clocks is itself one of the measurements (see "timer clocks/tick").
"""
import sys

CLK = 26_590_906  # NTSC system clock (PAL: 26_593_900)
LOADS = ["no bitmap", "1 bitmap", "2 bitmaps", "3 bitmaps", "blitter"]


def words(path):
    b = open(path, "rb").read()
    for i in range(0, len(b) - 127, 2):
        blk = b[i:i + 128]
        if blk[:2] == b"PR":
            return [int.from_bytes(blk[j:j + 2], "big") for j in range(0, 128, 2)]
        if blk[:2] == b"RP":
            return [int.from_bytes(blk[j:j + 2], "little") for j in range(0, 128, 2)]
    raise SystemExit(f"{path}: no probe block (magic PR) found")


def rows(w):
    r = []
    sec = [""]                     # rows repeat per load, so key on (section, label)

    def add(k, v):
        if k.startswith("["):
            sec[0] = k
        r.append(((sec[0], k), v))
    ck = sum(w[:63]) & 0xFFFF
    add("version / boot count", f"{w[1]} / {w[2]}")
    add("stage (255 = finished)", w[3])
    add("checksum", "ok" if ck == w[63] else f"BAD ({ck:04X} vs {w[63]:04X})")
    add("EEPROM write failures", w[14])
    add("CONFIG raw", f"{w[4]:04X} ({'NTSC' if w[4] & 0x10 else 'PAL'})")
    add("VC max (half-lines)", w[5])
    add("HC max", w[6])
    add("timer changed in 1000 reads", w[7])
    fp = (w[8] << 16) | w[9]
    add("frame period (ticks)", fp)
    add("  spread over 16 frames", w[10])
    add("blit fill 320x100 (ticks)", w[11])
    add("blit copy 320x100 (ticks)", w[12])
    add(f"blit fill, {'8' if w[1] < 2 else '2'}-bitmap OP load", w[13])
    add("late threshold (ticks)", w[15])
    for i, name in enumerate(LOADS):
        b = 16 + 8 * i
        frames = w[b + 7] or 1
        add(f"[{name}]", "")
        add("  68K loops/frame" if i != 4 else "  fills in 16 frames", w[b])
        add("  I2S irqs/frame", f"{w[b+1]/frames:.1f}")
        add("  period min/max", f"{w[b+2]} / {w[b+3]}")
        add("  late periods", w[b + 6])
        add("  8 DRAM loads max/avg", f"{w[b+4]} / {w[b+5]}")
    # derived: rates from the measured frame period (1-bitmap load)
    irq = w[16 + 8 + 1] / (w[16 + 8 + 7] or 1)
    if irq and fp:
        hz = CLK / (2 * fp)               # 2 clocks per timer tick
        add("[derived]", "")
        add("frame rate (2 clocks/tick)", f"{hz:.2f} Hz")
        add("I2S sample rate", f"{irq * hz:.0f} Hz")
        add("period avg (ticks)", f"{fp / irq:.0f}")
    return r


def main():
    cols = []
    for arg in sys.argv[1:]:
        label, _, path = arg.rpartition("=")
        cols.append((label or path, rows(words(path))))
    if not cols:
        raise SystemExit(__doc__)
    keys = []                      # derived rows may be missing in some saves
    for _, r in cols:
        keys += [k for k, _ in r if k not in keys]
    vals = [dict(r) for _, r in cols]
    kw = max(len(k[1]) for k in keys)
    print(" " * kw + "".join(f"  {c[0]:>16}" for c in cols))
    for k in keys:
        print(f"{k[1]:<{kw}}" + "".join(f"  {str(v.get(k, '-')):>16}" for v in vals))


if __name__ == "__main__":
    main()
