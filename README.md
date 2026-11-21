# Jaguar hardware probe

A small Atari Jaguar program that measures timing that emulators often get wrong, so the same image can be run on a real Jaguar and in emulators and the numbers compared. It runs unattended for about 10 seconds. Then it shows 64 result words on screen as hex and saves them to the cart EEPROM (a JagGD keeps that as a 128-byte `.e2p`).

It measures:
- whether the Jerry timers can be read back;
- the frame period;
- blitter fill and copy speed;
- DSP I2S interrupt rate and spacing;
- DSP DRAM load latency;
- 68K work per frame.

All of these are taken under five Object Processor or blitter loads.

## Build

Needs the [jaguar-sdk](https://github.com/cubanismo/jaguar-sdk) (rmac, rln, m68k-aout-gcc):

    make JAGSDK=/path/to/jaguar-sdk

This gives `probe.cof`, which Virtual Jaguar and BigPEmu load directly. For a cart image (JagGD, Skunkboard), wrap it with your usual ROM tool. Atari's EEPROM driver is assembled straight from the SDK (`jaguar/source/eeprom/eeprom.s`) and isn't included here.

## Read the results

    python3 decode.py real=results/real-jaguar.e2p vj=my.eep

This takes any 128-byte EEPROM dump (JagGD `.e2p`, BigPEmu `.bigpeep`, Virtual Jaguar `.eep`); byte order is detected. If an emulator doesn't save the EEPROM, copy the 64 words off the screen.

Time base: Jerry timer 2, prescaler 1 (2 system clocks per tick, ~75 ns), divider read at `$F1003C` by both the 68K and the DSP. All durations are in these ticks.

| Word | Meaning |
|---|---|
| 0–3 | magic `PR`, version, boot count, stage (255 = finished; shows where a hang happened) |
| 4–7 | CONFIG raw, VC max, HC max, timer changes in 1000 reads (0 = timer unreadable) |
| 8–10 | frame period in ticks (32-bit), spread across 16 frames |
| 11–13 | blit fill / blit copy 320×100, fill under 2-bitmap OP load |
| 14–15 | EEPROM write failures, DSP "late" threshold (1.5 × min period) |
| 16+8n | load n (0 no bitmap, 1 one bitmap, 2 two, 3 three, 4 blitter; run order 0 1 4 2 3, heaviest last): 68K loops/frame (load 4: fills), I2S irqs, period min, max, 8-DRAM-load max, avg, late count, frames |
| 63 | checksum of words 0–62 |

## Results so far

`results/comparison.txt` compares these NTSC runs:
- **real Jaguar:** a JagGD save;
- **BigPEmu;**
- **Virtual Jaguar Rx develop** at 9321810;
- **Virtual Jaguar with the timing fixes** from [slaapliedje/Virtual-Jaguar-Rx](https://github.com/slaapliedje/Virtual-Jaguar-Rx) (branch `etjag`).

On the real Jaguar, the run stops at stage 7. Three stacked full-width 8bpp bitmaps are more than the Object Processor can fetch per line, so it never reaches STOP and starves the 68K. That's why that save has no 3-bitmap results and its checksum isn't written. Two bitmaps run fine. No emulator tested models this limit.

Other real-hardware findings:
- **Frame and I2S rate:** the frame is 60.07 Hz (VC max 523). I2S runs at ~15.98 kHz, 266 interrupts per frame.
- **I2S spacing:** it jitters ±12 % with one bitmap and ±23 % with two, as bus stalls delay interrupt entry. No interrupts are late or missed.
- **DSP DRAM loads:** 8 back-to-back loads take ~70–80 ticks on average, and up to 262 under two bitmaps. BigPEmu gives a flat 32.
- **68K work:** the 68K gets ~3090 loop iterations per frame with one bitmap. BigPEmu and Virtual Jaguar give ~5000.

## Licence

GPL v2 (see `COPYING`). `startup.s` and the 8×8 font in `font.h` come from the jaguar-sdk `jaghello` example, which is GPL v2.
