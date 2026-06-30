#pragma once
// ============================================================================
//  Medusa GM  --  config.h
//  Board:   RP2040 w/ 16 MB flash (e.g. TENSTAR / WeAct RP2040 Pro Micro 16MB)
//           RP2350 / Pico 2 also supported -- defaults auto-tune per chip.
//           (arduino-pico core by earlephilhower)
//  Audio:   AMY synth engine -> I2S -> PCM5102 DAC
//  Display: 128x64 OLED, SH1106 by default (SSD1306/SSD1309 selectable, I2C)
//  Input:   1x rotary encoder w/ push, 5x momentary switches
//
//  The 16 MB flash holds a high-fidelity 44.1 kHz GM sample bank baked from a
//  full General-MIDI SoundFont (see tools/sf2_to_amy.py).
//
//  All wiring is collected here -- change pins to match your build.
// ============================================================================

#include <Arduino.h>

// ─── I2S audio out -> PCM5102 ───────────────────────────────────────────────
// AMY's pico-audio backend uses a PIO state machine: BCLK and LRCLK MUST be
// consecutive GPIOs (LRCLK = BCLK + 1).  DIN is the serial data line.
// PCM5102 wiring:  BCK<-GP15  LRCK<-GP16  DIN<-GP17  SCK->GND (internal PLL)
//                  FLT/DEMP/XSMT/FMT to GND/3V3 per the module's defaults.
#define PIN_I2S_BCLK   15      // -> PCM5102 BCK   (LRCK is BCLK+1 = GP16)
#define PIN_I2S_DOUT   17      // -> PCM5102 DIN

// ─── 128x64 OLED over hardware I2C ──────────────────────────────────────────
// 4-pin SCL/SDA panel.  GP4=SDA0, GP5=SCL0 -> I2C0 (Wire).
// At 400 kHz a full 128x64 frame takes ~25 ms; the 8-block AMY audio buffer
// (~47 ms) absorbs this without underruns.
// Add 2.2k-4.7k pull-ups on SDA/SCL if your module does not include them.
#define PIN_OLED_SDA    4      // I2C0 SDA -> OLED SDA
#define PIN_OLED_SCL    5      // I2C0 SCL -> OLED SCL
#define OLED_ADDR      0x3C    // 0x3C typical; some panels are 0x3D
#define OLED_I2C_HZ    400000  // Fast Mode

// Display controller.  These 128x64 panels are pin/protocol compatible and
// only differ in the controller IC, so you can swap modules by changing one
// number here (no rewiring).  The right choice for a given panel:
//   OLED_DRV_SH1106   1.3" "1.3 OLED" modules (most common cheap blue/white)
//   OLED_DRV_SSD1306  0.96"/1.3" SSD1306 panels
//   OLED_DRV_SSD1309  2.42" SSD1309 panels (the original Medusa build)
// SH1106 has 132 columns of RAM but a 128px window; U8g2's SH1106 ctor handles
// the 2-pixel offset automatically, so no code change is needed when switching.
#define OLED_DRV_SH1106   0
#define OLED_DRV_SSD1306  1
#define OLED_DRV_SSD1309  2

#ifndef OLED_DRIVER
#define OLED_DRIVER  OLED_DRV_SH1106   // default: SH1106 1.3" module
#endif

// ─── Rotary encoder (EC11 w/ push) ──────────────────────────────────────────
#define PIN_ENC_A       6
#define PIN_ENC_B       7
#define PIN_ENC_SW      8
#define ENC_TICKS_PER_DETENT 4

// ─── 5 momentary switches ───────────────────────────────────────────────────
// All active-low to GND with internal pull-ups.
#define PIN_BTN_PLAY    9      // play / stop          (shift = stop+rewind)
#define PIN_BTN_MUTE   13      // accent / edit / save (context; labelled MUTE)
#define PIN_BTN_TRACK  12      // next track           (shift = prev)
#define PIN_BTN_PAGE   11      // next view            (shift = prev)
#define PIN_BTN_SHIFT  10      // modifier (hold)

#define DEBOUNCE_MS    5
#define LONG_PRESS_MS  600

// ─── Optional click-out gate (reserved, not driven in v1) ───────────────────
#define PIN_CLICK_OUT  14

// ─── Sequencer dimensions ───────────────────────────────────────────────────
#define NUM_GM_TRACKS  16      // 16 General-MIDI tracks ...
#define SYNTH_TRACK    16      // ... plus 1 AMY synth track (index 16)
#define NUM_TRACKS     17
#define NUM_STEPS      16      // steps per pattern
#define NUM_PATTERNS   8       // patterns chainable into a song

// ─── AMY voice pool ─────────────────────────────────────────────────────────
// Defaults are auto-tuned to the chip.  The RP2040's twin Cortex-M0+ cores have
// roughly half the DSP throughput of the RP2350's M33s, so it gets a smaller
// pool to stay glitch-free; the RP2350 keeps the full 40 voices.  Override
// either symbol on the build command line to taste.
#if defined(ARDUINO_ARCH_RP2350)
  #ifndef AMY_MAX_OSCS
  #define AMY_MAX_OSCS 48
  #endif
  #ifndef POLYPHONY
  #define POLYPHONY    40      // simultaneously sounding notes (<= AMY_MAX_OSCS)
  #endif
#else   // RP2040 (and anything else): leaner pool for the M0+ cores
  #ifndef AMY_MAX_OSCS
  #define AMY_MAX_OSCS 32
  #endif
  #ifndef POLYPHONY
  #define POLYPHONY    24      // simultaneously sounding notes (<= AMY_MAX_OSCS)
  #endif
#endif

// ─── Stereo / effects ───────────────────────────────────────────────────────
// Panning is free and always on.  The reverb + chorus add a fixed per-block CPU
// cost.  The RP2350 at 250 MHz manages it alongside 40 voices + the OLED; the
// RP2040 should be overclocked (Tools > CPU Speed >= 200 MHz) for the full FX
// chain.  If you hear crackle/dropouts, set MEDUSA_FX to 0 (you keep stereo
// panning, you just lose the reverb/chorus) and/or lower POLYPHONY above.
#ifndef MEDUSA_FX
#define MEDUSA_FX       1      // 1 = reverb + chorus on, 0 = panning only
#endif

// ─── Tempo ──────────────────────────────────────────────────────────────────
#define BPM_DEFAULT    120
#define BPM_MIN        40
#define BPM_MAX        300
#define SWING_MAX      66      // % maximum swing

// ─── Persistence ────────────────────────────────────────────────────────────
#define STORAGE_MAGIC   0x4D47   // 'MG'
#define STORAGE_VERSION 2     // bumped: TrackCfg gained pan + reverb fields
