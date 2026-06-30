# Medusa GM

A 16-track General-MIDI groovebox + 1 subtractive synth track, powered by the
[AMY](https://github.com/shorepine/amy) synthesizer engine on a Raspberry Pi
RP2040. Sequencer, 16 GM voices, an AMY synth voice, stereo panning, and an
optional reverb/chorus send — all driven from one encoder, five buttons and a
128×64 OLED.

## Hardware

| Block        | Part                                             | Pins (config.h) |
|--------------|--------------------------------------------------|-----------------|
| MCU          | **RP2040, 16 MB flash** (TENSTAR / WeAct RP2040 Pro Micro 16MB) | — |
| Audio DAC    | PCM5102 I²S                                       | BCK `GP15`, LRCK `GP16` (=BCK+1), DIN `GP17` |
| Display      | **128×64 OLED — SH1106** (SSD1306/SSD1309 also supported) | SDA `GP4`, SCL `GP5`, addr `0x3C` |
| Encoder      | EC11 with push                                   | A `GP6`, B `GP7`, SW `GP8` |
| Buttons (×5) | PLAY/MUTE/TRACK/PAGE/SHIFT, active-low           | `GP9`,`GP13`,`GP12`,`GP11`,`GP10` |
| Click out    | reserved                                         | `GP14` |

> RP2350 / Pico 2 also builds unchanged — `config.h` auto-tunes the voice pool
> (up to 40 voices on RP2350, 24 on RP2040). All pins live in `config.h`; if
> your board doesn't expose one of the GPIOs above, remap it there.

## Why 16 MB flash

The GM voices play from a sample bank baked out of a full General-MIDI
SoundFont (**Power GM 1.5**) at **44.1 kHz** — the same rate AMY renders at, so
a note played at a sample's root key needs no resampling. That bank is ~7 MB of
flash, which is why this build targets a 16 MB board. The remaining flash leaves
plenty of room for code plus a small filesystem for song save/load.

## Build (Arduino IDE / arduino-cli)

1. Install the **arduino-pico** core (Earle Philhower).
2. Libraries: **AMY** (use the fork in this repo — it contains the baked GM
   bank), **U8g2**.
3. Board / Tools settings:
   - **Board:** Raspberry Pi Pico / generic RP2040
   - **Flash Size:** a 16 MB layout that reserves an FS partition, e.g.
     *“15 MB Sketch + 1 MB FS”*.
   - **CPU Speed:** overclock to **≥ 200 MHz** (the bank + reverb/chorus need it).
   - **Optimize:** `-O3` / “Fast”.

### Choosing the OLED controller

These 128×64 panels are protocol-compatible and differ only in the controller
chip, so you can swap modules with **no rewiring** — change one line in
`config.h`:

```c
#define OLED_DRIVER OLED_DRV_SH1106    // default 1.3" modules
// #define OLED_DRIVER OLED_DRV_SSD1306
// #define OLED_DRIVER OLED_DRV_SSD1309
```

## Controls

`SHIFT` = hold the SHIFT button.

| Control            | Action                          | With SHIFT          |
|--------------------|---------------------------------|---------------------|
| PLAY               | play / stop                     | stop & rewind       |
| PAGE               | next view                       | previous view       |
| TRACK              | next track                      | previous track      |
| encoder rotate     | value / cursor                  | tempo (BPM)         |
| encoder push       | toggle step / next field        |                     |
| encoder long-press | clear current track row         |                     |
| REC (tap)          | accent / audition               | tie                 |
| REC (hold)+rotate  | edit step note / velocity       |                     |

Views: **STEP** grid · **INST** · **SYNTH** · **SONG** (pattern/chain/save-load).

## Re-baking the sample bank

To rebuild the ROM bank from a different SoundFont:

```bash
python3 tools/sf2_to_amy.py generate your_font.sf2 \
    --amy-src "AMY (1)/AMY/src" \
    --sketch  MedusaGM_good_v2_reverb_panning
```

This rewrites `pcm_tiny.h` + `pcm_samples_tiny.h` (the AMY ROM pool) and
`medusa_gm.h` (the GM program/drum → preset map). Each GM program gets one
sample taken near middle-C; looped instruments are trimmed to their loop point
(small, sustains forever), one-shots and drums keep their full decay. Use
`inspect` instead of `generate` to preview what a font contains. Tunables
(sample rate, decay caps) live at the top of the script — at 44.1 kHz the bank
is ~7 MB; drop `TARGET_SR` to 22050 to roughly halve it.
