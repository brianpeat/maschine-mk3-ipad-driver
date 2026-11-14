# Maschine MK3 protocol notes

Read from one unit (USB 17cc:1600, firmware 1.45) on macOS, 2026-11-14.

## USB layout

| Interface | Class | Endpoints | Use |
|---|---|---|---|
| 0-2 | USB Audio Class 2 | iso OUT 0x01, iso IN 0x81 | audio, 4 out / 2 in |
| 3 | USB MIDI (class-compliant) | bulk OUT 0x02, IN 0x82 | 5-pin MIDI jacks; works on iPadOS as is |
| 4 | HID | interrupt IN 0x83, OUT 0x03, 64 bytes | pads, buttons, knobs, LEDs |
| 5 | vendor ff/bd | bulk OUT 0x04 | the two displays |
| 6 | DFU | none | firmware update |

## HID input report 0x02: pads (64 bytes)

Byte 0 is the report ID, then up to 21 entries of 3 bytes: `pad, type << 4 | pressure high nibble, pressure low byte`.
The list ends at the first all-zero entry after the first.

- `pad`: 0 = top-left, counting across then down; 12 = bottom-left, 3 = top-right.
- Pressure is 12 bits, 0 to 4095.
- Types seen: 1 = hit (the value is the strike strength: soft tap ~1300, hard tap ~3700),
  4 = pressure while held, 3 = release after a hit, 0 and 2 = start and end of a slow press with no strike.

## HID input report 0x01: buttons, knobs, wheel (42 bytes)

Sent only when something changes. Byte 0 is the report ID.

- Bytes 1-10: one bit per button or touch sensor. Confirmed so far: byte 6 bit 0x20 = Play,
  byte 1 bit 0x40 = Shift, byte 1 bit 0x01 = wheel pushed, byte 9 bit 0x80 = wheel touched,
  byte 10 bit 0x80 = knob 1 touched. Byte 8 bit 0x01 was seen and is not identified yet.
- Byte 11: wheel position, a 4-bit counter that steps by one per click.
- Bytes 12-27: the eight knobs, 16-bit little-endian each, counting 0 to 999 and wrapping.

## HID output reports

- 0x80: 62 bytes, button LEDs.
- 0x81: 41 bytes, 25 touch-strip LEDs then 16 pad LEDs. Each byte is
  `colour index << 2 | brightness 0-3`. Confirmed by test: writing `1 << 2 | 3` and `11 << 2 | 3`
  to pad 12 blinked the bottom-left pad red and blue. Each report sets every LED in it, so
  unlisted LEDs go dark. Sent with a HID Set Report (output) while NI's helper was running.
- 0x80 (button LEDs): confirmed working on the iPad. White buttons take brightness 0-63; the
  coloured ones (Group A-H, Sampling, wheel arrows) take `colour << 2 | brightness`, colour 17 = white.
  The button numbering and LED positions in `Driver/MK3Driver.cpp` come from the
  [MK3Kit](https://github.com/chaoyi-ai/MK3Kit) project; its button numbers matched this unit
  for every button checked (Play, Shift, wheel push, Channel/MIDI).
