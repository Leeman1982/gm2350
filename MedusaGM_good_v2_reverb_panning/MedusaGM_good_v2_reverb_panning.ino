// ============================================================================
//  Medusa GM  --  a 16-track General-MIDI groovebox + 1 AMY synth track
//  ---------------------------------------------------------------------------
//  Hardware:  RP2040 w/ 16 MB flash (TENSTAR / WeAct RP2040 Pro Micro 16MB)
//             RP2350 / Pico 2 also supported  |  arduino-pico core
//             PCM5102 I2S DAC  |  128x64 SH1106 OLED  |  rotary encoder + 5 buttons
//
//  Audio is produced by the AMY synthesizer library.  The 16 GM tracks play
//  samples from a full General-MIDI SoundFont baked into AMY's flash PCM ROM
//  bank at 44.1 kHz by tools/sf2_to_amy.py; the 17th track is an AMY
//  subtractive synth voice.  The 7 MB bank is why this build wants 16 MB flash.
//
//  Build notes (Arduino IDE):
//    Board:        "Raspberry Pi Pico" / generic RP2040  (arduino-pico core)
//    Flash Size:   16 MB board.  Pick a layout that leaves an FS partition for
//                  song save/load, e.g. "15 MB Sketch + 1 MB FS" (the ~7 MB
//                  sample bank + code fit the sketch region with room to spare).
//    CPU Speed:    overclock to >= 200 MHz (the GM bank + reverb/chorus need it)
//    Optimize:     -O3 / "Fast"
//    Display:      set OLED_DRIVER in config.h (SH1106 default / SSD1306 / SSD1309)
//    Libraries:    AMY (this repo's fork w/ the GM bank), U8g2
//  For RP2350/Pico 2 the same sketch builds unchanged; config.h auto-tunes the
//  voice pool up to 40 voices.  See README.md for full wiring and the control map.
// ============================================================================

#include <AMY-Arduino.h>

#include "config.h"
#include "model.h"
#include "engine.h"
#include "sequencer.h"
#include "controls.h"
#include "storage.h"
#include "ui.h"

Song      song;
Engine    engine;
Sequencer sequencer;
Controls  controls;
Storage   storage;
UI        ui;

// Cached at boot: is the 160-preset GM bank actually linked?  Drives the LED
// heartbeat so we can read the board's state even if the OLED is dead.
static bool g_bankOK = false;

// Heartbeat run from loop() — its mere presence proves we reached loop() and
// are NOT bricked or boot-looping.  Two distinct, *calm* patterns (never the
// old fast panic-blink):
//   bank OK    -> slow steady 1 Hz  (......on/off, like a resting pulse)
//   bank wrong -> "blip-blip ... pause"  (two quick blinks every 1.5 s)
static void ledHeartbeat(bool ok) {
#ifdef LED_BUILTIN
    uint32_t t = millis();
    bool on;
    if (ok) {
        on = (t % 1000) < 500;                       // 1 Hz steady
    } else {
        uint32_t p = t % 1500;                       // double-blip every 1.5 s
        on = (p < 110) || (p >= 250 && p < 360);
    }
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#endif
}

void setup() {
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
#endif

    // ── Start AMY: stereo I2S out to the PCM5102, rendering across both cores.
    amy_config_t cfg = amy_default_config();
    cfg.audio              = AMY_AUDIO_IS_I2S;
    cfg.i2s_bclk           = PIN_I2S_BCLK;        // LRCLK is BCLK+1 (pico-audio)
    cfg.i2s_lrc            = PIN_I2S_BCLK + 1;
    cfg.i2s_dout           = PIN_I2S_DOUT;
    cfg.platform.multicore = 1;                   // use core1 for half the voices
    cfg.max_oscs           = AMY_MAX_OSCS;
    cfg.features.default_synths = 0;              // we drive oscillators ourselves
    cfg.features.startup_bleep  = 0;
    cfg.features.reverb    = MEDUSA_FX;            // stereo reverb (see config.h)
    cfg.features.chorus    = MEDUSA_FX;            // + chorus; set MEDUSA_FX 0 if CPU-bound
    cfg.features.echo      = 0;
    cfg.midi               = AMY_MIDI_IS_NONE;
    amy_start(cfg);

    // ── App modules.
    songInitDefault(song);
    storage.begin();
    engine.begin();
    sequencer.begin(&song, &engine);
    controls.begin();
    ui.begin(&song, &sequencer, &engine, &controls, &storage);

    // The firmware boots and runs no matter which AMY library is linked.  If the
    // GM bank isn't baked in (the stock Library-Manager AMY has only a handful of
    // ROM presets) you still get the full UI and sequencer; the GM voices just
    // fall back to whatever presets that library has.  The LED heartbeat and the
    // boot splash both report the linked preset count so a wrong library is
    // obvious without bricking anything.
    g_bankOK = engine.bankOK();

    // Global stereo reverb + chorus on the room/hall buses (no-op if MEDUSA_FX 0).
    engine.setupEffects();
}

void loop() {
    // Render + push one audio block to I2S (this paces the loop at block rate).
    amy_update();

    // Service controls, sequencer timing, and the display.
    controls.update();
    ui.handleInput();
    sequencer.update();
    ui.render();

    // Proof-of-life + bank status, visible even if the OLED is dead.
    ledHeartbeat(g_bankOK);
}
